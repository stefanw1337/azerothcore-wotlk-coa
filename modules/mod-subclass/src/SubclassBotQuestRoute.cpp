/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license.
 */

#include "Cell.h"
#include "CellImpl.h"
#include "Chat.h"
#include "Config.h"
#include "GameObject.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "Group.h"
#include "ObjectAccessor.h"
#include "Map.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "PlayerScript.h"
#include "QuestDef.h"
#include "Spell.h"
#include "SubclassQuestRoute.h"
#include "WorldPacket.h"
#include "WorldScript.h"
#include "WorldSession.h"
#include <algorithm>
#include <list>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#if __has_include("Playerbots.h") && __has_include("NewRpgInfo.h")
#include "Playerbots.h"
#define SUBCLASS_QUEST_ROUTE_WITH_SQUID_PLAYERBOTS 1
#endif

#ifdef SUBCLASS_QUEST_ROUTE_WITH_SQUID_PLAYERBOTS

namespace
{
constexpr uint32 CheckMs = 2000;
constexpr float QuestReach = 1500.0f;
constexpr uint32 StuckSteers = 3;
constexpr uint32 SkipMs = 15 * MINUTE * IN_MILLISECONDS;
constexpr float ObjectReach = 4.0f;
constexpr float PassingYards = 60.0f;
constexpr uint32 ObjectGiveUpMs = 30 * IN_MILLISECONDS;
constexpr uint32 ObjectForgetMs = 5 * MINUTE * IN_MILLISECONDS;
constexpr uint32 ObjectMoveId = 0x5CB0;
constexpr float UseReach = 10.0f;
constexpr uint32 UseWaitMs = 15 * IN_MILLISECONDS;
constexpr float DropperFollowYards = 15.0f;
constexpr float EmptySpawnYards = 40.0f;

struct RouteSettings
{
    bool Enabled = true;
    bool PreferQuests = true;
};

struct BotRoute
{
    uint32 SinceCheck = 0;
    std::string Log;
    std::vector<uint32> Quests;
    uint32 Head = 0;
    uint32 Steers = 0;
    uint64 Progress = 0;
    std::vector<uint32> Priority;
    std::vector<std::pair<uint32, std::string_view>> Unplanned;
    std::unordered_map<uint32, uint32> SkippedAt;
    ObjectGuid Object;
    uint32 ObjectQuest = 0;
    uint32 ObjectSince = 0;
    std::unordered_map<ObjectGuid::LowType, uint32> ObjectTried;
    uint32 UsedAt = 0;
};

struct UseOn
{
    uint32 ItemId = 0;
    uint32 Target = 0;
    int32 Objective = -1;
};

std::unordered_map<uint32, std::vector<uint32>>& droppers = *new std::unordered_map<uint32, std::vector<uint32>>;
std::map<std::pair<uint32, uint32>, std::vector<Position>>& dropperSpawns =
    *new std::map<std::pair<uint32, uint32>, std::vector<Position>>;

RouteSettings settings;
std::mutex& routeMutex = *new std::mutex;
std::unordered_map<uint32, BotRoute>& routes = *new std::unordered_map<uint32, BotRoute>;
std::unordered_map<uint32, std::set<uint32>>& ignored = *new std::unordered_map<uint32, std::set<uint32>>;

bool Ignored(Player* bot, uint32 questId)
{
    auto const found = ignored.find(bot->GetGUID().GetCounter());
    return found != ignored.end() && found->second.count(questId);
}

bool DoableAlone(Player* bot, Quest const* quest)
{
    return bot->GetLevel() + 3 >= bot->GetQuestLevel(quest) && quest->GetType() == 0 &&
        quest->GetSuggestedPlayers() < 2;
}

std::vector<int32> ObjectivesLeft(Quest const* quest, QuestStatusData const& status)
{
    if (status.Status == QUEST_STATUS_COMPLETE)
        return { -1 };
    std::vector<int32> left;
    if (status.Status != QUEST_STATUS_INCOMPLETE)
        return left;
    for (int32 i = 0; i < QUEST_OBJECTIVES_COUNT; ++i)
        if (quest->RequiredNpcOrGo[i] && status.CreatureOrGOCount[i] < quest->RequiredNpcOrGoCount[i])
            left.push_back(i);
    for (int32 i = 0; i < QUEST_ITEM_OBJECTIVES_COUNT; ++i)
        if (quest->RequiredItemId[i] && status.ItemCount[i] < quest->RequiredItemCount[i])
            left.push_back(QUEST_OBJECTIVES_COUNT + i);
    return left;
}

std::string_view StopOf(Player* bot, PlayerbotAI* brain, uint32 questId, Subclass::RouteStop& stop)
{
    Quest const* quest = sObjectMgr->GetQuestTemplate(questId);
    QuestPOIVector const* pois = sObjectMgr->GetQuestPOIVector(questId);
    auto const found = bot->getQuestStatusMap().find(questId);
    if (!quest || found == bot->getQuestStatusMap().end())
        return "unknown";
    if (found->second.Status == QUEST_STATUS_FAILED)
        return "failed";
    if (brain->lowPriorityQuest.count(questId))
        return "set aside by the bot";
    if (found->second.Status != QUEST_STATUS_COMPLETE && !DoableAlone(bot, quest))
        return bot->GetLevel() + 3 < bot->GetQuestLevel(quest) ? "level too high" : "group quest";
    if (!pois)
        return "no map marker";
    std::vector<int32> const left = ObjectivesLeft(quest, found->second);
    float nearest = QuestReach;
    for (QuestPOI const& poi : *pois)
    {
        if (poi.MapId != bot->GetMapId() || poi.points.empty() ||
            std::find(left.begin(), left.end(), poi.ObjectiveIndex) == left.end())
            continue;
        float x = 0.0f;
        float y = 0.0f;
        for (QuestPOIPoint const& point : poi.points)
        {
            x += point.x;
            y += point.y;
        }
        x /= poi.points.size();
        y /= poi.points.size();
        float const distance = bot->GetDistance2d(x, y);
        if (distance >= nearest)
            continue;
        float const z = std::max(bot->GetMap()->GetHeight(x, y, MAX_HEIGHT), bot->GetMap()->GetWaterLevel(x, y));
        if (z == INVALID_HEIGHT || z == VMAP_INVALID_HEIGHT_VALUE ||
            bot->GetZoneId() != bot->GetMap()->GetZoneId(bot->GetPhaseMask(), x, y, z))
            continue;
        nearest = distance;
        stop = { questId, x, y };
    }
    return nearest < QuestReach ? "" : "far away";
}

std::string LogOf(Player* bot)
{
    std::string log = std::to_string(bot->GetZoneId());
    for (uint8 slot = 0; slot < MAX_QUEST_LOG_SIZE; ++slot)
        if (uint32 const questId = bot->GetQuestSlotQuestId(slot))
            log += " " + std::to_string(questId) + ":" + std::to_string(bot->GetQuestStatus(questId));
    return log;
}

uint64 ProgressOf(Player* bot, uint32 questId)
{
    auto const found = bot->getQuestStatusMap().find(questId);
    if (found == bot->getQuestStatusMap().end())
        return 0;
    uint64 progress = uint64(found->second.Status) << 48;
    for (int32 i = 0; i < QUEST_OBJECTIVES_COUNT; ++i)
        progress += found->second.CreatureOrGOCount[i];
    for (int32 i = 0; i < QUEST_ITEM_OBJECTIVES_COUNT; ++i)
        progress += found->second.ItemCount[i];
    return progress;
}

void Replan(Player* bot, PlayerbotAI* brain, BotRoute& route)
{
    uint32 const now = getMSTime();
    std::vector<Subclass::RouteStop> stops;
    route.Unplanned.clear();
    for (uint8 slot = 0; slot < MAX_QUEST_LOG_SIZE; ++slot)
    {
        uint32 const questId = bot->GetQuestSlotQuestId(slot);
        if (!questId)
            continue;
        if (Ignored(bot, questId))
        {
            brain->lowPriorityQuest.insert(questId);
            route.Unplanned.emplace_back(questId, "ignored");
            continue;
        }
        auto const skipped = route.SkippedAt.find(questId);
        if (skipped != route.SkippedAt.end() && getMSTimeDiff(skipped->second, now) < SkipMs)
        {
            route.Unplanned.emplace_back(questId, "stuck, tried again soon");
            continue;
        }
        Subclass::RouteStop stop;
        std::string_view const reason = StopOf(bot, brain, questId, stop);
        if (reason.empty())
            stops.push_back(stop);
        else
            route.Unplanned.emplace_back(questId, reason);
    }
    route.Priority.erase(std::remove_if(route.Priority.begin(), route.Priority.end(),
        [bot](uint32 questId) { return bot->FindQuestSlot(questId) >= MAX_QUEST_LOG_SIZE; }), route.Priority.end());
    route.Quests = Subclass::PriorityFirst(Subclass::PlanQuestRoute(bot->GetPositionX(), bot->GetPositionY(), stops),
        route.Priority);
    for (auto chosen = route.Priority.rbegin(); chosen != route.Priority.rend(); ++chosen)
    {
        uint32 const questId = *chosen;
        if (Ignored(bot, questId) || std::find(route.Quests.begin(), route.Quests.end(), questId) != route.Quests.end())
            continue;
        route.Quests.insert(route.Quests.begin(), questId);
        route.Unplanned.erase(std::remove_if(route.Unplanned.begin(), route.Unplanned.end(),
            [questId](auto const& unplanned) { return unplanned.first == questId; }), route.Unplanned.end());
    }
    route.Log = LogOf(bot);
}

void AimAtQuest(Player* bot, NewRpgInfo::DoQuest& doing)
{
    if (doing.pos != WorldPosition())
        return;
    Quest const* quest = sObjectMgr->GetQuestTemplate(doing.questId);
    QuestPOIVector const* pois = sObjectMgr->GetQuestPOIVector(doing.questId);
    auto const found = bot->getQuestStatusMap().find(doing.questId);
    if (!quest || !pois || found == bot->getQuestStatusMap().end())
        return;
    std::vector<int32> const left = ObjectivesLeft(quest, found->second);
    float nearest = 0.0f;
    for (QuestPOI const& poi : *pois)
    {
        if (poi.MapId != bot->GetMapId() || poi.points.empty() ||
            std::find(left.begin(), left.end(), poi.ObjectiveIndex) == left.end())
            continue;
        float x = 0.0f;
        float y = 0.0f;
        for (QuestPOIPoint const& point : poi.points)
        {
            x += point.x;
            y += point.y;
        }
        x /= poi.points.size();
        y /= poi.points.size();
        float const z = std::max(bot->GetMap()->GetHeight(x, y, MAX_HEIGHT), bot->GetMap()->GetWaterLevel(x, y));
        float const distance = bot->GetDistance2d(x, y);
        if (z == INVALID_HEIGHT || z == VMAP_INVALID_HEIGHT_VALUE || (nearest && distance >= nearest))
            continue;
        nearest = distance;
        doing.pos = WorldPosition(bot->GetMapId(), x, y, z);
        doing.objectiveIdx = poi.ObjectiveIndex;
        doing.lastReachPOI = 0;
    }
}

bool Steerable(Player* bot, PlayerbotAI* brain)
{
    return bot->IsAlive() && !bot->IsInCombat() && !bot->IsInFlight() && !bot->GetMap()->Instanceable() &&
        brain->HasStrategy("new rpg", BOT_STATE_NON_COMBAT) && !brain->HasStrategy("stay", BOT_STATE_NON_COMBAT);
}

int32 ObjectiveFrom(Player* bot, uint32 questId, GameObjectQuestItemList const& items)
{
    Quest const* quest = sObjectMgr->GetQuestTemplate(questId);
    auto const found = bot->getQuestStatusMap().find(questId);
    if (!quest || found == bot->getQuestStatusMap().end() || found->second.Status != QUEST_STATUS_INCOMPLETE ||
        Ignored(bot, questId))
        return -1;
    for (int32 i = 0; i < QUEST_ITEM_OBJECTIVES_COUNT; ++i)
        if (quest->RequiredItemId[i] && found->second.ItemCount[i] < quest->RequiredItemCount[i] &&
            std::find(items.begin(), items.end(), quest->RequiredItemId[i]) != items.end())
            return QUEST_OBJECTIVES_COUNT + i;
    return -1;
}

struct QuestObject
{
    GameObject* Object = nullptr;
    uint32 QuestId = 0;
    int32 Objective = 0;
};

QuestObject QuestObjectInSight(Player* bot, uint32 doingQuestId, BotRoute const& route)
{
    uint32 const now = getMSTime();
    float const sight = sPlayerbotAIConfig.sightDistance;
    std::list<GameObject*> seen;
    Acore::GameObjectInRangeCheck check(bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ(), sight);
    Acore::GameObjectListSearcher<Acore::GameObjectInRangeCheck> searcher(bot, seen, check);
    Cell::VisitObjects(bot, searcher, sight);
    QuestObject nearest;
    for (GameObject* go : seen)
    {
        GameObjectQuestItemList const* items = sObjectMgr->GetGameObjectQuestItemList(go->GetEntry());
        auto const tried = route.ObjectTried.find(go->GetSpawnId());
        float const distance = bot->GetDistance(go);
        if (!items || go->GetGoType() != GAMEOBJECT_TYPE_CHEST || !go->isSpawned() || go->getLootState() != GO_READY ||
            !go->ActivateToQuest(bot) || (nearest.Object && distance >= bot->GetDistance(nearest.Object)) ||
            (tried != route.ObjectTried.end() && getMSTimeDiff(tried->second, now) < ObjectForgetMs))
            continue;
        if (doingQuestId)
            if (int32 const objective = ObjectiveFrom(bot, doingQuestId, *items); objective >= 0)
            {
                nearest = { go, doingQuestId, objective };
                continue;
            }
        if (distance > PassingYards)
            continue;
        for (auto const& [questId, status] : bot->getQuestStatusMap())
            if (int32 const objective = ObjectiveFrom(bot, questId, *items); objective >= 0)
            {
                nearest = { go, questId, objective };
                break;
            }
    }
    return nearest;
}

bool Openable(Player* bot, GameObject const* go)
{
    return go->isSpawned() && go->getLootState() == GO_READY && go->ActivateToQuest(bot);
}

void OpenQuestObject(Player* bot, GameObject* go)
{
    if (bot->IsMounted())
        bot->RemoveAurasByType(SPELL_AURA_MOUNTED);
    bot->StopMoving();
    bot->SetFacingToObject(go);
    WorldPacket use(CMSG_GAMEOBJ_USE, 8);
    use << go->GetGUID();
    bot->GetSession()->HandleGameObjectUseOpcode(use);
}

bool SteerToQuestObject(Player* bot, PlayerbotAI* brain, BotRoute& route, NewRpgInfo::DoQuest* doing)
{
    uint32 const now = getMSTime();
    GameObject* go = route.Object ? bot->GetMap()->GetGameObject(route.Object) : nullptr;
    if (go && (!Openable(bot, go) || getMSTimeDiff(route.ObjectSince, now) > ObjectGiveUpMs))
    {
        route.ObjectTried[go->GetSpawnId()] = now;
        go = nullptr;
    }
    if (!go)
    {
        route.Object.Clear();
        QuestObject const found = QuestObjectInSight(bot, doing ? doing->questId : 0, route);
        if (!found.Object)
            return false;
        go = found.Object;
        route.Object = go->GetGUID();
        route.ObjectQuest = found.QuestId;
        route.ObjectSince = now;
        if (doing && doing->questId == found.QuestId)
        {
            doing->pos = WorldPosition(go->GetMapId(), go->GetPositionX(), go->GetPositionY(), go->GetPositionZ());
            doing->objectiveIdx = found.Objective;
            doing->lastReachPOI = 0;
        }
    }
    brain->SetNextCheckDelay(CheckMs);
    if (!bot->IsWithinDistInMap(go, ObjectReach))
    {
        bot->GetMotionMaster()->MovePoint(ObjectMoveId, go->GetPositionX(), go->GetPositionY(), go->GetPositionZ());
        return true;
    }
    route.ObjectTried[go->GetSpawnId()] = now;
    route.Object.Clear();
    brain->lowPriorityQuest.erase(route.ObjectQuest);
    OpenQuestObject(bot, go);
    return true;
}

UseOn UseOnOf(Player* bot, Quest const* quest, QuestStatusData const& status)
{
    for (int32 i = 0; i < QUEST_OBJECTIVES_COUNT; ++i)
    {
        int32 const target = quest->RequiredNpcOrGo[i];
        CreatureTemplate const* creature = target > 0 ? sObjectMgr->GetCreatureTemplate(target) : nullptr;
        if (!creature || status.CreatureOrGOCount[i] >= quest->RequiredNpcOrGoCount[i])
            continue;
        FactionTemplateEntry const* faction = sFactionTemplateStore.LookupEntry(creature->faction);
        if (!faction || faction->IsHostileTo(*bot->GetFactionTemplateEntry()))
            continue;
        for (uint32 itemId : quest->ItemDrop)
            if (ItemTemplate const* proto = itemId ? sObjectMgr->GetItemTemplate(itemId) : nullptr)
                for (auto const& spell : proto->Spells)
                    if (spell.SpellId > 0 && spell.SpellTrigger == ITEM_SPELLTRIGGER_ON_USE)
                        return { itemId, uint32(target), i };
    }
    return {};
}

std::vector<uint32> const& DroppersOf(uint32 itemId)
{
    auto [found, fresh] = droppers.try_emplace(itemId);
    if (fresh)
        for (auto const& [entry, creature] : *sObjectMgr->GetCreatureTemplates())
            if (CreatureQuestItemList const* items = sObjectMgr->GetCreatureQuestItemList(entry))
                if (std::find(items->begin(), items->end(), itemId) != items->end())
                    found->second.push_back(entry);
    return found->second;
}

std::vector<Position> const& DropperSpawnsOf(uint32 itemId, uint32 mapId)
{
    auto [found, fresh] = dropperSpawns.try_emplace({ itemId, mapId });
    if (fresh)
    {
        std::vector<uint32> const& entries = DroppersOf(itemId);
        for (auto const& [spawnId, data] : sObjectMgr->GetAllCreatureData())
            if (data.mapid == mapId && std::find(entries.begin(), entries.end(), data.id) != entries.end())
                found->second.emplace_back(data.posX, data.posY, data.posZ);
    }
    return found->second;
}

Position NearestDropper(Player* bot, uint32 itemId)
{
    float const sight = sPlayerbotAIConfig.sightDistance;
    Creature* nearest = nullptr;
    for (uint32 entry : DroppersOf(itemId))
        if (Creature* creature = bot->FindNearestCreature(entry, sight))
            if (!nearest || bot->GetDistance(creature) < bot->GetDistance(nearest))
                nearest = creature;
    if (nearest)
        return nearest->GetPosition();
    Position best;
    float bestDistance = 0.0f;
    for (Position const& spawn : DropperSpawnsOf(itemId, bot->GetMapId()))
    {
        float const distance = bot->GetExactDist(&spawn);
        if (distance > EmptySpawnYards && (!bestDistance || distance < bestDistance))
        {
            best = spawn;
            bestDistance = distance;
        }
    }
    return best;
}

bool UseQuestItemOn(Player* bot, PlayerbotAI* brain, BotRoute& route, NewRpgInfo::DoQuest& doing)
{
    Quest const* quest = sObjectMgr->GetQuestTemplate(doing.questId);
    auto const found = bot->getQuestStatusMap().find(doing.questId);
    if (!quest || found == bot->getQuestStatusMap().end() || found->second.Status != QUEST_STATUS_INCOMPLETE)
        return false;
    UseOn const use = UseOnOf(bot, quest, found->second);
    if (use.Objective < 0)
        return false;
    Item* item = bot->GetItemByEntry(use.ItemId);
    if (!item)
    {
        Position const dropper = NearestDropper(bot, use.ItemId);
        if (dropper.GetPositionX() == 0.0f && dropper.GetPositionY() == 0.0f)
            return false;
        if (doing.pos.GetMapId() != bot->GetMapId() || doing.pos.GetExactDist(dropper) > DropperFollowYards)
        {
            doing.pos = WorldPosition(bot->GetMapId(), dropper.GetPositionX(), dropper.GetPositionY(),
                dropper.GetPositionZ());
            doing.objectiveIdx = use.Objective;
            doing.lastReachPOI = 0;
        }
        return false;
    }
    Creature* target = bot->FindNearestCreature(use.Target, sPlayerbotAIConfig.sightDistance);
    if (!target)
        return false;
    uint32 const now = getMSTime();
    brain->SetNextCheckDelay(CheckMs);
    if (!bot->IsWithinDistInMap(target, UseReach))
    {
        bot->GetMotionMaster()->MovePoint(ObjectMoveId, target->GetPositionX(), target->GetPositionY(),
            target->GetPositionZ());
        return true;
    }
    if (route.UsedAt && getMSTimeDiff(route.UsedAt, now) < UseWaitMs)
        return true;
    if (bot->IsMounted())
        bot->RemoveAurasByType(SPELL_AURA_MOUNTED);
    bot->StopMoving();
    bot->SetFacingToObject(target);
    SpellCastTargets targets;
    targets.SetUnitTarget(target);
    bot->CastItemUseSpell(item, targets, 1, 0);
    route.UsedAt = now;
    return true;
}

void FollowRoute(Player* bot, PlayerbotAI* brain, BotRoute& route)
{
    if (route.Log != LogOf(bot) || route.Quests.empty())
        Replan(bot, brain, route);
    if (route.Quests.empty())
        return;
    uint32 const head = route.Quests.front();
    NewRpgInfo& info = brain->rpgInfo;
    auto* doing = std::get_if<NewRpgInfo::DoQuest>(&info.data);
    if (doing && Ignored(bot, doing->questId))
    {
        info.ChangeToIdle();
        doing = nullptr;
    }
    if (SteerToQuestObject(bot, brain, route, doing))
        return;
    if (doing && UseQuestItemOn(bot, brain, route, *doing))
        return;
    if (doing)
        AimAtQuest(bot, *doing);
    NewRpgStatus const status = info.GetStatus();
    uint64 const progress = ProgressOf(bot, head);
    if (doing && doing->questId == head)
    {
        if (route.Head != head || route.Progress != progress)
        {
            route.Head = head;
            route.Progress = progress;
            route.Steers = 0;
        }
        return;
    }
    bool const drifting = status == RPG_IDLE || status == RPG_WANDER_RANDOM || status == RPG_WANDER_NPC ||
        status == RPG_GO_GRIND || status == RPG_GO_CAMP;
    if (status != RPG_DO_QUEST && !(drifting && settings.PreferQuests))
        return;
    if (route.Head == head && route.Progress == progress)
        ++route.Steers;
    else
    {
        route.Head = head;
        route.Progress = progress;
        route.Steers = 1;
    }
    if (route.Steers > StuckSteers)
    {
        route.Priority.erase(std::remove(route.Priority.begin(), route.Priority.end(), head), route.Priority.end());
        route.SkippedAt[head] = getMSTime();
        route.Quests.erase(route.Quests.begin());
        route.Steers = 0;
        return;
    }
    if (Quest const* quest = sObjectMgr->GetQuestTemplate(head))
    {
        info.ChangeToDoQuest(head, quest);
        if (auto* now = std::get_if<NewRpgInfo::DoQuest>(&info.data))
            AimAtQuest(bot, *now);
    }
}
}

