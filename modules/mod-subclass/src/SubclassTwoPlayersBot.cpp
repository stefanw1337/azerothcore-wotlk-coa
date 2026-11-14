/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license.
 */

#include "CharacterCache.h"
#include "Config.h"
#include "DatabaseEnv.h"
#include "MotionMaster.h"
#include "Player.h"
#include "Random.h"
#include "SpellAuraEffects.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "StringFormat.h"
#include "WorldSession.h"
#include <algorithm>
#include <array>
#include <iterator>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#if __has_include("Playerbots.h") && __has_include("PlayerbotMgr.h") && __has_include("PlayerbotAIConfig.h")
#include "PlayerbotAIConfig.h"
#include "PlayerbotMgr.h"
#include "Playerbots.h"
#define SUBCLASS_WITH_SQUID_PLAYERBOTS 1
#endif

bool TwoPlayersHeadIsBot(Player const* head)
{
    return head && head->GetSession() && head->GetSession()->IsBot();
}

#ifdef SUBCLASS_WITH_SQUID_PLAYERBOTS

namespace
{
std::string QuestingStrategy()
{
    return sPlayerbotAIConfig.enableNewRpgStrategy ? "new rpg" : "rpg";
}

struct Ride
{
    uint32 NextTry = 0;
    bool Held = false;
    float HeldX = 0.0f;
    float HeldY = 0.0f;
};

constexpr uint32 RideRetryMs = 10000;
constexpr uint32 FirstHotsMountSpell = 9860001;
constexpr uint32 LastHotsMountSpell = 9860999;

std::mutex& rideMutex = *new std::mutex;
std::unordered_map<uint32, Ride>& rides = *new std::unordered_map<uint32, Ride>;

std::array<char const*, 4> const MountLines = {
    "Long walk. {0} carry the ride, {1} enjoy the view.",
    "We ride! Well. Ride rides. {0} walks.",
    "{0} legs, ride on top. Fair deal, {1}?",
    "Up you go. Heavy, but faster than {1}'s chatter.",
};
std::array<char const*, 3> const DropLines = {
    "drops the {0} and raises both fists.",
    "sets the {0} down, gently. Then charges.",
    "tosses the {0} aside. Fight first, ride later.",
};

float MountDistance()
{
    return sConfigMgr->GetOption<float>("Subclass.TwoPlayers.Bot.MountDistance", 100.0f);
}

WorldPosition const* GoalOf(PlayerbotAI* brain)
{
    NewRpgInfo& info = brain->rpgInfo;
    if (auto const* quest = std::get_if<NewRpgInfo::DoQuest>(&info.data))
        return &quest->pos;
    if (auto const* grind = std::get_if<NewRpgInfo::GoGrind>(&info.data))
        return &grind->pos;
    if (auto const* camp = std::get_if<NewRpgInfo::GoCamp>(&info.data))
        return &camp->pos;
    if (auto const* city = std::get_if<NewRpgInfo::GoCity>(&info.data))
        return &city->pos;
    return nullptr;
}

float GoalDistance(Player* body, WorldPosition const* goal)
{
    if (!goal || goal->GetMapId() != body->GetMapId() || (!goal->GetPositionX() && !goal->GetPositionY()))
        return 0.0f;
    return body->GetExactDist2d(goal->GetPositionX(), goal->GetPositionY());
}

void AddMountsOf(Player* head, std::vector<uint32>& mounts)
{
    for (auto const& [spellId, spell] : head->GetSpellMap())
    {
        if (spell->State == PLAYERSPELL_REMOVED || !spell->Active)
            continue;
        SpellInfo const* info = sSpellMgr->GetSpellInfo(spellId);
        if (info && info->HasAura(SPELL_AURA_MOUNTED) &&
            std::find(mounts.begin(), mounts.end(), spellId) == mounts.end())
            mounts.push_back(spellId);
    }
}

std::string MountName(Player* body)
{
    Unit::AuraEffectList const& mounts = body->GetAuraEffectsByType(SPELL_AURA_MOUNTED);
    return mounts.empty() ? "mount" : mounts.front()->GetSpellInfo()->SpellName[LOCALE_enUS];
}

bool MountUp(Player* body, Player* rider)
{
    std::vector<uint32> mounts;
    AddMountsOf(body, mounts);
    AddMountsOf(rider, mounts);
    if (sConfigMgr->GetOption<bool>("Subclass.TwoPlayers.Bot.PreferHotsMounts", true))
    {
        std::vector<uint32> hots;
        std::copy_if(mounts.begin(), mounts.end(), std::back_inserter(hots),
            [](uint32 spellId) { return spellId >= FirstHotsMountSpell && spellId <= LastHotsMountSpell; });
        if (!hots.empty())
            mounts = hots;
    }
    if (mounts.empty() || body->IsNonMeleeSpellCast(false))
        return false;
    SpellInfo const* mount = sSpellMgr->GetSpellInfo(mounts[urand(0, mounts.size() - 1)]);
    body->StopMoving();
    body->GetMotionMaster()->Clear();
    if (body->CastSpell(body, mount, TRIGGERED_NONE) != SPELL_CAST_OK)
        return false;
    body->TextEmote(std::string("hoists the ") + mount->SpellName[LOCALE_enUS] + " onto his shoulders.");
    body->Say(Acore::StringFormat(MountLines[urand(0, MountLines.size() - 1)], body->GetName(), rider->GetName()),
        LANG_UNIVERSAL);
    return true;
}

void GetDown(Player* body)
{
    body->TextEmote(Acore::StringFormat(DropLines[urand(0, DropLines.size() - 1)], MountName(body)));
    body->RemoveAurasByType(SPELL_AURA_MOUNTED);
}

bool Fighting(Player* body, PlayerbotAI* brain)
{
    return body->IsInCombat() || brain->GetState() == BOT_STATE_COMBAT;
}

void LinkForOneLogin(uint32 headAccount, uint32 masterAccount, bool link)
{
    if (link)
        PlayerbotsDatabase.DirectExecute("INSERT IGNORE INTO playerbots_account_links (account_id, linked_account_id) "
            "VALUES ({}, {})", headAccount, masterAccount);
    else
        PlayerbotsDatabase.DirectExecute("DELETE FROM playerbots_account_links WHERE account_id = {} AND "
            "linked_account_id = {}", headAccount, masterAccount);
}
}

