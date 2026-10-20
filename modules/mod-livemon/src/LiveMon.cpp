/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license.
 */

#include "Config.h"
#include "DatabaseEnv.h"
#include "DBCStores.h"
#include "GameTime.h"
#include "Log.h"
#include "Player.h"
#include "PlayerScript.h"
#include "Pet.h"
#include "ScriptMgr.h"
#include "SpellAuraEffects.h"
#include "SpellAuras.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "StringFormat.h"
#include "UnitScript.h"
#include "WorldScript.h"
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/read_until.hpp>
#include <boost/asio/streambuf.hpp>
#include <boost/asio/write.hpp>
#include <atomic>
#include <cmath>
#include <map>
#include <mutex>
#include <sstream>
#include <thread>
#include <unordered_map>

namespace
{
constexpr std::array<AuraType, 9> ControlAuras = { SPELL_AURA_MOD_STUN, SPELL_AURA_MOD_FEAR, SPELL_AURA_MOD_CONFUSE,
    SPELL_AURA_MOD_ROOT, SPELL_AURA_MOD_SILENCE, SPELL_AURA_MOD_PACIFY_SILENCE, SPELL_AURA_MOD_CHARM,
    SPELL_AURA_MOD_POSSESS, SPELL_AURA_MOD_PACIFY };
constexpr uint32 SampleTick = 1000;
constexpr float MaxStepYards = 60.0f;
constexpr std::size_t TopSpells = 12;

struct Totals
{
    uint64 Damage = 0;
    uint64 Healing = 0;
    uint64 DamageTaken = 0;
    uint64 HealingTaken = 0;
    uint64 ControlGivenMs = 0;
    uint64 ControlTakenMs = 0;
    uint32 Controls = 0;
    uint32 Deaths = 0;
    uint32 Kills = 0;
    uint64 CombatMs = 0;
    double MovedYards = 0.0;

