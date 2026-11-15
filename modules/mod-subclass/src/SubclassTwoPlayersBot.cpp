/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license.
 */

#include "CellImpl.h"
#include "CharacterCache.h"
#include "Config.h"
#include "DatabaseEnv.h"
#include "DBCStores.h"
#include "GameObject.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
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
#include <list>
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

struct Treasure
{
    ObjectGuid Target;
    uint32 Since = 0;
    uint32 LastLook = 0;
    std::unordered_map<ObjectGuid::LowType, uint32> Tried;
};

std::unordered_map<uint32, Treasure>& treasures = *new std::unordered_map<uint32, Treasure>;

struct Ride
{
    uint32 NextTry = 0;
    bool Held = false;
    float HeldX = 0.0f;
    float HeldY = 0.0f;
    uint32 LastTalk = 0;
    uint32 LastDown = 0;
    uint32 LastMountLine = 0;
    uint32 LastDropLine = 0;
    uint32 MountedAt = 0;
    uint32 PausedAt = 0;
};

constexpr uint32 RideRetryMs = 10000;
constexpr uint32 RemountAfterMs = 30 * IN_MILLISECONDS;
constexpr float DistractionYards = 60.0f;
constexpr float TreasureYards = 60.0f;
constexpr float TreasureReach = 4.0f;
constexpr uint32 TreasureLookMs = 2 * IN_MILLISECONDS;
constexpr uint32 TreasureGiveUpMs = 25 * IN_MILLISECONDS;
constexpr uint32 TreasureForgetMs = 10 * MINUTE * IN_MILLISECONDS;
constexpr uint32 TreasureMoveId = 0x54524541;
constexpr uint32 ShortRideMs = 20 * IN_MILLISECONDS;
constexpr uint32 PauseAfterShortRideMs = 3 * MINUTE * IN_MILLISECONDS;
constexpr float RestedHealthPct = 85.0f;
constexpr float RestedManaPct = 70.0f;
constexpr uint32 TalkEveryMs = 5 * MINUTE * IN_MILLISECONDS;
constexpr uint32 FirstHotsMountSpell = 9860001;
constexpr uint32 LastHotsMountSpell = 9860999;

std::mutex& rideMutex = *new std::mutex;
std::unordered_map<uint32, Ride>& rides = *new std::unordered_map<uint32, Ride>;

std::array<char const*, 10> const MountLines = {
    "Long walk. {0} carry the ride, {1} enjoy the view.",
    "We ride! Well. Ride rides. {0} walks.",
    "{0} legs, ride on top. Fair deal, {1}?",
    "Up you go. Heavy, but faster than {1}'s chatter.",
    "{1} say ride. {0} say who carry who?",
    "Two heads, one back, and now a mount on top. {0} strong.",
    "Shoulders full. Legs busy. {1}, you count the steps.",
    "This mount lazy. {0} do all the walking again.",
    "{1}, stop kicking. Mount not go faster.",
    "Far place is far. {0} carry everything, like always.",
};
std::array<char const*, 6> const DropLines = {
    "drops the {0} and raises both fists.",
    "sets the {0} down, gently. Then charges.",
    "tosses the {0} aside. Fight first, ride later.",
    "plants the {0} in the grass and cracks his knuckles.",
    "shrugs the {0} off his shoulders. Smash time.",
    "parks the {0} behind a rock and turns to fight.",
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
        if (info && info->HasAura(SPELL_AURA_MOUNTED) && !info->HasAura(SPELL_AURA_MOD_INCREASE_MOUNTED_FLIGHT_SPEED) &&
            !info->HasAura(SPELL_AURA_FLY) && std::find(mounts.begin(), mounts.end(), spellId) == mounts.end())
            mounts.push_back(spellId);
    }
}

std::string MountName(Player* body)
{
    Unit::AuraEffectList const& mounts = body->GetAuraEffectsByType(SPELL_AURA_MOUNTED);
    return mounts.empty() ? "mount" : mounts.front()->GetSpellInfo()->SpellName[LOCALE_enUS];
}

bool MayTalk(Player* body, bool asked)
{
    std::lock_guard<std::mutex> lock(rideMutex);
    Ride& ride = rides[body->GetGUID().GetCounter()];
    uint32 const now = getMSTime();
    if (!asked && ride.LastTalk && getMSTimeDiff(ride.LastTalk, now) < TalkEveryMs)
        return false;
    ride.LastTalk = now;
    return true;
}