bool TwoPlayersBotsAvailable()
{
    return true;
}

bool TwoPlayersCallBot(Player* master, uint32 headGuid)
{
    PlayerbotMgr* manager = GET_PLAYERBOT_MGR(master);
    if (!manager)
        return false;
    ObjectGuid const head = ObjectGuid::Create<HighGuid::Player>(headGuid);
    uint32 const headAccount = sCharacterCache->GetCharacterAccountIdByGuid(head);
    uint32 const masterAccount = master->GetSession()->GetAccountId();
    if (!headAccount)
        return false;
    bool const alreadyLinked = headAccount == masterAccount || manager->IsAccountLinked(headAccount, masterAccount);
    if (!alreadyLinked)
        LinkForOneLogin(headAccount, masterAccount, true);
    manager->AddPlayerBot(head, masterAccount);
    if (!alreadyLinked)
        LinkForOneLogin(headAccount, masterAccount, false);
    return true;
}

void TwoPlayersSteerBotBody(Player* body, bool roam)
{
    PlayerbotAI* brain = GET_PLAYERBOT_AI(body);
    if (!brain)
        return;
    std::string const questing = QuestingStrategy();
    bool const roaming = brain->HasStrategy("grind", BOT_STATE_NON_COMBAT) &&
        brain->HasStrategy(questing, BOT_STATE_NON_COMBAT);
    bool const following = brain->HasStrategy("follow", BOT_STATE_NON_COMBAT);
    bool const staying = brain->HasStrategy("stay", BOT_STATE_NON_COMBAT);
    if (roam)
    {
        if (!roaming || following || staying)
            brain->ChangeStrategy("-follow,-stay,+grind,+" + questing, BOT_STATE_NON_COMBAT);
        return;
    }
    if (staying && !roaming && !following)
        return;
    brain->ChangeStrategy("-follow,-grind,-" + questing + ",+stay", BOT_STATE_NON_COMBAT);
    body->StopMoving();
    body->GetMotionMaster()->Clear();
}

void TwoPlayersKeepBotBodyRoaming(Player* body)
{
    PlayerbotAI* brain = GET_PLAYERBOT_AI(body);
    if (!brain)
        return;
    std::string const questing = QuestingStrategy();
    if (brain->HasStrategy("stay", BOT_STATE_NON_COMBAT))
    {
        if (brain->HasStrategy("grind", BOT_STATE_NON_COMBAT) || brain->HasStrategy(questing, BOT_STATE_NON_COMBAT))
        {
            brain->ChangeStrategy("-grind,-" + questing, BOT_STATE_NON_COMBAT);
            brain->rpgInfo.ChangeToIdle();
            body->StopMoving();
            body->GetMotionMaster()->Clear();
        }
        return;
    }
    std::string change;
    if (brain->HasStrategy("follow", BOT_STATE_NON_COMBAT))
        change += ",-follow";
    for (std::string const& wanted : { std::string("grind"), questing, std::string("loot"), std::string("food") })
        if (!brain->HasStrategy(wanted, BOT_STATE_NON_COMBAT))
            change += ",+" + wanted;
    if (!change.empty())
        brain->ChangeStrategy(change.substr(1), BOT_STATE_NON_COMBAT);
}

