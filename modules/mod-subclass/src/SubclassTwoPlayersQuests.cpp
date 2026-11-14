/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license.
 */

#include "Chat.h"
#include "LocalLevelScaling.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "PlayerScript.h"
#include "QuestDef.h"
#include "SubclassRules.h"
#include "WorldSession.h"
#include <mutex>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

uint32 TwoPlayersPartnerOf(uint32 guid);
bool TwoPlayersIsLeftHead(uint32 guid);
Player* TwoPlayersSeatedPartner(Player const* head);

namespace
{
constexpr uint32 QuestSyncTickMs = 1000;

struct ChosenReward
{
    uint32 Quest = 0;
    uint32 Reward = 0;
    ObjectGuid Giver;
};

std::mutex& questMutex = *new std::mutex;
std::unordered_map<uint32, uint32>& sinceSync = *new std::unordered_map<uint32, uint32>;
std::unordered_map<uint32, ChosenReward>& chosenRewards = *new std::unordered_map<uint32, ChosenReward>;
std::unordered_map<uint32, std::vector<std::pair<uint32, uint32>>>& rewardItems =
    *new std::unordered_map<uint32, std::vector<std::pair<uint32, uint32>>>;
std::set<std::pair<uint32, uint32>>& toldLogFull = *new std::set<std::pair<uint32, uint32>>;
std::set<std::pair<uint32, uint32>>& toldBagsFull = *new std::set<std::pair<uint32, uint32>>;
std::unordered_set<uint32>& turningInForPartner = *new std::unordered_set<uint32>;

bool RangedHead(Player const* player)
{
    uint32 const guid = player->GetGUID().GetCounter();
    return TwoPlayersPartnerOf(guid) && !TwoPlayersIsLeftHead(guid);
}

void Tell(Player* player, std::string const& text)
{
    if (player->GetSession() && !player->GetSession()->IsBot())
        ChatHandler(player->GetSession()).SendSysMessage(text);
}

bool InLog(Player const* player, uint32 questId)
{
    QuestStatus const status = player->GetQuestStatus(questId);
    return status == QUEST_STATUS_INCOMPLETE || status == QUEST_STATUS_COMPLETE;
}

void GiveMissingQuest(Player* taker, Player* holder, Quest const* quest)
{
    uint32 const questId = quest->GetQuestId();
    if (taker->GetQuestStatus(questId) != QUEST_STATUS_NONE || taker->IsQuestRewarded(questId))
        return;
    if (taker->FindQuestSlot(0) >= MAX_QUEST_LOG_SIZE)
    {
        bool fresh = false;
        {
            std::lock_guard<std::mutex> lock(questMutex);
            fresh = toldLogFull.emplace(taker->GetGUID().GetCounter(), questId).second;
        }
        if (fresh)
            Tell(taker, "Your quest log is full, so you could not share " + holder->GetName() + "'s quest \"" +
                quest->GetTitle() + "\".");
        return;
    }
    taker->AddQuest(quest, nullptr);
    if (taker->CanCompleteQuest(questId))
        taker->CompleteQuest(questId);
}

void CatchUpCounters(Player* behind, Player* ahead, Quest const* quest)
{
    uint32 const questId = quest->GetQuestId();
    QuestStatusData& own = behind->getQuestStatusMap()[questId];
    QuestStatusData const& partner = ahead->getQuestStatusMap()[questId];
    for (uint8 i = 0; i < QUEST_OBJECTIVES_COUNT; ++i)
    {
        if (!quest->RequiredNpcOrGo[i] || !quest->RequiredNpcOrGoCount[i])
            continue;
        uint16 const before = own.CreatureOrGOCount[i];
        uint16 const after = Subclass::QuestCounterCatchUp(before, partner.CreatureOrGOCount[i],
            uint16(quest->RequiredNpcOrGoCount[i]));
        if (after == before)
            continue;
        own.CreatureOrGOCount[i] = after;
        behind->GetQuestStatusSaveMap()[questId] = true;
        behind->SendQuestUpdateAddCreatureOrGo(quest, ObjectGuid::Empty, i, before, after - before);
    }
    if (partner.Explored && !own.Explored && quest->HasSpecialFlag(QUEST_SPECIAL_FLAGS_EXPLORATION_OR_EVENT))
        behind->AreaExploredOrEventHappens(questId);
}

bool HasQuestItems(Player const* player, Quest const* quest)
{
    for (uint8 i = 0; i < QUEST_ITEM_OBJECTIVES_COUNT; ++i)
        if (quest->RequiredItemId[i] && player->GetItemCount(quest->RequiredItemId[i], true) < quest->RequiredItemCount[i])
            return false;
    return true;
}

void CatchUpItems(Player* behind, Player* ahead, Quest const* quest)
{
    for (uint8 i = 0; i < QUEST_ITEM_OBJECTIVES_COUNT; ++i)
    {
        uint32 const itemId = quest->RequiredItemId[i];
        if (!itemId || !quest->RequiredItemCount[i])
            continue;
        uint32 const missing = Subclass::QuestItemTopUp(behind->GetItemCount(itemId, true),
            ahead->GetItemCount(itemId, true), quest->RequiredItemCount[i]);
        if (!missing)
            continue;
        ItemPosCountVec dest;
        if (behind->CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, itemId, missing) != EQUIP_ERR_OK)
        {
            bool fresh = false;
            {
                std::lock_guard<std::mutex> lock(questMutex);
                fresh = toldBagsFull.emplace(behind->GetGUID().GetCounter(), itemId).second;
            }
            ItemTemplate const* proto = sObjectMgr->GetItemTemplate(itemId);
            if (fresh && proto)
                for (Player* head : { behind, ahead })
                    Tell(head, behind->GetName() + " has no room for " + proto->Name1 + ", so it could not be "
                        "shared for \"" + quest->GetTitle() + "\". Make room in " + behind->GetName() + "'s bags.");
            continue;
        }
        if (Item* item = behind->StoreNewItem(dest, itemId, true))
            behind->SendNewItem(item, missing, true, false);
    }
}