    void Add(Totals const& other)
    {
        Damage += other.Damage;
        Healing += other.Healing;
        DamageTaken += other.DamageTaken;
        HealingTaken += other.HealingTaken;
        ControlGivenMs += other.ControlGivenMs;
        ControlTakenMs += other.ControlTakenMs;
        Controls += other.Controls;
        Deaths += other.Deaths;
        Kills += other.Kills;
        CombatMs += other.CombatMs;
        MovedYards += other.MovedYards;
    }
};

struct Watched
{
    uint32 Guid = 0;
    std::string Name;
    uint8 Race = 0;
    uint8 Class = 0;
    uint8 Level = 0;
    uint8 Tree = 0;
    uint8 SubClass = 0;
    uint16 SubSpec = 0;
    uint32 Zone = 0;
    uint32 Map = 0;
    bool Online = false;
    bool InCombat = false;
    uint32 Health = 0;
    uint32 MaxHealth = 0;
    Totals Session;
    Totals Minute;
    std::map<uint32, uint64> SpellDamage;
    std::map<uint32, uint64> SpellHealing;
    float X = 0.0f;
    float Y = 0.0f;
    float Z = 0.0f;
    uint32 Tick = 0;
};

struct ControlKey
{
    uint32 Target = 0;
    uint32 Spell = 0;
    uint32 Caster = 0;
    bool operator<(ControlKey const& other) const
    {
        return std::tie(Target, Spell, Caster) < std::tie(other.Target, other.Spell, other.Caster);
    }
};

struct Settings
{
    bool Enabled = true;
    std::string Bind = "127.0.0.1";
    uint16 Port = 8089;
    uint32 FlushSeconds = 60;
};

std::mutex& liveMutex = *new std::mutex;
std::unordered_map<uint32, Watched>& watched = *new std::unordered_map<uint32, Watched>;
std::map<ControlKey, uint32>& controls = *new std::map<ControlKey, uint32>;
Settings& settings = *new Settings;
std::atomic<bool> serving{ false };
uint32 flushElapsed = 0;
uint32 startedAt = 0;

uint32 NowMs()
{
    return uint32(GameTime::GetGameTimeMS().count());
}

Player* Controller(Unit* unit)
{
    if (!unit)
        return nullptr;
    if (Player* player = unit->ToPlayer())
        return player;
    if (Unit* owner = unit->GetCharmerOrOwner())
        return owner->ToPlayer();
    return nullptr;
}

std::unordered_map<uint32, std::pair<uint8, uint16>> SubclassChoices()
{
    std::unordered_map<uint32, std::pair<uint8, uint16>> choices;
    if (!CharacterDatabase.Query("SELECT 1 FROM `information_schema`.`tables` WHERE `table_schema` = DATABASE() "
            "AND `table_name` = 'mod_subclass_character'"))
        return choices;
    if (QueryResult result = CharacterDatabase.Query("SELECT `guid`, `sub_class`, `sub_spec` FROM "
            "`mod_subclass_character`"))
        do
        {
            Field* fields = result->Fetch();
            choices[fields[0].Get<uint32>()] = { fields[1].Get<uint8>(), fields[2].Get<uint16>() };
        } while (result->NextRow());
    return choices;
}

void ApplySubclassChoices()
{
    auto const choices = SubclassChoices();
    std::lock_guard<std::mutex> lock(liveMutex);
    for (auto& [guid, entry] : watched)
    {
        auto const itr = choices.find(guid);
        entry.SubClass = itr == choices.end() ? 0 : itr->second.first;
        entry.SubSpec = itr == choices.end() ? 0 : itr->second.second;
    }
}

Watched& Track(Player* player)
{
    Watched& entry = watched[player->GetGUID().GetCounter()];
    entry.Guid = player->GetGUID().GetCounter();
    entry.Name = player->GetName();
    entry.Race = player->getRace();
    entry.Class = player->getClass();
    entry.Level = player->GetLevel();
    return entry;
}

void Refresh(Player* player, Watched& entry)
{
    entry.Level = player->GetLevel();
    entry.Tree = player->GetMostPointsTalentTree();
    entry.Zone = player->GetZoneId();
    entry.Map = player->GetMapId();
    entry.Online = true;
    entry.Health = player->GetHealth();
    entry.MaxHealth = player->GetMaxHealth();
    bool const fighting = player->IsInCombat();
    if (fighting && entry.InCombat)
    {
        entry.Session.CombatMs += SampleTick;
        entry.Minute.CombatMs += SampleTick;
    }
    entry.InCombat = fighting;
    float const step = std::hypot(player->GetPositionX() - entry.X, player->GetPositionY() - entry.Y,
        player->GetPositionZ() - entry.Z);
    if ((entry.X || entry.Y) && step < MaxStepYards)
    {
        entry.Session.MovedYards += step;
        entry.Minute.MovedYards += step;
    }
    entry.X = player->GetPositionX();
    entry.Y = player->GetPositionY();
    entry.Z = player->GetPositionZ();
}

bool IsControl(SpellInfo const* info)
{
    return std::any_of(ControlAuras.begin(), ControlAuras.end(), [info](AuraType type)
    {
        return info->HasAura(type);
    });
}

std::string Escaped(std::string const& text)
{
    std::string out;
    for (char c : text)
    {
        if (c == '"' || c == '\\')
            out += '\\';
        if (uint8(c) >= 0x20)
            out += c;
    }
    return out;
}

std::string ClassName(uint8 classId)
{
    if (ChrClassesEntry const* entry = sChrClassesStore.LookupEntry(classId))
        if (entry->name[0] && *entry->name[0])
            return entry->name[0];
    return Acore::StringFormat("Class {}", classId);
}

std::string RaceName(uint8 raceId)
{
    if (ChrRacesEntry const* entry = sChrRacesStore.LookupEntry(raceId))
        if (entry->name[0] && *entry->name[0])
            return entry->name[0];
    return Acore::StringFormat("Race {}", raceId);
}

std::string TotalsJson(Totals const& totals)
{
    return Acore::StringFormat("\"damage\":{},\"healing\":{},\"damageTaken\":{},\"healingTaken\":{},"
        "\"controlGivenMs\":{},\"controlTakenMs\":{},\"controls\":{},\"deaths\":{},\"kills\":{},\"combatMs\":{},"
        "\"movedYards\":{}", totals.Damage, totals.Healing, totals.DamageTaken, totals.HealingTaken,
        totals.ControlGivenMs, totals.ControlTakenMs, totals.Controls, totals.Deaths, totals.Kills, totals.CombatMs,
        uint64(totals.MovedYards));
}

std::string SpellsJson(std::map<uint32, uint64> const& spells)
{
    std::vector<std::pair<uint32, uint64>> sorted(spells.begin(), spells.end());
    std::sort(sorted.begin(), sorted.end(), [](auto const& a, auto const& b) { return a.second > b.second; });
    std::string out = "[";
    for (std::size_t index = 0; index < sorted.size() && index < TopSpells; ++index)
    {
        SpellInfo const* info = sSpellMgr->GetSpellInfo(sorted[index].first);
        std::string const name = sorted[index].first ? (info && info->SpellName[0] ? info->SpellName[0] : "?") :
            "Melee";
        out += Acore::StringFormat("{}{{\"id\":{},\"name\":\"{}\",\"icon\":{},\"amount\":{}}}", index ? "," : "",
            sorted[index].first, Escaped(name), info ? info->SpellIconID : 0, sorted[index].second);
    }
    return out + "]";
}

std::string LiveJson()
{
    std::lock_guard<std::mutex> lock(liveMutex);
    std::ostringstream out;
    out << "{\"serverTimeMs\":" << NowMs() << ",\"uptimeSeconds\":" << (GameTime::GetGameTime().count() - startedAt)
        << ",\"characters\":[";
    bool first = true;
    for (auto const& [guid, entry] : watched)
    {
        out << (first ? "" : ",") << Acore::StringFormat("{{\"guid\":{},\"name\":\"{}\",\"race\":\"{}\","
            "\"class\":\"{}\",\"classId\":{},\"level\":{},\"tree\":{},\"subclass\":\"{}\",\"subclassId\":{},"
            "\"subSpec\":{},\"zone\":{},\"map\":{},\"online\":{},\"inCombat\":{},\"health\":{},\"maxHealth\":{},"
            "\"session\":{{{}}},\"spells\":{},\"heals\":{}}}", guid, Escaped(entry.Name), Escaped(RaceName(entry.Race)),
            Escaped(ClassName(entry.Class)), entry.Class, entry.Level, entry.Tree,
            entry.SubClass ? Escaped(ClassName(entry.SubClass)) : "", entry.SubClass, entry.SubSpec, entry.Zone,
            entry.Map, entry.Online ? "true" : "false", entry.InCombat ? "true" : "false", entry.Health,
            entry.MaxHealth, TotalsJson(entry.Session), SpellsJson(entry.SpellDamage), SpellsJson(entry.SpellHealing));
        first = false;
    }
    out << "]}";
    return out.str();
}

std::string HistoryJson(uint32 minutes)
{
    std::ostringstream out;
    out << "{\"minutes\":" << minutes << ",\"combos\":[";
    QueryResult result = CharacterDatabase.Query("SELECT `race`, `class`, `sub_class`, `tree`, "
        "COUNT(DISTINCT `guid`), SUM(`damage`), SUM(`healing`), SUM(`damage_taken`), SUM(`control_given_ms`), "
        "SUM(`control_taken_ms`), SUM(`deaths`), SUM(`kills`), SUM(`combat_ms`), SUM(`moved_yards`), "
        "AVG(`level`) FROM `livemon_minute` WHERE `minute` >= UNIX_TIMESTAMP() DIV 60 - {} "
        "GROUP BY `race`, `class`, `sub_class`, `tree`", minutes);
    bool first = true;
    if (result)
        do
        {
            Field* f = result->Fetch();
            out << (first ? "" : ",") << Acore::StringFormat("{{\"race\":\"{}\",\"class\":\"{}\",\"classId\":{},"
                "\"subclass\":\"{}\","
                "\"tree\":{},\"characters\":{},\"damage\":{},\"healing\":{},\"damageTaken\":{},\"controlGivenMs\":{},"
                "\"controlTakenMs\":{},\"deaths\":{},\"kills\":{},\"combatMs\":{},\"movedYards\":{},\"level\":{}}}",
                Escaped(RaceName(f[0].Get<uint8>())), Escaped(ClassName(f[1].Get<uint8>())), f[1].Get<uint8>(),
                f[2].Get<uint8>() ? Escaped(ClassName(f[2].Get<uint8>())) : "", f[3].Get<uint8>(),
                f[4].Get<std::string>(),
                f[5].Get<std::string>(), f[6].Get<std::string>(), f[7].Get<std::string>(), f[8].Get<std::string>(),
                f[9].Get<std::string>(), f[10].Get<std::string>(), f[11].Get<std::string>(), f[12].Get<std::string>(),
                f[13].Get<std::string>(), f[14].Get<std::string>());
            first = false;
        } while (result->NextRow());
    out << "]}";
    return out.str();
}

void Flush()
{
    uint32 const minute = uint32(GameTime::GetGameTime().count() / 60);
    std::vector<std::string> rows;
    {
        std::lock_guard<std::mutex> lock(liveMutex);
        for (auto& [guid, entry] : watched)
        {
            Totals const& t = entry.Minute;
            if (t.Damage || t.Healing || t.DamageTaken || t.CombatMs || t.MovedYards > 1.0 || t.Deaths || t.Kills)
                rows.push_back(Acore::StringFormat(
                    "({}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {})",
                    minute, guid, entry.Race, entry.Class, entry.Level, entry.Tree, entry.SubClass, entry.SubSpec,
                    t.Damage, t.Healing, t.DamageTaken, t.HealingTaken, t.ControlGivenMs, t.ControlTakenMs, t.Deaths,
                    t.Kills, t.CombatMs, uint64(t.MovedYards)));
            entry.Minute = Totals();
        }
    }
    if (rows.empty())
        return;
    std::string values;
    for (std::string const& row : rows)
        values += (values.empty() ? "" : ",") + row;
    CharacterDatabase.Execute("INSERT INTO `livemon_minute` (`minute`, `guid`, `race`, `class`, `level`, `tree`, "
        "`sub_class`, `sub_spec`, `damage`, `healing`, `damage_taken`, `healing_taken`, `control_given_ms`, "
        "`control_taken_ms`, `deaths`, `kills`, `combat_ms`, `moved_yards`) VALUES " + values +
        " ON DUPLICATE KEY UPDATE `damage` = `damage` + VALUES(`damage`), `healing` = `healing` + VALUES(`healing`), "
        "`damage_taken` = `damage_taken` + VALUES(`damage_taken`), `healing_taken` = `healing_taken` + "
        "VALUES(`healing_taken`), `control_given_ms` = `control_given_ms` + VALUES(`control_given_ms`), "
        "`control_taken_ms` = `control_taken_ms` + VALUES(`control_taken_ms`), `deaths` = `deaths` + "
        "VALUES(`deaths`), `kills` = `kills` + VALUES(`kills`), `combat_ms` = `combat_ms` + VALUES(`combat_ms`), "
        "`moved_yards` = `moved_yards` + VALUES(`moved_yards`)");
}

void CreateTable()
{
    CharacterDatabase.DirectExecute(
        "CREATE TABLE IF NOT EXISTS `livemon_minute` ( "
        "`minute` INT UNSIGNED NOT NULL, "
        "`guid` INT UNSIGNED NOT NULL, "
        "`race` TINYINT UNSIGNED NOT NULL DEFAULT 0, "
        "`class` TINYINT UNSIGNED NOT NULL DEFAULT 0, "
        "`level` TINYINT UNSIGNED NOT NULL DEFAULT 0, "
        "`tree` TINYINT UNSIGNED NOT NULL DEFAULT 0, "
        "`sub_class` TINYINT UNSIGNED NOT NULL DEFAULT 0, "
        "`sub_spec` SMALLINT UNSIGNED NOT NULL DEFAULT 0, "
        "`damage` BIGINT UNSIGNED NOT NULL DEFAULT 0, "
        "`healing` BIGINT UNSIGNED NOT NULL DEFAULT 0, "
        "`damage_taken` BIGINT UNSIGNED NOT NULL DEFAULT 0, "
        "`healing_taken` BIGINT UNSIGNED NOT NULL DEFAULT 0, "
        "`control_given_ms` BIGINT UNSIGNED NOT NULL DEFAULT 0, "
        "`control_taken_ms` BIGINT UNSIGNED NOT NULL DEFAULT 0, "
        "`deaths` INT UNSIGNED NOT NULL DEFAULT 0, "
        "`kills` INT UNSIGNED NOT NULL DEFAULT 0, "
        "`combat_ms` BIGINT UNSIGNED NOT NULL DEFAULT 0, "
        "`moved_yards` BIGINT UNSIGNED NOT NULL DEFAULT 0, "
        "PRIMARY KEY (`minute`, `guid`), "
        "KEY `idx_combo` (`race`, `class`, `sub_class`, `tree`) "
        ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 ");
}

std::string Respond(std::string const& request)
{
    std::istringstream line(request);
    std::string method;
    std::string path;
    line >> method >> path;
    std::string body;
    if (path.rfind("/live", 0) == 0)
        body = LiveJson();
    else if (path.rfind("/history", 0) == 0)
    {
        uint32 minutes = 60;
        if (std::size_t const at = path.find("minutes="); at != std::string::npos)
            minutes = std::max<uint32>(1, std::strtoul(path.c_str() + at + 8, nullptr, 10));
        body = HistoryJson(minutes);
    }
    else
        body = "{\"livemon\":1,\"endpoints\":[\"/live\",\"/history?minutes=60\"]}";
    return "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nAccess-Control-Allow-Origin: *\r\n"
        "Cache-Control: no-store\r\nConnection: close\r\nContent-Length: " + std::to_string(body.size()) + "\r\n\r\n" +
        body;
}

void Serve()
{
    try
    {
        boost::asio::io_context context;
        boost::asio::ip::tcp::acceptor acceptor(context,
            { boost::asio::ip::make_address(settings.Bind), settings.Port });
        LOG_INFO("module", "LiveMon: live feed on http://{}:{}/live", settings.Bind, settings.Port);
        while (serving)
        {
            boost::asio::ip::tcp::socket socket(context);
            acceptor.accept(socket);
            boost::asio::streambuf buffer;
            boost::system::error_code error;
            boost::asio::read_until(socket, buffer, "\r\n\r\n", error);
            std::string const request{ std::istreambuf_iterator<char>(&buffer), std::istreambuf_iterator<char>() };
            std::string const response = Respond(request);
            boost::asio::write(socket, boost::asio::buffer(response), error);
        }
    }
    catch (std::exception const& problem)
    {
        LOG_ERROR("module", "LiveMon: live feed stopped: {}", problem.what());
    }
}
}

class livemon_world : public WorldScript
{
public:
    livemon_world() : WorldScript("livemon_world", { WORLDHOOK_ON_AFTER_CONFIG_LOAD, WORLDHOOK_ON_STARTUP,
        WORLDHOOK_ON_UPDATE }) { }

