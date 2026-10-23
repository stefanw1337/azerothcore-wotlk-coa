/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license.
 */

#include "AscensionCoATalentData.h"
#include "AscensionSpecialization.h"
#include "ClientDBC.h"
#include "Config.h"
#include "DatabaseEnv.h"
#include "DBCStores.h"
#include "GameTime.h"
#include "Item.h"
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
#include <cstring>
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
constexpr uint32 MinFightMs = 15000;
constexpr uint32 SnapshotRefreshMs = 2000;
constexpr uint32 SnapshotInterestMs = 30000;
constexpr uint32 BuildRefreshMs = 60000;
constexpr std::size_t RecordsShown = 15;
constexpr uint32 StandardTalentKey = 0x80000000;

struct Totals
{
    uint64 Damage = 0;
    uint64 Healing = 0;
    uint64 DamageTaken = 0;
    uint64 HealingTaken = 0;
    uint64 ControlGivenMs = 0;
    uint64 ControlTakenMs = 0;
    uint64 BuffGivenMs = 0;
    uint32 Controls = 0;
    uint32 Deaths = 0;
    uint32 Kills = 0;
    uint64 CombatMs = 0;
    double MovedYards = 0.0;
};

struct Tally
{
    uint64 Amount = 0;
    uint32 Count = 0;
};

enum RecordKind : uint8
{
    RecordHit,
    RecordHeal,
    RecordFightDps,
    RecordFightHps,
    RecordSpeed,
    RecordKinds
};

struct Record
{
    double Value = 0.0;
    uint32 Spell = 0;
    uint32 At = 0;
    std::string Name;
    uint8 Race = 0;
    uint8 Class = 0;
    uint8 SubClass = 0;
    uint8 Level = 0;
    bool Dirty = false;
};

struct Watched
{
    uint32 Guid = 0;
    std::string Name;
    uint8 Race = 0;
    uint8 Class = 0;
    uint8 Level = 0;
    uint8 Tree = 0;
    std::string Spec;
    uint8 SubClass = 0;
    uint16 SubSpec = 0;
    uint32 Build = 0;
    uint32 Zone = 0;
    uint32 Map = 0;
    bool Online = false;
    bool InCombat = false;
    uint32 Health = 0;
    uint32 MaxHealth = 0;
    float RunSpeed = 0.0f;
    float MovedPerSecond = 0.0f;
    Totals Session;
    Totals Minute;
    std::map<uint32, Tally> SpellDamage;
    std::map<uint32, Tally> SpellHealing;
    std::map<uint32, Tally> MinuteSpellDamage;
    std::map<uint32, Tally> MinuteSpellHealing;
    std::map<uint32, Tally> ControlBySpell;
    std::map<uint32, Tally> BuffBySpell;
    uint32 LastSpell = 0;
    uint32 LastHeal = 0;
    uint32 FightStart = 0;
    uint64 FightDamage = 0;
    uint64 FightHealing = 0;
    float X = 0.0f;
    float Y = 0.0f;
    float Z = 0.0f;
    uint32 Tick = 0;
    uint32 BuildTick = BuildRefreshMs;
    std::string Snapshot;
    uint32 SnapshotAt = 0;
    uint32 WantedUntil = 0;
};