void CatchUp(Player* behind, Player* ahead, Quest const* quest)
{
    uint32 const questId = quest->GetQuestId();
    if (behind->GetQuestStatus(questId) != QUEST_STATUS_INCOMPLETE)
        return;
    CatchUpCounters(behind, ahead, quest);
    CatchUpItems(behind, ahead, quest);
    if (behind->GetQuestStatus(questId) != QUEST_STATUS_INCOMPLETE)
        return;
    if (behind->CanCompleteQuest(questId) ||
        (ahead->GetQuestStatus(questId) == QUEST_STATUS_COMPLETE && HasQuestItems(behind, quest)))
        behind->CompleteQuest(questId);
}

std::vector<uint32> LogOf(Player const* player)
{
    std::vector<uint32> quests;
    for (uint16 slot = 0; slot < MAX_QUEST_LOG_SIZE; ++slot)
        if (uint32 const questId = player->GetQuestSlotQuestId(slot))
            quests.push_back(questId);
    return quests;
}

void SyncQuests(Player* first, Player* second)
{
    for (auto const& [holder, taker] : { std::make_pair(first, second), std::make_pair(second, first) })
        for (uint32 const questId : LogOf(holder))
            if (Quest const* quest = sObjectMgr->GetQuestTemplate(questId))
            {
                if (!InLog(holder, questId))
                    continue;
                GiveMissingQuest(taker, holder, quest);
                if (InLog(taker, questId))
                    CatchUp(taker, holder, quest);
            }
}

void DropQuest(Player* player, uint32 questId)
{
    uint16 const slot = player->FindQuestSlot(questId);
    if (slot >= MAX_QUEST_LOG_SIZE || !player->TakeQuestSourceItem(questId, false))
        return;
    if (Quest const* quest = sObjectMgr->GetQuestTemplate(questId))
    {
        if (quest->HasSpecialFlag(QUEST_SPECIAL_FLAGS_TIMED))
            player->RemoveTimedQuest(questId);
        if (quest->HasFlag(QUEST_FLAGS_FLAGS_PVP))
        {
            player->pvpInfo.IsHostile = player->pvpInfo.IsInHostileArea || player->HasPvPForcingQuest();
            player->UpdatePvPState();
        }
    }
    player->AbandonQuest(questId);
    player->RemoveActiveQuest(questId);
    player->RemoveTimedAchievement(ACHIEVEMENT_TIMED_TYPE_QUEST, questId);
}

uint32 RewardChoiceOf(Player const* head, Quest const* quest)
{
    uint32 const guid = head->GetGUID().GetCounter();
    std::lock_guard<std::mutex> lock(questMutex);
    auto const chosen = chosenRewards.find(guid);
    if (chosen != chosenRewards.end() && chosen->second.Quest == quest->GetQuestId())
        return chosen->second.Reward;
    auto const received = rewardItems.find(guid);
    if (received == rewardItems.end())
        return 0;
    for (uint32 i = 0; i < quest->GetRewChoiceItemsCount() && i < QUEST_REWARD_CHOICES_COUNT; ++i)
        for (auto const& [itemId, count] : received->second)
            if (itemId == LocalLevelScaling::QuestRewardItemFor(head, quest->RewardChoiceItemId[i], quest))
                return i;
    return 0;
}

Object* GiverFor(Player* partner, Player* head, Quest const* quest)
{
    ObjectGuid giver;
    {
        std::lock_guard<std::mutex> lock(questMutex);
        auto const chosen = chosenRewards.find(head->GetGUID().GetCounter());
        if (chosen != chosenRewards.end() && chosen->second.Quest == quest->GetQuestId())
            giver = chosen->second.Giver;
    }
    if (giver)
        if (Object* object = ObjectAccessor::GetObjectByTypeMask(*partner, giver, TYPEMASK_UNIT | TYPEMASK_GAMEOBJECT))
            return object;
    return head;
}

uint64 WorthOfRewardItems(uint32 guid)
{
    std::lock_guard<std::mutex> lock(questMutex);
    auto const received = rewardItems.find(guid);
    if (received == rewardItems.end())
        return 0;
    uint64 worth = 0;
    for (auto const& [itemId, count] : received->second)
        if (ItemTemplate const* item = sObjectMgr->GetItemTemplate(itemId))
            worth += uint64(item->SellPrice) * count;
    return worth;
}

