/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license.
 */

#include "Chat.h"
#include "Config.h"
#include "Group.h"
#include "ObjectAccessor.h"
#include "Map.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "PlayerScript.h"
#include "QuestDef.h"
#include "SubclassQuestRoute.h"
#include "WorldPacket.h"
#include "WorldScript.h"
#include "WorldSession.h"
#include <algorithm>
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
};

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
    route.Log = LogOf(bot);
}

bool Steerable(Player* bot, PlayerbotAI* brain)
{
    return bot->IsAlive() && !bot->IsInCombat() && !bot->IsInFlight() && !bot->GetMap()->Instanceable() &&
        brain->HasStrategy("new rpg", BOT_STATE_NON_COMBAT) && !brain->HasStrategy("stay", BOT_STATE_NON_COMBAT);
}

void FollowRoute(Player* bot, PlayerbotAI* brain, BotRoute& route)
{
    if (route.Log != LogOf(bot) || route.Quests.empty())
        Replan(bot, brain, route);
    if (route.Quests.empty())
        return;
    uint32 const head = route.Quests.front();
    NewRpgInfo& info = brain->rpgInfo;
    auto const* doing = std::get_if<NewRpgInfo::DoQuest>(&info.data);
    if (doing && Ignored(bot, doing->questId))
    {
        info.ChangeToIdle();
        doing = nullptr;
    }
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
        info.ChangeToDoQuest(head, quest);
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

std::string PlanOf(Player* player, Player* bot, PlayerbotAI* brain)
{
    BotRoute& route = routes[bot->GetGUID().GetCounter()];
    if (bot->GetMap() == player->GetMap() && route.Log != LogOf(bot))
        Replan(bot, brain, route);
    NewRpgInfo& info = brain->rpgInfo;
    auto const* doing = std::get_if<NewRpgInfo::DoQuest>(&info.data);
    uint32 const doingId = doing ? doing->questId : 0;
    std::string plan = std::string(StatusWords(info.GetStatus())) + "|" + std::to_string(doingId) + ":" +
        (doingId ? TitleOf(doingId) : "") + "|";
    std::string separator;
    for (uint32 const questId : route.Quests)
    {
        plan += separator + std::to_string(questId) + ":" + TitleOf(questId);
        separator = ";";
    }
    plan += "|";
    separator.clear();
    for (auto const& [questId, reason] : route.Unplanned)
    {
        plan += separator + std::to_string(questId) + ":" + std::string(reason) + ":" + TitleOf(questId);
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
        if (Quest const* quest = sObjectMgr->GetQuestTemplate(questId))
            brain->rpgInfo.ChangeToDoQuest(questId, quest);
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