    void OnAfterConfigLoad(bool /*reload*/) override
    {
        settings.Enabled = sConfigMgr->GetOption<bool>("LiveMon.Enable", true);
        settings.Bind = sConfigMgr->GetOption<std::string>("LiveMon.Bind", "127.0.0.1");
        settings.Port = uint16(sConfigMgr->GetOption<uint32>("LiveMon.Port", 8089));
        settings.FlushSeconds = std::max<uint32>(10, sConfigMgr->GetOption<uint32>("LiveMon.FlushSeconds", 60));
    }

    void OnStartup() override
    {
        startedAt = uint32(GameTime::GetGameTime().count());
        if (!settings.Enabled || serving.exchange(true))
            return;
        CreateTable();
        std::thread(Serve).detach();
    }

    void OnUpdate(uint32 diff) override
    {
        if (!settings.Enabled)
            return;
        flushElapsed += diff;
        if (flushElapsed < settings.FlushSeconds * IN_MILLISECONDS)
            return;
        flushElapsed = 0;
        ApplySubclassChoices();
        Flush();
    }
};

class livemon_player : public PlayerScript
{
public:
    livemon_player() : PlayerScript("livemon_player", { PLAYERHOOK_ON_LOGIN, PLAYERHOOK_ON_LOGOUT,
        PLAYERHOOK_ON_UPDATE, PLAYERHOOK_ON_PLAYER_JUST_DIED, PLAYERHOOK_ON_CREATURE_KILL, PLAYERHOOK_ON_PVP_KILL }) { }