void Forget(uint32 guid)
{
    std::lock_guard<std::mutex> lock(questMutex);
    chosenRewards.erase(guid);
    rewardItems.erase(guid);
}

class mod_subclass_two_players_quests_player : public PlayerScript
{
public:
    mod_subclass_two_players_quests_player() : PlayerScript("mod_subclass_two_players_quests_player",
        { PLAYERHOOK_ON_UPDATE, PLAYERHOOK_ON_QUEST_ABANDON, PLAYERHOOK_ON_QUEST_GIVER_CHOOSE_REWARD,
          PLAYERHOOK_ON_QUEST_REWARD_ITEM, PLAYERHOOK_ON_LOGOUT })
    {
    }

    void OnPlayerUpdate(Player* player, uint32 diff) override
    {
        uint32 const guid = player->GetGUID().GetCounter();
        {
            std::lock_guard<std::mutex> lock(questMutex);
            uint32& since = sinceSync[guid];
            since += diff;
            if (since < QuestSyncTickMs)
                return;
            since = 0;
        }
        if (!TwoPlayersPartnerOf(guid) || !TwoPlayersIsLeftHead(guid))
            return;
        if (Player* rider = TwoPlayersSeatedPartner(player))
            SyncQuests(player, rider);
    }

    void OnPlayerQuestAbandon(Player* player, uint32 questId) override
    {
        if (Player* partner = TwoPlayersSeatedPartner(player))
            if (InLog(partner, questId))
            {
                DropQuest(partner, questId);
                Tell(partner, player->GetName() + " abandoned a quest, so it is gone from your log too.");
            }
    }

    bool OnPlayerQuestGiverChooseReward(Player* player, Object* questGiver, Quest const* quest, uint32 reward) override
    {
        if (TwoPlayersPartnerOf(player->GetGUID().GetCounter()))
        {
            std::lock_guard<std::mutex> lock(questMutex);
            chosenRewards[player->GetGUID().GetCounter()] = { quest->GetQuestId(), reward, questGiver->GetGUID() };
        }
        return false;
    }

    void OnPlayerQuestRewardItem(Player* player, Item* item, uint32 count) override
    {
        uint32 const guid = player->GetGUID().GetCounter();
        if (!TwoPlayersPartnerOf(guid) || !item)
            return;
        uint32 const itemId = item->GetEntry();
        {
            std::lock_guard<std::mutex> lock(questMutex);
            rewardItems[guid].emplace_back(itemId, count);
        }
        if (RangedHead(player))
            player->DestroyItemCount(itemId, count, true);
    }

    void OnPlayerLogout(Player* player) override
    {
        uint32 const guid = player->GetGUID().GetCounter();
        std::lock_guard<std::mutex> lock(questMutex);
        sinceSync.erase(guid);
        chosenRewards.erase(guid);
        rewardItems.erase(guid);
        turningInForPartner.erase(guid);
        for (auto it = toldLogFull.begin(); it != toldLogFull.end();)
            it = it->first == guid ? toldLogFull.erase(it) : std::next(it);
        for (auto it = toldBagsFull.begin(); it != toldBagsFull.end();)
            it = it->first == guid ? toldBagsFull.erase(it) : std::next(it);
    }
};
}

void TwoPlayersQuestRewarded(Player* player, Quest const* quest)
{
    uint32 const guid = player->GetGUID().GetCounter();
    if (!TwoPlayersPartnerOf(guid))
        return;
    uint32 const questId = quest->GetQuestId();
    bool forPartner = false;
    {
        std::lock_guard<std::mutex> lock(questMutex);
        forPartner = turningInForPartner.count(guid) != 0;
    }
    Player* partner = TwoPlayersSeatedPartner(player);
    if (!forPartner && partner && InLog(partner, questId))
    {
        uint32 const partnerGuid = partner->GetGUID().GetCounter();
        if (partner->GetQuestStatus(questId) == QUEST_STATUS_INCOMPLETE)
            partner->CompleteQuest(questId);
        {
            std::lock_guard<std::mutex> lock(questMutex);
            turningInForPartner.insert(partnerGuid);
            rewardItems.erase(partnerGuid);
        }
        partner->RewardQuest(quest, RewardChoiceOf(player, quest), GiverFor(partner, player, quest));
        {
            std::lock_guard<std::mutex> lock(questMutex);
            turningInForPartner.erase(partnerGuid);
        }
        Forget(partnerGuid);
    }
    if (RangedHead(player))
        if (uint64 const worth = WorthOfRewardItems(guid))
        {
            player->ModifyMoney(int32(std::min<uint64>(worth, uint64(MAX_MONEY_AMOUNT))));
            Tell(player, "The quest's item reward goes to your Melee Head; you get its worth in gold.");
        }
    Forget(guid);
}

void AddSC_mod_subclass_two_players_quests()
{
    new mod_subclass_two_players_quests_player();
}