namespace
{
std::string_view StatusWords(NewRpgStatus status)
{
    switch (status)
    {
        case RPG_IDLE: return "idle";
        case RPG_GO_GRIND: return "going to grind";
        case RPG_GO_CAMP: return "going to camp";
        case RPG_WANDER_RANDOM: return "looking around";
        case RPG_WANDER_NPC: return "visiting someone";
        case RPG_DO_QUEST: return "questing";
        case RPG_TRAVEL_FLIGHT: return "flying";
        case RPG_REST: return "resting";
        default: return "busy";
    }
}

std::string Plain(std::string text)
{
    std::replace_if(text.begin(), text.end(), [](char c) { return c == '|' || c == ';' || c == '\t'; }, ' ');
    return text;
}

std::string TitleOf(uint32 questId)
{
    Quest const* quest = sObjectMgr->GetQuestTemplate(questId);
    return quest ? Plain(quest->GetTitle()) : std::to_string(questId);
}

std::string WithCounts(Player* bot, uint32 questId, std::string title)
{
    Quest const* quest = sObjectMgr->GetQuestTemplate(questId);
    auto const found = bot->getQuestStatusMap().find(questId);
    if (!quest || found == bot->getQuestStatusMap().end())
        return title;
    if (found->second.Status == QUEST_STATUS_COMPLETE)
        return title + " (done)";
    std::string counts;
    for (int32 i = 0; i < QUEST_OBJECTIVES_COUNT; ++i)
        if (quest->RequiredNpcOrGo[i] && quest->RequiredNpcOrGoCount[i])
            counts += (counts.empty() ? "" : ", ") + std::to_string(found->second.CreatureOrGOCount[i]) + "/" +
                std::to_string(quest->RequiredNpcOrGoCount[i]);
    for (int32 i = 0; i < QUEST_ITEM_OBJECTIVES_COUNT; ++i)
        if (quest->RequiredItemId[i] && quest->RequiredItemCount[i])
            counts += (counts.empty() ? "" : ", ") + std::to_string(found->second.ItemCount[i]) + "/" +
                std::to_string(quest->RequiredItemCount[i]);
    return counts.empty() ? title : title + " (" + counts + ")";
}

std::string PlanOf(Player* player, Player* bot, PlayerbotAI* brain)
{
    BotRoute& route = routes[bot->GetGUID().GetCounter()];
    if (bot->GetMap() == player->GetMap() && route.Log != LogOf(bot))
        Replan(bot, brain, route);
    NewRpgInfo& info = brain->rpgInfo;
    auto const* doing = std::get_if<NewRpgInfo::DoQuest>(&info.data);
    uint32 const doingId = doing ? doing->questId : 0;
    std::string plan = std::string(StatusWords(info.GetStatus())) + "|" + std::to_string(doingId) + ":" +
        (doingId ? WithCounts(bot, doingId, TitleOf(doingId)) : "") + "|";
    std::string separator;
    for (uint32 const questId : route.Quests)
    {
        plan += separator + std::to_string(questId) + ":" + WithCounts(bot, questId, TitleOf(questId));
        separator = ";";
    }
    plan += "|";
    separator.clear();
    for (auto const& [questId, reason] : route.Unplanned)
    {
        plan += separator + std::to_string(questId) + ":" + std::string(reason) + ":" +
            WithCounts(bot, questId, TitleOf(questId));
        separator = ";";
    }
    return plan;
}

void SendPlanLine(Player* player, std::string const& line)
{
    WorldPacket packet;
    ChatHandler::BuildChatPacket(packet, CHAT_MSG_WHISPER, LANG_ADDON, player, player, "SUBCLASS\t" + line);
    player->SendDirectMessage(&packet);
}

std::vector<Player*> GroupBots(Player* player)
{
    std::vector<Player*> bots;
    Group* group = player->GetGroup();
    if (!group)
        return bots;
    for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
        if (Player* member = ref->GetSource())
            if (member != player && member->GetSession() && member->GetSession()->IsBot() && GET_PLAYERBOT_AI(member))
                bots.push_back(member);
    return bots;
}
}