    void OnPlayerLogin(Player* player) override
    {
        if (!settings.Enabled)
            return;
        {
            std::lock_guard<std::mutex> lock(liveMutex);
            Refresh(player, Track(player));
        }
        ApplySubclassChoices();
    }

    void OnPlayerLogout(Player* player) override
    {
        std::lock_guard<std::mutex> lock(liveMutex);
        if (auto itr = watched.find(player->GetGUID().GetCounter()); itr != watched.end())
            itr->second.Online = false;
    }

    void OnPlayerUpdate(Player* player, uint32 diff) override
    {
        if (!settings.Enabled)
            return;
        std::lock_guard<std::mutex> lock(liveMutex);
        Watched& entry = Track(player);
        entry.Tick += diff;
        if (entry.Tick < SampleTick)
            return;
        entry.Tick = 0;
        Refresh(player, entry);
    }

    void OnPlayerJustDied(Player* player) override
    {
        std::lock_guard<std::mutex> lock(liveMutex);
        Watched& entry = Track(player);
        ++entry.Session.Deaths;
        ++entry.Minute.Deaths;
    }

    void OnPlayerCreatureKill(Player* killer, Creature* /*killed*/) override
    {
        std::lock_guard<std::mutex> lock(liveMutex);
        Watched& entry = Track(killer);
        ++entry.Session.Kills;
        ++entry.Minute.Kills;
    }