struct AuraKey
{
    uint32 Target = 0;
    uint32 Spell = 0;
    uint32 Caster = 0;
    bool operator<(AuraKey const& other) const
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
std::map<AuraKey, uint32>& controls = *new std::map<AuraKey, uint32>;
std::map<AuraKey, uint32>& buffs = *new std::map<AuraKey, uint32>;
std::map<std::pair<uint8, uint32>, Record>& records = *new std::map<std::pair<uint8, uint32>, Record>;
Settings& settings = *new Settings;
std::atomic<bool> serving{ false };
uint32 flushElapsed = 0;
uint32 startedAt = 0;

uint32 NowMs()
{
    return uint32(GameTime::GetGameTimeMS().count());
}

uint32 NowSeconds()
{
    return uint32(GameTime::GetGameTime().count());
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

std::string SpellName(uint32 spellId)
{
    SpellInfo const* info = sSpellMgr->GetSpellInfo(spellId);
    return info && info->SpellName[0] ? info->SpellName[0] : "";
}

uint32 SpellIcon(uint32 spellId)
{
    SpellInfo const* info = sSpellMgr->GetSpellInfo(spellId);
    return info ? info->SpellIconID : 0;
}

AscensionCompatData::CoATalentEntry const* FindEntry(uint32 entryId)
{
    auto const& entries = AscensionCompatData::CoATalentEntries;
    auto itr = std::lower_bound(entries.begin(), entries.end(), entryId,
        [](AscensionCompatData::CoATalentEntry const& entry, uint32 id) { return entry.EntryId < id; });
    return itr != entries.end() && itr->EntryId == entryId ? &*itr : nullptr;
}

constexpr uint32 ChrSpecsDisplayName = 29;
std::unordered_map<uint16, std::string> specNames;

void LoadSpecNames()
{
    ClientDBC specs;
    if (!specs.Load(GetClientDBCPath("ChrSpecs.dbc"), ChrSpecsDisplayName + 1))
        return;
    for (uint32 row = 0; row < specs.GetRecordCount(); ++row)
    {
        ClientDBC::Record const record = specs.GetRecord(row);
        if (std::string_view const name = record.GetString(ChrSpecsDisplayName); !name.empty())
            specNames[uint16(record.GetUInt32(0))] = std::string(name);
    }
    LOG_INFO("module", "LiveMon: {} specialization names", specNames.size());
}

std::string CoASpecName(uint16 specId)
{
    if (auto const itr = specNames.find(specId); itr != specNames.end())
        return itr->second;
    return specId ? Acore::StringFormat("Spec {}", specId) : "";
}

std::string SpecOf(Player* player)
{
    return CoASpecName(uint16(GetAscensionActiveSpecialization(player)));
}

void NoteRecord(Player* player, RecordKind kind, double value, uint32 spellId, uint8 subClass)
{
    Record& record = records[{ uint8(kind), player->GetGUID().GetCounter() }];
    if (value <= record.Value)
        return;
    record = { value, spellId, NowSeconds(), player->GetName(), player->getRace(), player->getClass(), subClass,
        player->GetLevel(), true };
}

void EndFight(Player* player, Watched& entry)
{
    uint32 const lasted = NowMs() - entry.FightStart;
    if (entry.FightStart && lasted >= MinFightMs)
    {
        NoteRecord(player, RecordFightDps, entry.FightDamage * 1000.0 / lasted, 0, entry.SubClass);
        NoteRecord(player, RecordFightHps, entry.FightHealing * 1000.0 / lasted, 0, entry.SubClass);
    }
    entry.FightStart = 0;
    entry.FightDamage = 0;
    entry.FightHealing = 0;
}

std::vector<std::pair<uint32, uint32>> StandardTalentsOf(Player const* player)
{
    std::vector<std::pair<uint32, uint32>> known;
    uint8 const spec = player->GetActiveSpec();
    for (TalentEntry const* talent : sTalentStore)
    {
        if (!talent)
            continue;
        for (int rank = MAX_TALENT_RANK - 1; rank >= 0; --rank)
            if (talent->RankID[rank] && player->HasTalent(talent->RankID[rank], spec))
            {
                known.push_back({ talent->TalentID, uint32(rank + 1) });
                break;
            }
    }
    return known;
}

uint32 BuildHash(Player const* player)
{
    uint32 hash = 2166136261u;
    auto mix = [&hash](uint32 value)
    {
        for (int shift = 0; shift < 32; shift += 8)
        {
            hash ^= (value >> shift) & 0xFF;
            hash *= 16777619u;
        }
    };
    std::vector<AscensionCoATalentState::KnownEntry> entries = GetAscensionKnownTalentEntries(player);
    std::sort(entries.begin(), entries.end(), [](auto const& a, auto const& b) { return a.EntryId < b.EntryId; });
    for (auto const& entry : entries)
        if (entry.Rank)
        {
            mix(entry.EntryId);
            mix(entry.Rank);
        }
    for (auto const& [talentId, rank] : StandardTalentsOf(player))
    {
        mix(talentId | StandardTalentKey);
        mix(rank);
    }
    return hash;
}

void Refresh(Player* player, Watched& entry, uint32 elapsed)
{
    entry.Level = player->GetLevel();
    entry.Tree = player->GetMostPointsTalentTree();
    entry.Zone = player->GetZoneId();
    entry.Map = player->GetMapId();
    entry.Online = true;
    entry.Health = player->GetHealth();
    entry.MaxHealth = player->GetMaxHealth();
    entry.RunSpeed = player->GetSpeed(MOVE_RUN);
    NoteRecord(player, RecordSpeed, entry.RunSpeed, 0, entry.SubClass);
    bool const fighting = player->IsInCombat();
    if (fighting && entry.InCombat)
    {
        entry.Session.CombatMs += elapsed;
        entry.Minute.CombatMs += elapsed;
    }
    if (fighting && !entry.FightStart)
        entry.FightStart = NowMs();
    else if (!fighting && entry.FightStart)
        EndFight(player, entry);
    entry.InCombat = fighting;
    float const step = std::hypot(player->GetPositionX() - entry.X, player->GetPositionY() - entry.Y,
        player->GetPositionZ() - entry.Z);
    entry.MovedPerSecond = 0.0f;
    if ((entry.X || entry.Y) && step < MaxStepYards && !player->IsInFlight())
    {
        entry.Session.MovedYards += step;
        entry.Minute.MovedYards += step;
        entry.MovedPerSecond = step * 1000.0f / std::max<uint32>(elapsed, 1);
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

bool IsOfferedBuff(Aura const* aura, Unit const* target)
{
    SpellInfo const* info = aura->GetSpellInfo();
    return info->IsPositive() && !aura->IsPassive() && aura->GetMaxDuration() > 0 && aura->GetCasterGUID() !=
        target->GetGUID() && !IsControl(info);
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
        "\"controlGivenMs\":{},\"controlTakenMs\":{},\"buffGivenMs\":{},\"controls\":{},\"deaths\":{},\"kills\":{},"
        "\"combatMs\":{},\"movedYards\":{}", totals.Damage, totals.Healing, totals.DamageTaken, totals.HealingTaken,
        totals.ControlGivenMs, totals.ControlTakenMs, totals.BuffGivenMs, totals.Controls, totals.Deaths, totals.Kills,
        totals.CombatMs, uint64(totals.MovedYards));
}

std::string SpellsJson(std::map<uint32, Tally> const& spells, std::size_t limit)
{
    std::vector<std::pair<uint32, Tally>> sorted(spells.begin(), spells.end());
    std::sort(sorted.begin(), sorted.end(), [](auto const& a, auto const& b)
    {
        return a.second.Amount > b.second.Amount;
    });
    std::string out = "[";
    for (std::size_t index = 0; index < sorted.size() && index < limit; ++index)
    {
        uint32 const spellId = sorted[index].first;
        std::string const name = spellId ? SpellName(spellId) : "Melee";
        out += Acore::StringFormat("{}{{\"id\":{},\"name\":\"{}\",\"icon\":{},\"amount\":{},\"count\":{}}}",
            index ? "," : "", spellId, Escaped(name.empty() ? "?" : name), SpellIcon(spellId),
            sorted[index].second.Amount, sorted[index].second.Count);
    }
    return out + "]";
}

std::string LiveJson()
{
    std::lock_guard<std::mutex> lock(liveMutex);
    std::ostringstream out;
    out << "{\"serverTimeMs\":" << NowMs() << ",\"uptimeSeconds\":" << (NowSeconds() - startedAt)
        << ",\"characters\":[";
    bool first = true;
    for (auto const& [guid, entry] : watched)
    {
        out << (first ? "" : ",") << Acore::StringFormat("{{\"guid\":{},\"name\":\"{}\",\"race\":\"{}\","
            "\"class\":\"{}\",\"classId\":{},\"level\":{},\"tree\":{},\"spec\":\"{}\",\"subclass\":\"{}\","
            "\"subclassId\":{},\"subSpec\":{},\"subSpecName\":\"{}\",\"build\":{},\"zone\":{},\"map\":{},"
            "\"online\":{},\"inCombat\":{},\"health\":{},\"maxHealth\":{},\"runSpeed\":{:.2f},\"moving\":{:.2f},"
            "\"session\":{{{}}},\"spells\":{},\"heals\":{}}}", guid, Escaped(entry.Name),
            Escaped(RaceName(entry.Race)), Escaped(ClassName(entry.Class)), entry.Class, entry.Level, entry.Tree,
            Escaped(entry.Spec), entry.SubClass ? Escaped(ClassName(entry.SubClass)) : "", entry.SubClass,
            entry.SubSpec, Escaped(CoASpecName(entry.SubSpec)), entry.Build, entry.Zone, entry.Map,
            entry.Online ? "true" : "false", entry.InCombat ? "true" : "false", entry.Health, entry.MaxHealth,
            entry.RunSpeed, entry.MovedPerSecond, TotalsJson(entry.Session), SpellsJson(entry.SpellDamage, TopSpells),
            SpellsJson(entry.SpellHealing, TopSpells));
        first = false;
    }
    out << "]}";
    return out.str();
}

std::string HistoryJson(uint32 minutes)
{
    std::ostringstream out;
    out << "{\"minutes\":" << minutes << ",\"combos\":[";
    QueryResult result = CharacterDatabase.Query("SELECT `race`, `class`, `sub_class`, `tree`, `sub_spec`, "
        "COUNT(DISTINCT `guid`), SUM(`damage`), SUM(`healing`), SUM(`damage_taken`), SUM(`control_given_ms`), "
        "SUM(`control_taken_ms`), SUM(`deaths`), SUM(`kills`), SUM(`combat_ms`), SUM(`moved_yards`), "
        "AVG(`level`), COUNT(*) FROM `livemon_minute` WHERE `minute` >= UNIX_TIMESTAMP() DIV 60 - {} "
        "GROUP BY `race`, `class`, `sub_class`, `tree`, `sub_spec`", minutes);
    bool first = true;
    if (result)
        do
        {
            Field* f = result->Fetch();
            out << (first ? "" : ",") << Acore::StringFormat("{{\"race\":\"{}\",\"class\":\"{}\",\"classId\":{},"
                "\"subclass\":\"{}\",\"tree\":{},\"subSpecName\":\"{}\",\"characters\":{},\"damage\":{},"
                "\"healing\":{},\"damageTaken\":{},\"controlGivenMs\":{},\"controlTakenMs\":{},\"deaths\":{},"
                "\"kills\":{},\"combatMs\":{},\"movedYards\":{},\"level\":{},\"minutes\":{}}}",
                Escaped(RaceName(f[0].Get<uint8>())), Escaped(ClassName(f[1].Get<uint8>())), f[1].Get<uint8>(),
                f[2].Get<uint8>() ? Escaped(ClassName(f[2].Get<uint8>())) : "", f[3].Get<uint8>(),
                Escaped(CoASpecName(f[4].Get<uint16>())), f[5].Get<std::string>(), f[6].Get<std::string>(),
                f[7].Get<std::string>(), f[8].Get<std::string>(), f[9].Get<std::string>(), f[10].Get<std::string>(),
                f[11].Get<std::string>(), f[12].Get<std::string>(), f[13].Get<std::string>(),
                f[14].Get<std::string>(), f[15].Get<std::string>(), f[16].Get<std::string>());
            first = false;
        } while (result->NextRow());
    out << "]}";
    return out.str();
}

std::string RecordsJson()
{
    std::array<std::vector<std::pair<uint32, Record>>, RecordKinds> byKind;
    {
        std::lock_guard<std::mutex> lock(liveMutex);
        for (auto const& [key, record] : records)
            byKind[key.first].push_back({ key.second, record });
    }
    static constexpr std::array<char const*, RecordKinds> Names = { "hit", "heal", "fightDps", "fightHps", "speed" };
    std::string out = "{";
    for (std::size_t kind = 0; kind < byKind.size(); ++kind)
    {
        auto& list = byKind[kind];
        std::sort(list.begin(), list.end(), [](auto const& a, auto const& b)
        {
            return a.second.Value > b.second.Value;
        });
        out += Acore::StringFormat("{}\"{}\":[", kind ? "," : "", Names[kind]);
        for (std::size_t index = 0; index < list.size() && index < RecordsShown; ++index)
        {
            Record const& record = list[index].second;
            out += Acore::StringFormat("{}{{\"guid\":{},\"name\":\"{}\",\"race\":\"{}\",\"class\":\"{}\","
                "\"classId\":{},\"subclass\":\"{}\",\"level\":{},\"value\":{:.2f},\"spell\":\"{}\",\"icon\":{},"
                "\"at\":{}}}", index ? "," : "", list[index].first, Escaped(record.Name),
                Escaped(RaceName(record.Race)), Escaped(ClassName(record.Class)), record.Class,
                record.SubClass ? Escaped(ClassName(record.SubClass)) : "", record.Level, record.Value,
                Escaped(record.Spell ? SpellName(record.Spell) : ""), SpellIcon(record.Spell), record.At);
        }
        out += "]";
    }
    return out + "}";
}

std::unordered_map<uint32, uint32> const& SpellSources()
{
    static std::unordered_map<uint32, uint32> const sources = []
    {
        std::unordered_map<uint32, uint32> found;
        auto link = [&found](uint32 spellId, uint32 key)
        {
            if (!spellId)
                return;
            found.try_emplace(spellId, key);
            if (SpellInfo const* info = sSpellMgr->GetSpellInfo(spellId))
                for (SpellEffectInfo const& effect : info->GetEffects())
                    if (effect.TriggerSpell)
                        found.try_emplace(effect.TriggerSpell, key);
        };
        for (AscensionCompatData::CoATalentEntry const& entry : AscensionCompatData::CoATalentEntries)
            for (uint32 spellId : entry.SpellIds)
                link(spellId, entry.EntryId);
        for (TalentEntry const* talent : sTalentStore)
            if (talent)
                for (uint32 spellId : talent->RankID)
                    link(spellId, talent->TalentID | StandardTalentKey);
        return found;
    }();
    return sources;
}

uint32 SourceOf(uint32 spellId)
{
    auto const& sources = SpellSources();
    auto const itr = sources.find(spellId);
    return itr == sources.end() ? 0 : itr->second;
}

std::string TalentsJson(Player* player)
{
    std::string out = "[";
    bool first = true;
    for (auto const& known : GetAscensionKnownTalentEntries(player))
    {
        AscensionCompatData::CoATalentEntry const* entry = FindEntry(known.EntryId);
        if (!entry || !known.Rank || known.Rank > entry->SpellCount)
            continue;
        uint32 const spellId = entry->SpellIds[known.Rank - 1];
        out += Acore::StringFormat("{}{{\"key\":{},\"name\":\"{}\",\"icon\":{},\"rank\":{},\"maxRank\":{},"
            "\"classId\":{},\"class\":\"{}\",\"tree\":\"{}\",\"borrowed\":{},\"talent\":{},\"cost\":\"{}\"}}",
            first ? "" : ",", entry->EntryId, Escaped(SpellName(spellId)), SpellIcon(spellId), known.Rank,
            uint32(entry->SpellCount), uint32(entry->ClassId), Escaped(ClassName(entry->ClassId)),
            Escaped(entry->SpecId ? CoASpecName(entry->SpecId) : "Class"),
            entry->ClassId != player->getClass() ? "true" : "false", entry->Talent ? "true" : "false",
            entry->AECost ? "class" : entry->TECost ? "spec" : "free");
        first = false;
    }
    for (auto const& [talentId, rank] : StandardTalentsOf(player))
    {
        TalentEntry const* talent = sTalentStore.LookupEntry(talentId);
        TalentTabEntry const* tab = talent ? sTalentTabStore.LookupEntry(talent->TalentTab) : nullptr;
        if (!talent || !tab)
            continue;
        uint32 maxRank = 0;
        for (uint32 spellId : talent->RankID)
            maxRank += spellId ? 1 : 0;
        uint32 const spellId = talent->RankID[rank - 1];
        uint8 tabClass = 0;
        for (uint8 classId = 1; classId < 32; ++classId)
            if (tab->ClassMask & (1u << (classId - 1)))
            {
                tabClass = classId;
                break;
            }
        out += Acore::StringFormat("{}{{\"key\":{},\"name\":\"{}\",\"icon\":{},\"rank\":{},\"maxRank\":{},"
            "\"classId\":{},\"class\":\"{}\",\"tab\":{},\"row\":{},\"column\":{},\"borrowed\":{},\"talent\":true}}",
            first ? "" : ",", talentId | StandardTalentKey, Escaped(SpellName(spellId)), SpellIcon(spellId), rank,
            maxRank, uint32(tabClass), Escaped(ClassName(tabClass)), tab->tabpage, talent->Row, talent->Col,
            tabClass != player->getClass() ? "true" : "false");
        first = false;
    }
    return out + "]";
}

std::string GearJson(Player* player)
{
    std::string out = "[";
    bool first = true;
    for (uint8 slot = EQUIPMENT_SLOT_START; slot < EQUIPMENT_SLOT_END; ++slot)
    {
        Item* item = player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
        ItemTemplate const* proto = item ? item->GetTemplate() : nullptr;
        if (!proto)
            continue;
        out += Acore::StringFormat("{}{{\"slot\":{},\"id\":{},\"name\":\"{}\",\"level\":{},\"quality\":{},"
            "\"display\":{}}}", first ? "" : ",", uint32(slot), proto->ItemId, Escaped(proto->Name1),
            proto->ItemLevel, proto->Quality, proto->DisplayInfoID);
        first = false;
    }
    return out + "]";
}

std::string StatsJson(Player* player)
{
    float spellCrit = 0.0f;
    for (uint8 school = SPELL_SCHOOL_HOLY; school < MAX_SPELL_SCHOOL; ++school)
        spellCrit = std::max(spellCrit, player->GetFloatValue(PLAYER_SPELL_CRIT_PERCENTAGE1 + school));
    Powers const power = player->getPowerType();
    return Acore::StringFormat("{{\"strength\":{:.0f},\"agility\":{:.0f},\"stamina\":{:.0f},\"intellect\":{:.0f},"
        "\"spirit\":{:.0f},\"health\":{},\"power\":{},\"powerType\":{},\"attackPower\":{:.0f},"
        "\"rangedAttackPower\":{:.0f},\"spellPower\":{},\"healingPower\":{},\"meleeCrit\":{:.2f},"
        "\"rangedCrit\":{:.2f},\"spellCrit\":{:.2f},\"meleeHaste\":{:.2f},\"spellHaste\":{:.2f},\"meleeHit\":{:.2f},"
        "\"spellHit\":{:.2f},\"armor\":{},\"dodge\":{:.2f},\"parry\":{:.2f},\"block\":{:.2f},\"runSpeed\":{:.2f}}}",
        player->GetStat(STAT_STRENGTH), player->GetStat(STAT_AGILITY), player->GetStat(STAT_STAMINA),
        player->GetStat(STAT_INTELLECT), player->GetStat(STAT_SPIRIT), player->GetMaxHealth(),
        player->GetMaxPower(power), uint32(power), player->GetTotalAttackPowerValue(BASE_ATTACK),
        player->GetTotalAttackPowerValue(RANGED_ATTACK), player->SpellBaseDamageBonusDone(SPELL_SCHOOL_MASK_MAGIC),
        player->SpellBaseHealingBonusDone(SPELL_SCHOOL_MASK_ALL), player->GetFloatValue(PLAYER_CRIT_PERCENTAGE),
        player->GetFloatValue(PLAYER_RANGED_CRIT_PERCENTAGE), spellCrit, player->GetRatingBonusValue(CR_HASTE_MELEE),
        player->GetRatingBonusValue(CR_HASTE_SPELL), player->GetRatingBonusValue(CR_HIT_MELEE),
        player->GetRatingBonusValue(CR_HIT_SPELL), player->GetArmor(), player->GetFloatValue(PLAYER_DODGE_PERCENTAGE),
        player->GetFloatValue(PLAYER_PARRY_PERCENTAGE), player->GetFloatValue(PLAYER_BLOCK_PERCENTAGE),
        player->GetSpeed(MOVE_RUN));
}

std::string SourcesJson(Watched const& entry)
{
    std::string out = "{";
    bool first = true;
    auto add = [&](std::map<uint32, Tally> const& spells)
    {
        for (auto const& [spellId, tally] : spells)
            if (uint32 const source = SourceOf(spellId))
            {
                out += Acore::StringFormat("{}\"{}\":{}", first ? "" : ",", spellId, source);
                first = false;
            }
    };
    add(entry.SpellDamage);
    add(entry.SpellHealing);
    return out + "}";
}

void BuildSnapshot(Player* player, Watched& entry)
{
    entry.Snapshot = Acore::StringFormat("{{\"builtAt\":{},\"spec\":\"{}\",\"stats\":{},\"gear\":{},\"talents\":{}}}",
        NowSeconds(), Escaped(entry.Spec), StatsJson(player), GearJson(player), TalentsJson(player));
    entry.SnapshotAt = NowMs();
}

std::string QueryTallies(std::string const& sql)
{
    std::string out = "[";
    if (QueryResult result = CharacterDatabase.Query(sql))
    {
        bool first = true;
        do
        {
            Field* f = result->Fetch();
            uint32 const spellId = f[0].Get<uint32>();
            std::string const name = spellId ? SpellName(spellId) : "Melee";
            out += Acore::StringFormat("{}{{\"id\":{},\"name\":\"{}\",\"icon\":{},\"amount\":{},\"count\":{},"
                "\"source\":{}}}", first ? "" : ",", spellId, Escaped(name.empty() ? "?" : name), SpellIcon(spellId),
                f[1].Get<std::string>(), f[2].Get<std::string>(), SourceOf(spellId));
            first = false;
        } while (result->NextRow());
    }
    return out + "]";
}

std::string CharacterJson(uint32 guid)
{
    std::string snapshot;
    std::string live = "null";
    {
        std::lock_guard<std::mutex> lock(liveMutex);
        auto itr = watched.find(guid);
        if (itr != watched.end())
        {
            Watched& entry = itr->second;
            entry.WantedUntil = NowMs() + SnapshotInterestMs;
            snapshot = entry.Snapshot;
            live = Acore::StringFormat("{{\"session\":{{{}}},\"spells\":{},\"heals\":{},\"controls\":{},\"buffs\":{},"
                "\"sources\":{},\"runSpeed\":{:.2f}}}", TotalsJson(entry.Session),
                SpellsJson(entry.SpellDamage, 60), SpellsJson(entry.SpellHealing, 60),
                SpellsJson(entry.ControlBySpell, 30), SpellsJson(entry.BuffBySpell, 30), SourcesJson(entry),
                entry.RunSpeed);
        }
    }
    std::string const spells = QueryTallies(Acore::StringFormat("SELECT `spell_id`, `amount`, `hits` FROM "
        "`livemon_spell` WHERE `guid` = {} AND `kind` = 0 ORDER BY `amount` DESC LIMIT 60", guid));
    std::string const heals = QueryTallies(Acore::StringFormat("SELECT `spell_id`, `amount`, `hits` FROM "
        "`livemon_spell` WHERE `guid` = {} AND `kind` = 1 ORDER BY `amount` DESC LIMIT 60", guid));
    std::string timeline = "[";
    if (QueryResult result = CharacterDatabase.Query("SELECT `minute`, `damage`, `healing`, `combat_ms`, `level`, "
            "`build` FROM `livemon_minute` WHERE `guid` = {} ORDER BY `minute` DESC LIMIT 1440", guid))
    {
        bool first = true;
        do
        {
            Field* f = result->Fetch();
            timeline += Acore::StringFormat("{}[{},{},{},{},{},{}]", first ? "" : ",", f[0].Get<uint32>(),
                f[1].Get<uint64>(), f[2].Get<uint64>(), f[3].Get<uint64>(), f[4].Get<uint32>(), f[5].Get<uint32>());
            first = false;
        } while (result->NextRow());
    }
    timeline += "]";
    return Acore::StringFormat("{{\"guid\":{},\"snapshot\":{},\"live\":{},\"allTime\":{{\"spells\":{},\"heals\":{}}},"
        "\"minutes\":{}}}", guid, snapshot.empty() ? "null" : snapshot, live, spells, heals, timeline);
}

void FlushSpells(uint32 guid, std::map<uint32, Tally>& spells, uint8 kind, std::vector<std::string>& rows)
{
    for (auto const& [spellId, tally] : spells)
        rows.push_back(Acore::StringFormat("({}, {}, {}, {}, {})", guid, spellId, uint32(kind), tally.Amount,
            tally.Count));
    spells.clear();
}

void Flush()
{
    uint32 const minute = NowSeconds() / 60;
    std::vector<std::string> rows;
    std::vector<std::string> spellRows;
    std::vector<std::string> recordRows;
    {
        std::lock_guard<std::mutex> lock(liveMutex);
        for (auto& [guid, entry] : watched)
        {
            Totals const& t = entry.Minute;
            if (t.Damage || t.Healing || t.DamageTaken || t.CombatMs || t.MovedYards > 1.0 || t.Deaths || t.Kills)
                rows.push_back(Acore::StringFormat(
                    "({}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {})",
                    minute, guid, entry.Race, entry.Class, entry.Level, entry.Tree, entry.SubClass, entry.SubSpec,
                    t.Damage, t.Healing, t.DamageTaken, t.HealingTaken, t.ControlGivenMs, t.ControlTakenMs, t.Deaths,
                    t.Kills, t.CombatMs, uint64(t.MovedYards), entry.Build));
            entry.Minute = Totals();
            FlushSpells(guid, entry.MinuteSpellDamage, 0, spellRows);
            FlushSpells(guid, entry.MinuteSpellHealing, 1, spellRows);
        }
        for (auto& [key, record] : records)
            if (record.Dirty)
            {
                record.Dirty = false;
                recordRows.push_back(Acore::StringFormat("({}, {}, '{}', {}, {}, {}, {}, {:.2f}, {}, {})",
                    uint32(key.first), key.second, record.Name, uint32(record.Race), uint32(record.Class),
                    uint32(record.SubClass), uint32(record.Level), record.Value, record.Spell, record.At));
            }
    }
    auto join = [](std::vector<std::string> const& parts)
    {
        std::string values;
        for (std::string const& part : parts)
            values += (values.empty() ? "" : ",") + part;
        return values;
    };
    if (!rows.empty())
        CharacterDatabase.Execute("INSERT INTO `livemon_minute` (`minute`, `guid`, `race`, `class`, `level`, `tree`, "
            "`sub_class`, `sub_spec`, `damage`, `healing`, `damage_taken`, `healing_taken`, `control_given_ms`, "
            "`control_taken_ms`, `deaths`, `kills`, `combat_ms`, `moved_yards`, `build`) VALUES " + join(rows) +
            " ON DUPLICATE KEY UPDATE `damage` = `damage` + VALUES(`damage`), `healing` = `healing` + "
            "VALUES(`healing`), `damage_taken` = `damage_taken` + VALUES(`damage_taken`), `healing_taken` = "
            "`healing_taken` + VALUES(`healing_taken`), `control_given_ms` = `control_given_ms` + "
            "VALUES(`control_given_ms`), `control_taken_ms` = `control_taken_ms` + VALUES(`control_taken_ms`), "
            "`deaths` = `deaths` + VALUES(`deaths`), `kills` = `kills` + VALUES(`kills`), `combat_ms` = `combat_ms` + "
            "VALUES(`combat_ms`), `moved_yards` = `moved_yards` + VALUES(`moved_yards`)");
    if (!spellRows.empty())
        CharacterDatabase.Execute("INSERT INTO `livemon_spell` (`guid`, `spell_id`, `kind`, `amount`, `hits`) VALUES " +
            join(spellRows) + " ON DUPLICATE KEY UPDATE `amount` = `amount` + VALUES(`amount`), `hits` = `hits` + "
            "VALUES(`hits`)");
    if (!recordRows.empty())
        CharacterDatabase.Execute("REPLACE INTO `livemon_record` (`kind`, `guid`, `name`, `race`, `class`, "
            "`sub_class`, `level`, `value`, `spell_id`, `at`) VALUES " + join(recordRows));
}

void CreateTables()
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
        "`build` INT UNSIGNED NOT NULL DEFAULT 0, "
        "PRIMARY KEY (`minute`, `guid`), "
        "KEY `idx_combo` (`race`, `class`, `sub_class`, `tree`), "
        "KEY `idx_guid` (`guid`, `minute`) "
        ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4");
    if (!CharacterDatabase.Query("SELECT 1 FROM `information_schema`.`columns` WHERE `table_schema` = DATABASE() AND "
            "`table_name` = 'livemon_minute' AND `column_name` = 'build'"))
        CharacterDatabase.DirectExecute("ALTER TABLE `livemon_minute` ADD COLUMN `build` INT UNSIGNED NOT NULL "
            "DEFAULT 0, ADD KEY `idx_guid` (`guid`, `minute`)");
    CharacterDatabase.DirectExecute(
        "CREATE TABLE IF NOT EXISTS `livemon_spell` ( "
        "`guid` INT UNSIGNED NOT NULL, "
        "`spell_id` INT UNSIGNED NOT NULL, "
        "`kind` TINYINT UNSIGNED NOT NULL, "
        "`amount` BIGINT UNSIGNED NOT NULL DEFAULT 0, "
        "`hits` INT UNSIGNED NOT NULL DEFAULT 0, "
        "PRIMARY KEY (`guid`, `spell_id`, `kind`) "
        ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4");
    CharacterDatabase.DirectExecute(
        "CREATE TABLE IF NOT EXISTS `livemon_record` ( "
        "`kind` TINYINT UNSIGNED NOT NULL, "
        "`guid` INT UNSIGNED NOT NULL, "
        "`name` VARCHAR(32) NOT NULL DEFAULT '', "
        "`race` TINYINT UNSIGNED NOT NULL DEFAULT 0, "
        "`class` TINYINT UNSIGNED NOT NULL DEFAULT 0, "
        "`sub_class` TINYINT UNSIGNED NOT NULL DEFAULT 0, "
        "`level` TINYINT UNSIGNED NOT NULL DEFAULT 0, "
        "`value` DOUBLE NOT NULL DEFAULT 0, "
        "`spell_id` INT UNSIGNED NOT NULL DEFAULT 0, "
        "`at` INT UNSIGNED NOT NULL DEFAULT 0, "
        "PRIMARY KEY (`kind`, `guid`) "
        ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4");
}

void LoadRecords()
{
    QueryResult result = CharacterDatabase.Query("SELECT `kind`, `guid`, `name`, `race`, `class`, `sub_class`, "
        "`level`, `value`, `spell_id`, `at` FROM `livemon_record`");
    std::lock_guard<std::mutex> lock(liveMutex);
    if (result)
        do
        {
            Field* f = result->Fetch();
            if (f[0].Get<uint8>() < RecordKinds)
                records[{ f[0].Get<uint8>(), f[1].Get<uint32>() }] = { f[7].Get<double>(), f[8].Get<uint32>(),
                    f[9].Get<uint32>(), f[2].Get<std::string>(), f[3].Get<uint8>(), f[4].Get<uint8>(),
                    f[5].Get<uint8>(), f[6].Get<uint8>(), false };
        } while (result->NextRow());
}

std::string Reset(bool onlyGone)
{
    if (onlyGone)
    {
        for (char const* table : { "livemon_minute", "livemon_spell", "livemon_record" })
            CharacterDatabase.DirectExecute(Acore::StringFormat("DELETE FROM `{}` WHERE `guid` NOT IN "
                "(SELECT `guid` FROM `characters`)", table));
        std::lock_guard<std::mutex> lock(liveMutex);
        std::erase_if(records, [](auto const& item)
        {
            return !watched.contains(item.first.second) || !watched.at(item.first.second).Online;
        });
        std::erase_if(watched, [](auto const& item) { return !item.second.Online; });
        return "{\"reset\":\"gone\"}";
    }
    for (char const* table : { "livemon_minute", "livemon_spell", "livemon_record" })
        CharacterDatabase.DirectExecute(Acore::StringFormat("TRUNCATE TABLE `{}`", table));
    std::lock_guard<std::mutex> lock(liveMutex);
    records.clear();
    controls.clear();
    buffs.clear();
    std::erase_if(watched, [](auto const& item) { return !item.second.Online; });
    for (auto& [guid, entry] : watched)
    {
        entry.Session = Totals();
        entry.Minute = Totals();
        entry.SpellDamage.clear();
        entry.SpellHealing.clear();
        entry.MinuteSpellDamage.clear();
        entry.MinuteSpellHealing.clear();
        entry.ControlBySpell.clear();
        entry.BuffBySpell.clear();
        entry.FightStart = 0;
        entry.FightDamage = 0;
        entry.FightHealing = 0;
    }
    return "{\"reset\":\"all\"}";
}

std::string Respond(std::string const& request)
{
    std::istringstream line(request);
    std::string method;
    std::string path;
    line >> method >> path;
    auto number = [&path](char const* key, uint32 fallback)
    {
        std::size_t const at = path.find(key);
        return at == std::string::npos ? fallback : uint32(std::strtoul(path.c_str() + at + std::strlen(key), nullptr,
            10));
    };
    std::string body;
    if (path.rfind("/live", 0) == 0)
        body = LiveJson();
    else if (path.rfind("/history", 0) == 0)
        body = HistoryJson(std::max<uint32>(1, number("minutes=", 60)));
    else if (path.rfind("/records", 0) == 0)
        body = RecordsJson();
    else if (path.rfind("/reset", 0) == 0)
        body = Reset(path.find("scope=gone") != std::string::npos);
    else if (path.rfind("/character", 0) == 0)
        body = CharacterJson(number("guid=", 0));
    else
        body = "{\"livemon\":2,\"endpoints\":[\"/live\",\"/history?minutes=60\",\"/records\",\"/character?guid=1\"]}";
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

void AddSpell(Watched& entry, uint32 spellId, uint64 amount, bool healing)
{
    for (auto* spells : healing ? std::array{ &entry.SpellHealing, &entry.MinuteSpellHealing } :
             std::array{ &entry.SpellDamage, &entry.MinuteSpellDamage })
    {
        Tally& tally = (*spells)[spellId];
        tally.Amount += amount;
        ++tally.Count;
    }
}

void AddUptime(std::map<uint32, Tally>& bySpell, uint32 spellId, uint64 lasted)
{
    Tally& tally = bySpell[spellId];
    tally.Amount += lasted;
    ++tally.Count;
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
        startedAt = NowSeconds();
        if (!settings.Enabled || serving.exchange(true))
            return;
        LoadSpecNames();
        CreateTables();
        LoadRecords();
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
        std::string const spec = SpecOf(player);
        {
            std::lock_guard<std::mutex> lock(liveMutex);
            Watched& entry = Track(player);
            entry.Spec = spec;
            Refresh(player, entry, SampleTick);
        }
        ApplySubclassChoices();
    }

    void OnPlayerLogout(Player* player) override
    {
        std::lock_guard<std::mutex> lock(liveMutex);
        if (auto itr = watched.find(player->GetGUID().GetCounter()); itr != watched.end())
        {
            EndFight(player, itr->second);
            itr->second.Online = false;
        }
    }

    void OnPlayerUpdate(Player* player, uint32 diff) override
    {
        if (!settings.Enabled)
            return;
        bool rebuild = false;
        bool snapshot = false;
        {
            std::lock_guard<std::mutex> lock(liveMutex);
            Watched& entry = Track(player);
            entry.Tick += diff;
            entry.BuildTick += diff;
            if (entry.Tick < SampleTick)
                return;
            uint32 const elapsed = entry.Tick;
            entry.Tick = 0;
            Refresh(player, entry, elapsed);
            if (entry.BuildTick >= BuildRefreshMs)
            {
                entry.BuildTick = 0;
                rebuild = true;
            }
            snapshot = entry.WantedUntil > NowMs() && NowMs() - entry.SnapshotAt >= SnapshotRefreshMs;
        }
        if (!rebuild && !snapshot)
            return;
        uint32 const build = rebuild ? BuildHash(player) : 0;
        std::string const spec = SpecOf(player);
        std::lock_guard<std::mutex> lock(liveMutex);
        Watched& entry = Track(player);
        entry.Spec = spec;
        if (rebuild)
            entry.Build = build;
        if (snapshot)
            BuildSnapshot(player, entry);
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
            entry.FightDamage += damage;
            NoteRecord(dealer, RecordHit, damage, entry.LastSpell, entry.SubClass);
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
            entry.FightHealing += gain;
            NoteRecord(giver, RecordHeal, gain, entry.LastHeal, entry.SubClass);
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
        AddSpell(entry, spellId, amount, healing);
        (healing ? entry.LastHeal : entry.LastSpell) = spellId;
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
        if (!settings.Enabled || !unit || !aura)
            return;
        bool const control = IsControl(aura->GetSpellInfo());
        if (!control && !IsOfferedBuff(aura, unit))
            return;
        Player* caster = Controller(aura->GetCaster());
        Player* target = unit->ToPlayer();
        if (control ? ((!caster && !target) || caster == target) : !caster)
            return;
        AuraKey const key = { unit->GetGUID().GetCounter(), aura->GetId(), aura->GetCasterGUID().GetCounter() };
        std::lock_guard<std::mutex> lock(liveMutex);
        (control ? controls : buffs)[key] = NowMs();
        if (caster && control)
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
        if (!aura)
            return;
        bool const control = IsControl(aura->GetSpellInfo());
        if (!control && !IsOfferedBuff(aura, unit))
            return;
        Player* caster = Controller(aura->GetCaster());
        Player* target = unit->ToPlayer();
        std::lock_guard<std::mutex> lock(liveMutex);
        std::map<AuraKey, uint32>& started = control ? controls : buffs;
        auto itr = started.find({ unit->GetGUID().GetCounter(), aura->GetId(), aura->GetCasterGUID().GetCounter() });
        if (itr == started.end())
            return;
        uint64 const lasted = NowMs() - itr->second;
        started.erase(itr);
        if (!control)
        {
            if (caster)
            {
                Watched& entry = Track(caster);
                entry.Session.BuffGivenMs += lasted;
                entry.Minute.BuffGivenMs += lasted;
                AddUptime(entry.BuffBySpell, aura->GetId(), lasted);
            }
            return;
        }
        if (caster && caster != target)
        {
            Watched& entry = Track(caster);
            entry.Session.ControlGivenMs += lasted;
            entry.Minute.ControlGivenMs += lasted;
            AddUptime(entry.ControlBySpell, aura->GetId(), lasted);
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
