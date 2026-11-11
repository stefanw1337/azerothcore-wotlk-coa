/*
 * mod-skinshifter - tokens: Soul Tokens for a Possessor, Skin Tokens for a Skinshifter (the same currency).
 *
 * Every Skinshifter.Tokens.KillsPerToken kills that give experience (grey creatures only with Tokens.GreyKills; totems and pets never)
 * make one token, up to Skinshifter.Tokens.Cap. The token keeps the toughest creature of those kills (a boss over
 * a rare elite over an elite over a rare over a normal one, then the higher level; on a tie the later kill): the counter in
 * the SkinCollection addon (a ring of holes: 20 for a Possessor, 10 of 2 tokens for a Skinshifter) shows that
 * creature in the token's hole, in the colour of the specialization. Rares, elites and bosses
 * will cost tokens to take over (level 20, not made yet): Skinshifter.Tokens.Cost.Rare / Elite / Boss, which are
 * also the counter's milestones (it burns brighter at each).
 * Stored in `skinshifter_tokens` (guid, kills, tokens), `skinshifter_token_mob` (guid, token, entry, display,
 * name) and `skinshifter_token_pack` (guid, entry, display, name, score: the toughest kill toward the next token). Messages (hidden addon whispers, prefix "SKIN"):
 *   client -> server  TOKENS (send the state)
 *   server -> client  TOKENS;<tokens>;<kills>;<kills per token>;<cap>;<rare>;<elite>;<boss>;<flavour>;<spec>,
 *                     TOKENMOB;<token>;<entry>;<display>;<name> (one per token, from 1), TOKENEND,
 *                     TOKENNEW;<token> (a token just earned)
 *   flavour: possessor or skinshifter (the ring's picture); spec: 0 none, 1-4 (its colour).
 * GM: .skin tokens <count> sets the count (to try the counter; the new tokens keep your target, if any).
 *
 * This code is released under the GNU AGPL v3 like AzerothCore.
 */