    void OnPlayerPVPKill(Player* killer, Player* /*killed*/) override
    {
        OnPlayerCreatureKill(killer, nullptr);
    }
};

class livemon_units : public UnitScript
{
public:
    livemon_units() : UnitScript("livemon_units", true, { UNITHOOK_ON_HEAL, UNITHOOK_MODIFY_SPELL_DAMAGE_TAKEN,
        UNITHOOK_MODIFY_MELEE_DAMAGE, UNITHOOK_ON_PERIODIC_DAMAGE_RESULT, UNITHOOK_MODIFY_HEAL_RECEIVED,
        UNITHOOK_ON_AURA_APPLY, UNITHOOK_ON_AURA_REMOVE }) { }

    uint32 DealDamage(Unit* attacker, Unit* victim, uint32 damage, DamageEffectType /*type*/,
        std::optional<uint32>* /*leech*/) override
    {
        if (!settings.Enabled || !damage || attacker == victim)
            return damage;
        Player* dealer = Controller(attacker);
        Player* taker = victim ? victim->ToPlayer() : nullptr;
        if (!dealer && !taker)
            return damage;
        std::lock_guard<std::mutex> lock(liveMutex);
        if (dealer)
        {
            Watched& entry = Track(dealer);
            entry.Session.Damage += damage;
            entry.Minute.Damage += damage;
        }
        if (taker)
        {
            Watched& entry = Track(taker);
            entry.Session.DamageTaken += damage;
            entry.Minute.DamageTaken += damage;
        }
        return damage;
    }