void SendBotPlans(Player* player)
{
    std::vector<Player*> const bots = GroupBots(player);
    std::vector<std::string> lines;
    {
        std::lock_guard<std::mutex> lock(routeMutex);
        for (Player* bot : bots)
            lines.push_back("BOTPLAN " + bot->GetName() + " " + PlanOf(player, bot, GET_PLAYERBOT_AI(bot)));
    }
    for (std::string const& line : lines)
        SendPlanLine(player, line);
    SendPlanLine(player, "BOTPLANEND");
}

void BotPlanDoNow(Player* player, std::string_view botName, uint32 questId)
{
    for (Player* bot : GroupBots(player))
    {
        if (bot->GetName() != botName || bot->GetQuestStatus(questId) == QUEST_STATUS_NONE)
            continue;
        PlayerbotAI* brain = GET_PLAYERBOT_AI(bot);
        std::lock_guard<std::mutex> lock(routeMutex);
        ignored[bot->GetGUID().GetCounter()].erase(questId);
        brain->lowPriorityQuest.erase(questId);
        BotRoute& route = routes[bot->GetGUID().GetCounter()];
        route.SkippedAt.erase(questId);
        route.Steers = 0;
        if (std::find(route.Quests.begin(), route.Quests.end(), questId) == route.Quests.end())
            route.Quests.push_back(questId);
        route.Quests = Subclass::MovedFirst(route.Quests, questId);
        route.Priority = route.Quests;
        route.Unplanned.erase(std::remove_if(route.Unplanned.begin(), route.Unplanned.end(),
            [questId](auto const& unplanned) { return unplanned.first == questId; }), route.Unplanned.end());
        if (Quest const* quest = sObjectMgr->GetQuestTemplate(questId))
        {
            brain->rpgInfo.ChangeToDoQuest(questId, quest);
            if (auto* now = std::get_if<NewRpgInfo::DoQuest>(&brain->rpgInfo.data))
                AimAtQuest(bot, *now);
        }
    }
    SendBotPlans(player);
}