#include "Chat.h"
#include "Config.h"
#include "Creature.h"
#include "DatabaseEnv.h"
#include "Field.h"
#include "Log.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "QueryResult.h"
#include "ScriptMgr.h"
#include "SkinshifterSpells.h"
#include "StringFormat.h"
#include "WorldPacket.h"
#include "WorldSession.h"

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace
{
using namespace SkinshifterSpells;

constexpr uint8 SkinshifterClassId = 33;
constexpr uint32 CounterSlots = 20;

struct TokenMob
{
    uint32 entry = 0;
    uint32 display = 0;
    std::string name;
};

struct TokenState
{
    uint32 kills = 0;               // toward the next token
    TokenMob toughest;              // the toughest of those kills: the next token's creature
    uint32 toughestScore = 0;
    std::vector<TokenMob> tokens;   // one per token
};

// How tough a kill was: its rank first (a boss, a rare elite, an elite, a rare, a normal one), then its level.
uint32 Toughness(Creature const* creature)
{
    uint32 rank = 0;
    if (creature->isWorldBoss() || creature->IsDungeonBoss())
        rank = 4;
    else
        switch (creature->GetCreatureTemplate()->rank)
        {
            case CREATURE_ELITE_RAREELITE: rank = 3; break;
            case CREATURE_ELITE_ELITE:     rank = 2; break;
            case CREATURE_ELITE_RARE:      rank = 1; break;
            default:                       rank = 0; break;
        }
    return 1 + rank * 1000 + creature->GetLevel();
}
std::unordered_map<uint32, TokenState> States;

struct TokenConfig
{
    uint32 killsPerToken = 10;
    bool greyKills = false;  // grey creatures count too
    uint32 cap = 20;
    uint32 rare = 5, elite = 10, boss = 20;
} Cfg;

bool IsSkinshifter(Player const* player)
{
    return player && player->getClass() == SkinshifterClassId;
}

void SendLine(Player* player, std::string const& line)
{
    WorldPacket packet;
    ChatHandler::BuildChatPacket(packet, CHAT_MSG_WHISPER, LANG_ADDON, player, player, "SKIN\t" + line);
    player->SendDirectMessage(&packet);
}

// The ring's picture is the flavour's; its colour the specialization's.
char const* FlavourOf(Player const* player)
{
    return IsPossessorRace(player->getRace()) ? "possessor" : "skinshifter";
}

// No ';' in a message field.
std::string Clean(std::string text)
{
    for (char& c : text)
        if (c == ';' || c == '\t' || c == '\n')
            c = ' ';
    return text;
}

void SendTokens(Player* player)
{
    TokenState const& state = States[player->GetGUID().GetCounter()];
    SendLine(player, Acore::StringFormat("TOKENS;{};{};{};{};{};{};{};{};{}", state.tokens.size(), state.kills,
        Cfg.killsPerToken, Cfg.cap, Cfg.rare, Cfg.elite, Cfg.boss, FlavourOf(player), uint32(CurrentSpec(player))));
    for (std::size_t index = 0; index < state.tokens.size(); ++index)
    {
        TokenMob const& mob = state.tokens[index];
        SendLine(player, Acore::StringFormat("TOKENMOB;{};{};{};{}", index + 1, mob.entry, mob.display, Clean(mob.name)));
    }
    SendLine(player, "TOKENEND");
}

void Save(uint32 guid, TokenState const& state)
{
    CharacterDatabase.Execute("REPLACE INTO `skinshifter_tokens` (`guid`, `kills`, `tokens`) VALUES ({}, {}, {})", guid,
        state.kills, uint32(state.tokens.size()));
}

void SavePack(uint32 guid, TokenState const& state)
{
    std::string name = state.toughest.name;
    CharacterDatabase.EscapeString(name);
    CharacterDatabase.Execute("REPLACE INTO `skinshifter_token_pack` (`guid`, `entry`, `display`, `name`, `score`) "
        "VALUES ({}, {}, {}, '{}', {})", guid, state.toughest.entry, state.toughest.display, name, state.toughestScore);
}

void SaveMob(uint32 guid, uint32 token, TokenMob const& mob)
{
    std::string name = mob.name;
    CharacterDatabase.EscapeString(name);
    CharacterDatabase.Execute("REPLACE INTO `skinshifter_token_mob` (`guid`, `token`, `entry`, `display`, `name`) "
        "VALUES ({}, {}, {}, {}, '{}')", guid, token, mob.entry, mob.display, name);
}

void Load(Player* player)
{
    uint32 const guid = player->GetGUID().GetCounter();
    TokenState state;
    uint32 count = 0;
    if (QueryResult result = CharacterDatabase.Query("SELECT `kills`, `tokens` FROM `skinshifter_tokens` WHERE `guid` = {}", guid))
    {
        state.kills = result->Fetch()[0].Get<uint32>();
        count = result->Fetch()[1].Get<uint32>();
    }
    state.tokens.resize(std::min(count, Cfg.cap));
    if (QueryResult result = CharacterDatabase.Query("SELECT `entry`, `display`, `name`, `score` FROM "
        "`skinshifter_token_pack` WHERE `guid` = {}", guid))
    {
        Field const* fields = result->Fetch();
        state.toughest = { fields[0].Get<uint32>(), fields[1].Get<uint32>(), fields[2].Get<std::string>() };
        state.toughestScore = fields[3].Get<uint32>();
    }
    if (QueryResult result = CharacterDatabase.Query("SELECT `token`, `entry`, `display`, `name` FROM `skinshifter_token_mob` "
        "WHERE `guid` = {}", guid))
    {
        do
        {
            Field const* fields = result->Fetch();
            uint32 const token = fields[0].Get<uint32>();
            if (token >= 1 && token <= state.tokens.size())
                state.tokens[token - 1] = { fields[1].Get<uint32>(), fields[2].Get<uint32>(), fields[3].Get<std::string>() };
        } while (result->NextRow());
    }
    States[guid] = std::move(state);
}

void SetCount(Player* player, uint32 count, Creature const* sample)
{
    uint32 const guid = player->GetGUID().GetCounter();
    TokenState& state = States[guid];
    count = std::min(count, Cfg.cap);
    uint32 const before = uint32(state.tokens.size());
    state.tokens.resize(count);
    for (uint32 token = before + 1; token <= count; ++token)
    {
        if (sample)
            state.tokens[token - 1] = { sample->GetEntry(), sample->GetNativeDisplayId(), sample->GetName() };
        SaveMob(guid, token, state.tokens[token - 1]);
    }
    if (count < before)
        CharacterDatabase.Execute("DELETE FROM `skinshifter_token_mob` WHERE `guid` = {} AND `token` > {}", guid, count);
    Save(guid, state);
    SendTokens(player);
}

void Killed(Player* player, Creature* creature)
{
    if (!IsSkinshifter(player) || !creature)
        return;
    // a kill that gives experience; with Skinshifter.Tokens.GreyKills also a grey one (not a pet, totem or critter)
    bool const counts = player->isHonorOrXPTarget(creature) || (Cfg.greyKills && !creature->IsPet() &&
        !creature->IsTotem() && !creature->IsCritter() && !creature->IsControlledByPlayer() &&
        !(creature->GetCreatureTemplate()->flags_extra & CREATURE_FLAG_EXTRA_NO_XP));
    if (!counts)
        return;
    // a taken-over creature's kills, and its own death, do not fill the counter (else a free takeover farms tokens)
    if (InTakeover(player) || IsTakeoverCreature(creature))
        return;
    uint32 const guid = player->GetGUID().GetCounter();
    TokenState& state = States[guid];
    if (state.tokens.size() >= Cfg.cap)
        return;  // the counter is full: kills wait until tokens are spent
    uint32 const score = Toughness(creature);
    if (score >= state.toughestScore)
    {
        state.toughest = { creature->GetEntry(), creature->GetNativeDisplayId(), creature->GetName() };
        state.toughestScore = score;
    }
    if (++state.kills < Cfg.killsPerToken)
    {
        Save(guid, state);
        if (state.toughestScore == score)
            SavePack(guid, state);
        return;
    }
    TokenMob const mob = state.toughest;
    state.kills = 0;
    state.toughest = {};
    state.toughestScore = 0;
    SavePack(guid, state);
    state.tokens.push_back(mob);
    uint32 const token = uint32(state.tokens.size());
    SaveMob(guid, token, mob);
    Save(guid, state);
    SendTokens(player);
    SendLine(player, Acore::StringFormat("TOKENNEW;{}", token));
    LOG_INFO("module", "Skinshifter: {} earns token {} of {} ({}).", player->GetName(), token, Cfg.cap, mob.name);
}

class SkinshifterTokenPlayerScript : public PlayerScript
{
public:
    SkinshifterTokenPlayerScript() : PlayerScript("SkinshifterTokenPlayerScript", { PLAYERHOOK_ON_LOGIN,
        PLAYERHOOK_ON_LOGOUT, PLAYERHOOK_ON_DELETE, PLAYERHOOK_ON_CREATURE_KILL, PLAYERHOOK_ON_CREATURE_KILLED_BY_PET }) { }

    void OnPlayerLogin(Player* player) override
    {
        if (IsSkinshifter(player))
            Load(player);
    }

    void OnPlayerLogout(Player* player) override
    {
        States.erase(player->GetGUID().GetCounter());
    }

    void OnPlayerDelete(ObjectGuid guid, uint32 /*accountId*/) override
    {
        States.erase(guid.GetCounter());
        CharacterDatabase.Execute("DELETE FROM `skinshifter_tokens` WHERE `guid` = {}", guid.GetCounter());
        CharacterDatabase.Execute("DELETE FROM `skinshifter_token_mob` WHERE `guid` = {}", guid.GetCounter());
        CharacterDatabase.Execute("DELETE FROM `skinshifter_token_pack` WHERE `guid` = {}", guid.GetCounter());
    }

    void OnPlayerCreatureKill(Player* killer, Creature* killed) override
    {
        Killed(killer, killed);
    }

    void OnPlayerCreatureKilledByPet(Player* owner, Creature* killed) override
    {
        Killed(owner, killed);
    }
};

// A kill by a summoned creature (Zombie Horde and the like: not a pet or totem, which the player hook already counts) counts
// for the player who summoned it.
class SkinshifterTokenUnitScript : public UnitScript
{
public:
    SkinshifterTokenUnitScript() : UnitScript("SkinshifterTokenUnitScript", true, { UNITHOOK_ON_UNIT_DEATH }) { }

    void OnUnitDeath(Unit* unit, Unit* killer) override
    {
        Creature* killed = unit ? unit->ToCreature() : nullptr;
        if (!killed || !killer || killer->IsPlayer() || killer->IsPet() || killer->IsTotem())
            return;
        if (Player* owner = killer->GetCharmerOrOwnerPlayerOrPlayerItself())
            Killed(owner, killed);
    }
};

class SkinshifterTokenWorldScript : public WorldScript
{
public:
    SkinshifterTokenWorldScript() : WorldScript("SkinshifterTokenWorldScript", { WORLDHOOK_ON_AFTER_CONFIG_LOAD,
        WORLDHOOK_ON_STARTUP }) { }

    void OnAfterConfigLoad(bool /*reload*/) override
    {
        Cfg.killsPerToken = std::max<uint32>(1, sConfigMgr->GetOption<uint32>("Skinshifter.Tokens.KillsPerToken", 10));
        Cfg.greyKills = sConfigMgr->GetOption<bool>("Skinshifter.Tokens.GreyKills", false);
        uint32 cap = std::max<uint32>(CounterSlots, sConfigMgr->GetOption<uint32>("Skinshifter.Tokens.Cap", 20));
        if (cap % CounterSlots)
        {
            uint32 const rounded = (cap / CounterSlots + 1) * CounterSlots;
            LOG_WARN("module", "Skinshifter.Tokens.Cap {} is not a multiple of {} (the counter's holes); {} is used.",
                cap, CounterSlots, rounded);
            cap = rounded;
        }
        Cfg.cap = cap;
        Cfg.rare = sConfigMgr->GetOption<uint32>("Skinshifter.Tokens.Cost.Rare", 5);
        Cfg.elite = sConfigMgr->GetOption<uint32>("Skinshifter.Tokens.Cost.Elite", 10);
        Cfg.boss = sConfigMgr->GetOption<uint32>("Skinshifter.Tokens.Cost.Boss", 20);
    }

    void OnStartup() override
    {
        CharacterDatabase.DirectExecute("CREATE TABLE IF NOT EXISTS `skinshifter_tokens` (`guid` INT UNSIGNED NOT NULL, "
            "`kills` INT UNSIGNED NOT NULL DEFAULT 0, `tokens` INT UNSIGNED NOT NULL DEFAULT 0, PRIMARY KEY (`guid`)) "
            "ENGINE=InnoDB DEFAULT CHARSET=utf8mb4");
        CharacterDatabase.DirectExecute("CREATE TABLE IF NOT EXISTS `skinshifter_token_mob` (`guid` INT UNSIGNED NOT NULL, "
            "`token` SMALLINT UNSIGNED NOT NULL, `entry` INT UNSIGNED NOT NULL DEFAULT 0, `display` INT UNSIGNED NOT NULL "
            "DEFAULT 0, `name` VARCHAR(100) NOT NULL DEFAULT '', PRIMARY KEY (`guid`, `token`)) ENGINE=InnoDB DEFAULT "
            "CHARSET=utf8mb4");
        CharacterDatabase.DirectExecute("CREATE TABLE IF NOT EXISTS `skinshifter_token_pack` (`guid` INT UNSIGNED NOT NULL, "
            "`entry` INT UNSIGNED NOT NULL DEFAULT 0, `display` INT UNSIGNED NOT NULL DEFAULT 0, `name` VARCHAR(100) NOT "
            "NULL DEFAULT '', `score` INT UNSIGNED NOT NULL DEFAULT 0, PRIMARY KEY (`guid`)) ENGINE=InnoDB DEFAULT "
            "CHARSET=utf8mb4");
        LOG_INFO("module", "Skinshifter: tokens: 1 per {} kills, up to {}; rare {}, elite {}, boss {}.", Cfg.killsPerToken,
            Cfg.cap, Cfg.rare, Cfg.elite, Cfg.boss);
    }
};
}