    void OnHeal(Unit* healer, Unit* receiver, uint32& gain) override
    {
        if (!settings.Enabled || !gain)
            return;
        Player* giver = Controller(healer);
        Player* taker = receiver ? receiver->ToPlayer() : nullptr;
        if (!giver && !taker)
            return;
        std::lock_guard<std::mutex> lock(liveMutex);
        if (giver)
        {
            Watched& entry = Track(giver);
            entry.Session.Healing += gain;
            entry.Minute.Healing += gain;
        }
        if (taker)
        {
            Watched& entry = Track(taker);
            entry.Session.HealingTaken += gain;
            entry.Minute.HealingTaken += gain;
        }
    }

    void NoteSpell(Unit* attacker, uint32 spellId, uint64 amount, bool healing)
    {
        Player* dealer = Controller(attacker);
        if (!settings.Enabled || !dealer || !amount)
            return;
        std::lock_guard<std::mutex> lock(liveMutex);
        Watched& entry = Track(dealer);
        (healing ? entry.SpellHealing : entry.SpellDamage)[spellId] += amount;
    }

    void ModifySpellDamageTaken(Unit* /*target*/, Unit* attacker, int32& damage, SpellInfo const* info) override
    {
        if (damage > 0 && info)
            NoteSpell(attacker, info->Id, uint64(damage), false);
    }