void BotPlanMoveUp(Player* player, std::string_view botName, uint32 questId)
{
    for (Player* bot : GroupBots(player))
    {
        if (bot->GetName() != botName)
            continue;
        std::lock_guard<std::mutex> lock(routeMutex);
        BotRoute& route = routes[bot->GetGUID().GetCounter()];
        route.Quests = Subclass::MovedUp(route.Quests, questId);
        route.Priority = route.Quests;
        route.Steers = 0;
    }
    SendBotPlans(player);
}

void BotPlanIgnore(Player* player, std::string_view botName, uint32 questId, bool ignore)
{
    for (Player* bot : GroupBots(player))
    {
        if (bot->GetName() != botName || bot->GetQuestStatus(questId) == QUEST_STATUS_NONE)
            continue;
        PlayerbotAI* brain = GET_PLAYERBOT_AI(bot);
        std::lock_guard<std::mutex> lock(routeMutex);
        uint32 const guid = bot->GetGUID().GetCounter();
        BotRoute& route = routes[guid];
        route.Priority.erase(std::remove(route.Priority.begin(), route.Priority.end(), questId), route.Priority.end());
        route.Quests.clear();
        route.Steers = 0;
        if (!ignore)
        {
            ignored[guid].erase(questId);
            brain->lowPriorityQuest.erase(questId);
            continue;
        }
        ignored[guid].insert(questId);
        brain->lowPriorityQuest.insert(questId);
        auto const* doing = std::get_if<NewRpgInfo::DoQuest>(&brain->rpgInfo.data);
        if (doing && doing->questId == questId)
            brain->rpgInfo.ChangeToIdle();
    }
    SendBotPlans(player);
}