uint32 FreshLine(Player* body, uint32 count, bool drop)
{
    std::lock_guard<std::mutex> lock(rideMutex);
    uint32& last = drop ? rides[body->GetGUID().GetCounter()].LastDropLine :
        rides[body->GetGUID().GetCounter()].LastMountLine;
    uint32 line = urand(0, count - 2);
    if (line >= last)
        ++line;
    last = line;
    return line;
}

void BodyEmote(Player* body, std::string const& text)
{
    body->TextEmote(" " + text);
}

bool MountUp(Player* body, Player* rider, bool asked)
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
    {
        std::lock_guard<std::mutex> lock(rideMutex);
        rides[body->GetGUID().GetCounter()].MountedAt = getMSTime();
    }
    if (!MayTalk(body, asked))
        return true;
    BodyEmote(body, std::string("hoists the ") + mount->SpellName[LOCALE_enUS] + " onto his shoulders.");
    body->Say(Acore::StringFormat(MountLines[FreshLine(body, MountLines.size(), false)], body->GetName(),
        rider->GetName()), LANG_UNIVERSAL);
    return true;
}

void GetDown(Player* body)
{
    {
        std::lock_guard<std::mutex> lock(rideMutex);
        Ride& ride = rides[body->GetGUID().GetCounter()];
        uint32 const now = getMSTime();
        ride.LastDown = now;
        if (ride.MountedAt && getMSTimeDiff(ride.MountedAt, now) < ShortRideMs)
            ride.PausedAt = now;
        ride.MountedAt = 0;
    }
    if (MayTalk(body, false))
        BodyEmote(body, Acore::StringFormat(DropLines[FreshLine(body, DropLines.size(), true)], MountName(body)));
    body->RemoveAurasByType(SPELL_AURA_MOUNTED);
}

bool Fighting(Player* body, PlayerbotAI* brain)
{
    return body->IsInCombat() || brain->GetState() == BOT_STATE_COMBAT;
}

bool NeedsRest(Player* body)
{
    return body->IsSitState() || body->GetHealthPct() < RestedHealthPct ||
        (body->getPowerType() == POWER_MANA && body->GetPowerPct(POWER_MANA) < RestedManaPct);
}

bool EnemyNear(Player* body)
{
    std::list<Unit*> around;
    Acore::AnyUnfriendlyUnitInObjectRangeCheck check(body, body, DistractionYards);
    Acore::UnitListSearcher<Acore::AnyUnfriendlyUnitInObjectRangeCheck> searcher(body, around, check);
    Cell::VisitObjects(body, searcher, DistractionYards);
    for (Unit* unit : around)
    {
        Creature const* creature = unit->ToCreature();
        if (creature && !creature->IsCivilian() && body->IsValidAttackTarget(unit))
            return true;
    }
    return false;
}

bool OpensWithoutSkill(GameObject const* go)
{
    uint32 const lockId = go->GetGOInfo()->GetLockId();
    LockEntry const* lock = lockId ? sLockStore.LookupEntry(lockId) : nullptr;
    if (!lock)
        return true;
    for (uint8 index = 0; index < MAX_LOCK_CASE; ++index)
    {
        if (lock->Type[index] == LOCK_KEY_ITEM)
            return false;
        if (lock->Type[index] == LOCK_KEY_SKILL && SkillByLockType(LockType(lock->Index[index])) != SKILL_NONE)
            return false;
    }
    return true;
}

bool IsTreasure(Player* body, GameObject const* go)
{
    if (go->GetGoType() != GAMEOBJECT_TYPE_CHEST || !go->isSpawned() || go->getLootState() != GO_READY ||
        !go->GetGOInfo()->chest.lootId || !OpensWithoutSkill(go))
        return false;
    return !go->GetGOInfo()->chest.questId || go->ActivateToQuest(body);
}