    void ModifyMeleeDamage(Unit* /*target*/, Unit* attacker, uint32& damage) override
    {
        NoteSpell(attacker, 0, damage, false);
    }

    void OnPeriodicDamageResult(Unit* /*target*/, Unit* attacker, uint32 damage, SpellInfo const* info) override
    {
        if (info)
            NoteSpell(attacker, info->Id, damage, false);
    }

    void ModifyHealReceived(Unit* /*target*/, Unit* healer, uint32& heal, SpellInfo const* info) override
    {
        if (info)
            NoteSpell(healer, info->Id, heal, true);
    }

    void OnAuraApply(Unit* unit, Aura* aura) override
    {
        if (!settings.Enabled || !unit || !aura || !IsControl(aura->GetSpellInfo()))
            return;
        Player* caster = Controller(aura->GetCaster());
        Player* target = unit->ToPlayer();
        if ((!caster && !target) || caster == target)
            return;
        std::lock_guard<std::mutex> lock(liveMutex);
        controls[{ unit->GetGUID().GetCounter(), aura->GetId(), aura->GetCasterGUID().GetCounter() }] = NowMs();
        if (caster)
        {
            ++Track(caster).Session.Controls;
            ++Track(caster).Minute.Controls;
        }
    }

    void OnAuraRemove(Unit* unit, AuraApplication* application, AuraRemoveMode /*mode*/) override
    {
        if (!settings.Enabled || !unit || !application)
            return;
        Aura const* aura = application->GetBase();
        if (!aura || !IsControl(aura->GetSpellInfo()))
            return;
        Player* caster = Controller(aura->GetCaster());
        Player* target = unit->ToPlayer();
        std::lock_guard<std::mutex> lock(liveMutex);
        auto itr = controls.find({ unit->GetGUID().GetCounter(), aura->GetId(), aura->GetCasterGUID().GetCounter() });
        if (itr == controls.end())
            return;
        uint64 const lasted = NowMs() - itr->second;
        controls.erase(itr);
        if (caster && caster != target)
        {
            Track(caster).Session.ControlGivenMs += lasted;
            Track(caster).Minute.ControlGivenMs += lasted;
        }
        if (target && caster != target)
        {
            Track(target).Session.ControlTakenMs += lasted;
            Track(target).Minute.ControlTakenMs += lasted;
        }
    }
};

void AddSC_livemon()
{
    new livemon_world();
    new livemon_player();
    new livemon_units();
}