class mod_subclass_bot_quest_route : public PlayerScript
{
public:
    mod_subclass_bot_quest_route() : PlayerScript("mod_subclass_bot_quest_route",
        { PLAYERHOOK_ON_UPDATE, PLAYERHOOK_ON_LOGOUT })
    {
    }

    void OnPlayerUpdate(Player* player, uint32 diff) override
    {
        if (!settings.Enabled || !player->GetSession() || !player->GetSession()->IsBot())
            return;
        PlayerbotAI* brain = GET_PLAYERBOT_AI(player);
        if (!brain)
            return;
        std::lock_guard<std::mutex> lock(routeMutex);
        BotRoute& route = routes[player->GetGUID().GetCounter()];
        route.SinceCheck += diff;
        if (route.SinceCheck < CheckMs)
            return;
        route.SinceCheck = 0;
        if (Steerable(player, brain))
            FollowRoute(player, brain, route);
    }

    void OnPlayerLogout(Player* player) override
    {
        std::lock_guard<std::mutex> lock(routeMutex);
        routes.erase(player->GetGUID().GetCounter());
    }
};

class mod_subclass_bot_quest_route_world : public WorldScript
{
public:
    mod_subclass_bot_quest_route_world() : WorldScript("mod_subclass_bot_quest_route_world",
        { WORLDHOOK_ON_AFTER_CONFIG_LOAD })
    {
    }