GameObject* TreasureInSight(Player* body, Treasure const& treasure, uint32 now)
{
    std::list<GameObject*> seen;
    Acore::GameObjectInRangeCheck check(body->GetPositionX(), body->GetPositionY(), body->GetPositionZ(),
        TreasureYards);
    Acore::GameObjectListSearcher<Acore::GameObjectInRangeCheck> searcher(body, seen, check);
    Cell::VisitObjects(body, searcher, TreasureYards);
    GameObject* nearest = nullptr;
    for (GameObject* go : seen)
    {
        auto const tried = treasure.Tried.find(go->GetSpawnId());
        if ((tried != treasure.Tried.end() && getMSTimeDiff(tried->second, now) < TreasureForgetMs) ||
            !IsTreasure(body, go) || (nearest && body->GetDistance(go) >= body->GetDistance(nearest)))
            continue;
        nearest = go;
    }
    return nearest;
}

void OpenTreasure(Player* body, GameObject* go)
{
    if (body->IsMounted())
        body->RemoveAurasByType(SPELL_AURA_MOUNTED);
    body->StopMoving();
    body->SetFacingToObject(go);
    WorldPacket use(CMSG_GAMEOBJ_USE, 8);
    use << go->GetGUID();
    body->GetSession()->HandleGameObjectUseOpcode(use);
}

bool OpenAfterUnlock(Player* body, GameObject* go)
{
    rideMutex.unlock();
    OpenTreasure(body, go);
    rideMutex.lock();
    return true;
}

bool SeekTreasure(Player* body, PlayerbotAI* brain, bool halted)
{
    uint32 const now = getMSTime();
    uint32 const guid = body->GetGUID().GetCounter();
    std::lock_guard<std::mutex> lock(rideMutex);
    Treasure& treasure = treasures[guid];
    if (halted || Fighting(body, brain) || brain->HasStrategy("stay", BOT_STATE_NON_COMBAT) ||
        body->IsNonMeleeSpellCast(false))
    {
        treasure.Target.Clear();
        return false;
    }
    GameObject* go = treasure.Target ? body->GetMap()->GetGameObject(treasure.Target) : nullptr;
    if (go && (!IsTreasure(body, go) || getMSTimeDiff(treasure.Since, now) > TreasureGiveUpMs))
    {
        treasure.Tried[go->GetSpawnId()] = now;
        go = nullptr;
    }
    if (!go)
    {
        treasure.Target.Clear();
        if (treasure.LastLook && getMSTimeDiff(treasure.LastLook, now) < TreasureLookMs)
            return false;
        treasure.LastLook = now;
        go = TreasureInSight(body, treasure, now);
        if (!go)
            return false;
        treasure.Target = go->GetGUID();
        treasure.Since = now;
    }
    brain->SetNextCheckDelay(IN_MILLISECONDS);
    if (!body->IsWithinDistInMap(go, TreasureReach))
    {
        if (!body->isMoving())
            body->GetMotionMaster()->MovePoint(TreasureMoveId, go->GetPositionX(), go->GetPositionY(),
                go->GetPositionZ());
        return true;
    }
    treasure.Tried[go->GetSpawnId()] = now;
    treasure.Target.Clear();
    return OpenAfterUnlock(body, go);
}

bool Distracted(Player* body, PlayerbotAI* brain)
{
    return brain->GetAiObjectContext()->GetValue<bool>("has available loot")->Get() || EnemyNear(body);
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
    return sPlayerbotAIConfig.enabled;
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
    if (SeekTreasure(body, brain, halted))
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
        if ((ride.NextTry && getMSTimeDiff(ride.NextTry, now) < RideRetryMs) ||
            (ride.LastDown && getMSTimeDiff(ride.LastDown, now) < RemountAfterMs) ||
            (ride.PausedAt && getMSTimeDiff(ride.PausedAt, now) < PauseAfterShortRideMs))
            return;
        ride.NextTry = 0;
    }
    if (halted || Fighting(body, brain) || !body->IsOutdoors() || body->GetVehicle() ||
        brain->HasStrategy("stay", BOT_STATE_NON_COMBAT) || distance < MountDistance() || NeedsRest(body) ||
        Distracted(body, brain))
        return;
    {
        std::lock_guard<std::mutex> lock(rideMutex);
        rides[body->GetGUID().GetCounter()].NextTry = now;
    }
    MountUp(body, rider, false);
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
        BodyEmote(body, "puts the " + MountName(body) + " down. Walking it is.");
        body->RemoveAurasByType(SPELL_AURA_MOUNTED);
        return "";
    }
    if (Fighting(body, brain))
        return body->GetName() + " is fighting. Ride after the fight.";
    if (!body->IsOutdoors())
        return "No riding in here.";
    if (!MountUp(body, rider, true))
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