namespace SkinshifterSpells
{
bool HandleTokenMessage(Player* player, std::string_view body)
{
    if (body != "TOKENS")
        return false;
    if (IsSkinshifter(player))
    {
        if (!States.count(player->GetGUID().GetCounter()))
            Load(player);
        SendTokens(player);
    }
    return true;
}

uint32 TokenCount(Player* player)
{
    if (!IsSkinshifter(player))
        return 0;
    if (!States.count(player->GetGUID().GetCounter()))
        Load(player);
    return uint32(States[player->GetGUID().GetCounter()].tokens.size());
}

uint32 TokenCostOf(uint32 rank, bool boss)
{
    if (boss || rank == CREATURE_ELITE_WORLDBOSS)
        return Cfg.boss;
    if (rank == CREATURE_ELITE_ELITE || rank == CREATURE_ELITE_RAREELITE)
        return Cfg.elite;
    if (rank == CREATURE_ELITE_RARE)
        return Cfg.rare;
    return 0;
}

// The newest tokens go first.
bool SpendTokens(Player* player, uint32 count)
{
    if (!count)
        return true;
    uint32 const have = TokenCount(player);
    if (have < count)
        return false;
    SetCount(player, have - count, nullptr);
    return true;
}

void EarnToken(Player* player, Creature const* creature)
{
    uint32 const have = TokenCount(player);
    if (!IsSkinshifter(player) || have >= Cfg.cap)
        return;
    SetCount(player, have + 1, creature);
    SendLine(player, Acore::StringFormat("TOKENNEW;{}", have + 1));
}

bool HandleTokensCommand(ChatHandler* handler, uint32 count)
{
    Player* player = handler->GetSession() ? handler->GetSession()->GetPlayer() : nullptr;
    if (!IsSkinshifter(player))
    {
        handler->SendSysMessage("Tokens are the Skinshifter's.");
        return true;
    }
    if (!States.count(player->GetGUID().GetCounter()))
        Load(player);
    SetCount(player, count, ObjectAccessor::GetCreature(*player, player->GetTarget()));
    handler->PSendSysMessage("Tokens set to {} of {}.", std::min(count, Cfg.cap), Cfg.cap);
    return true;
}
}

void AddSC_skinshifter_tokens()
{
    new SkinshifterTokenPlayerScript();
    new SkinshifterTokenWorldScript();
    new SkinshifterTokenUnitScript();
}
