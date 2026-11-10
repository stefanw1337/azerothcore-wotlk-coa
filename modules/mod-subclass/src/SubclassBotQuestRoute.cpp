/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license.
 */

#include "Config.h"
#include "Map.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "PlayerScript.h"
#include "QuestDef.h"
#include "SubclassQuestRoute.h"
#include "WorldScript.h"
#include "WorldSession.h"
#include <algorithm>
#include <mutex>
#include <string>
#include <unordered_map>
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
    std::unordered_map<uint32, uint32> SkippedAt;
};

RouteSettings settings;
std::mutex& routeMutex = *new std::mutex;
std::unordered_map<uint32, BotRoute>& routes = *new std::unordered_map<uint32, BotRoute>;

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

bool StopOf(Player* bot, PlayerbotAI* brain, uint32 questId, Subclass::RouteStop& stop)
{
    Quest const* quest = sObjectMgr->GetQuestTemplate(questId);
    QuestPOIVector const* pois = sObjectMgr->GetQuestPOIVector(questId);
    auto const found = bot->getQuestStatusMap().find(questId);
    if (!quest || !pois || found == bot->getQuestStatusMap().end() || brain->lowPriorityQuest.count(questId))
        return false;
    if (found->second.Status != QUEST_STATUS_COMPLETE && !DoableAlone(bot, quest))
        return false;
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
    return nearest < QuestReach;
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
    for (uint8 slot = 0; slot < MAX_QUEST_LOG_SIZE; ++slot)
    {
        uint32 const questId = bot->GetQuestSlotQuestId(slot);
        auto const skipped = route.SkippedAt.find(questId);
        if (skipped != route.SkippedAt.end() && getMSTimeDiff(skipped->second, now) < SkipMs)
            continue;
        Subclass::RouteStop stop;
        if (questId && StopOf(bot, brain, questId, stop))
            stops.push_back(stop);
    }
    route.Quests = Subclass::PlanQuestRoute(bot->GetPositionX(), bot->GetPositionY(), stops);
    route.Log = LogOf(bot);
}

bool Steerable(Player* bot, PlayerbotAI* brain)
{
    return bot->IsAlive() && !bot->IsInCombat() && !bot->IsInFlight() && !bot->GetMap()->Instanceable() &&
        brain->HasStrategy("new rpg", BOT_STATE_NON_COMBAT);
}

void FollowRoute(Player* bot, PlayerbotAI* brain, BotRoute& route)
{
    if (route.Log != LogOf(bot) || route.Quests.empty())
        Replan(bot, brain, route);
    if (route.Quests.empty())
        return;
    uint32 const head = route.Quests.front();
    NewRpgInfo& info = brain->rpgInfo;
    NewRpgStatus const status = info.GetStatus();
    auto const* doing = std::get_if<NewRpgInfo::DoQuest>(&info.data);
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
    if (status != RPG_DO_QUEST && !(status == RPG_IDLE && settings.PreferQuests))
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
        route.SkippedAt[head] = getMSTime();
        route.Quests.erase(route.Quests.begin());
        route.Steers = 0;
        return;
    }
    if (Quest const* quest = sObjectMgr->GetQuestTemplate(head))
        info.ChangeToDoQuest(head, quest);
}
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

void AddSC_mod_subclass_bot_quest_route()
{
}

#endif