void TwoPlayersBotBodyRides(Player* body, Player* rider, bool halted)
{
    PlayerbotAI* brain = GET_PLAYERBOT_AI(body);
    if (!brain || !body->IsAlive())
        return;
    if (body->IsMounted())
    {
        if (Fighting(body, brain))
            GetDown(body);
        return;
    }
    WorldPosition const* goal = GoalOf(brain);
    float const distance = GoalDistance(body, goal);
    uint32 const now = getMSTime();
    {
        std::lock_guard<std::mutex> lock(rideMutex);
        Ride& ride = rides[body->GetGUID().GetCounter()];
        if (ride.Held && goal && goal->GetPositionX() == ride.HeldX && goal->GetPositionY() == ride.HeldY)
            return;
        ride.Held = false;
        if (ride.NextTry && getMSTimeDiff(ride.NextTry, now) < RideRetryMs)
            return;
        ride.NextTry = 0;
    }
    if (halted || Fighting(body, brain) || !body->IsOutdoors() || body->GetVehicle() ||
        brain->HasStrategy("stay", BOT_STATE_NON_COMBAT) || distance < MountDistance())
        return;
    {
        std::lock_guard<std::mutex> lock(rideMutex);
        rides[body->GetGUID().GetCounter()].NextTry = now;
    }
    MountUp(body, rider);
}

std::string TwoPlayersBotBodyRideToggle(Player* body, Player* rider)
{
    PlayerbotAI* brain = GET_PLAYERBOT_AI(body);
    if (!brain)
        return "Ride only works when the body is a bot.";
    if (body->IsMounted())
    {
        WorldPosition const* goal = GoalOf(brain);
        {
            std::lock_guard<std::mutex> lock(rideMutex);
            Ride& ride = rides[body->GetGUID().GetCounter()];
            ride.Held = true;
            ride.HeldX = goal ? goal->GetPositionX() : 0.0f;
            ride.HeldY = goal ? goal->GetPositionY() : 0.0f;
        }
        body->TextEmote("puts the " + MountName(body) + " down. Walking it is.");
        body->RemoveAurasByType(SPELL_AURA_MOUNTED);
        return "";
    }
    if (Fighting(body, brain))
        return body->GetName() + " is fighting. Ride after the fight.";
    if (!body->IsOutdoors())
        return "No riding in here.";
    if (!MountUp(body, rider))
    {
        body->Say(body->GetName() + " got no ride to carry. " + body->GetName() + " walk.", LANG_UNIVERSAL);
        return "Neither head knows a mount that works here.";
    }
    return "";
}

void TwoPlayersKeepBotRiderStill(Player* rider)
{
    PlayerbotAI* brain = GET_PLAYERBOT_AI(rider);
    if (!brain)
        return;
    std::string const questing = QuestingStrategy();
    if (brain->HasStrategy("grind", BOT_STATE_NON_COMBAT) || brain->HasStrategy(questing, BOT_STATE_NON_COMBAT) ||
        brain->HasStrategy("food", BOT_STATE_NON_COMBAT))
        brain->ChangeStrategy("-grind,-food,-" + questing, BOT_STATE_NON_COMBAT);
    if (brain->rpgInfo.GetStatus() == RPG_IDLE)
        return;
    brain->rpgInfo.ChangeToIdle();
    if (rider->getStandState() != UNIT_STAND_STATE_STAND)
        rider->SetStandState(UNIT_STAND_STATE_STAND);
}

#else

bool TwoPlayersBotsAvailable()
{
    return false;
}

bool TwoPlayersCallBot(Player* /*master*/, uint32 /*headGuid*/)
{
    return false;
}

void TwoPlayersSteerBotBody(Player* /*body*/, bool /*roam*/)
{
}

void TwoPlayersKeepBotBodyRoaming(Player* /*body*/)
{
}

void TwoPlayersKeepBotRiderStill(Player* /*rider*/)
{
}

void TwoPlayersBotBodyRides(Player* /*body*/, Player* /*rider*/, bool /*halted*/)
{
}

std::string TwoPlayersBotBodyRideToggle(Player* /*body*/, Player* /*rider*/)
{
    return "Ride needs the bot module on this server.";
}

#endif