    void OnAfterConfigLoad(bool /*reload*/) override
    {
        settings.Enabled = sConfigMgr->GetOption<bool>("Subclass.Bots.QuestRoute.Enable", true);
        settings.PreferQuests = sConfigMgr->GetOption<bool>("Subclass.Bots.QuestRoute.PreferQuests", true);
    }
};

void AddSC_mod_subclass_bot_quest_route()
{
    new mod_subclass_bot_quest_route();
    new mod_subclass_bot_quest_route_world();
}

#else

void SendBotPlans(Player* player)
{
    WorldPacket packet;
    ChatHandler::BuildChatPacket(packet, CHAT_MSG_WHISPER, LANG_ADDON, player, player, "SUBCLASS\tBOTPLANEND");
    player->SendDirectMessage(&packet);
}

void BotPlanDoNow(Player* player, std::string_view /*botName*/, uint32 /*questId*/)
{
    SendBotPlans(player);
}

void BotPlanMoveUp(Player* player, std::string_view /*botName*/, uint32 /*questId*/)
{
    SendBotPlans(player);
}

void BotPlanIgnore(Player* player, std::string_view /*botName*/, uint32 /*questId*/, bool /*ignore*/)
{
    SendBotPlans(player);
}

void AddSC_mod_subclass_bot_quest_route()
{
}

#endif
