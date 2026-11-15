/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license.
 */

#include "Bag.h"
#include "Chat.h"
#include "Config.h"
#include "DatabaseEnv.h"
#include "Item.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "PlayerScript.h"
#include "QuestDef.h"
#include "WorldScript.h"
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

Player* TwoPlayersSeatedPartner(Player const* head);
bool TwoPlayersHeadIsBot(Player const* head);
void TwoPlayersSendAddon(Player* player, std::string const& message);
void TwoPlayersCourierList(Player* presser);

namespace
{
constexpr uint32 AutoCheckMs = 5000;
constexpr uint32 QualityMarkBase = 0xFFFFFF00;

struct Payout
{
    uint32 Due = 0;
    uint32 Money = 0;
    uint32 Items = 0;
    uint32 Leftovers = 0;
};

enum class Load : char
{
    None = 0,
    Junk = 'J',
    Marked = 'M',
    Leftover = 'Q',
    Markable = 'C'
};

using Marks = std::set<uint32>;

std::mutex& courierMutex = *new std::mutex;
std::unordered_map<uint32, std::vector<Payout>>& payouts = *new std::unordered_map<uint32, std::vector<Payout>>;
std::unordered_map<uint32, uint32>& sinceAutoCheck = *new std::unordered_map<uint32, uint32>;
std::unordered_map<uint32, Marks>& marks = *new std::unordered_map<uint32, Marks>;
std::unordered_map<uint32, std::vector<uint32>>& questsNeeding = *new std::unordered_map<uint32, std::vector<uint32>>;

uint32 CourierMs()
{
    return sConfigMgr->GetOption<uint32>("Subclass.TwoPlayers.CourierMinutes", 3) * MINUTE * IN_MILLISECONDS;
}

void Tell(Player* player, std::string const& text)
{
    if (player && player->GetSession())
        ChatHandler(player->GetSession()).SendSysMessage(text);
}

std::string Coins(uint32 money)
{
    std::string text;
    if (uint32 const gold = money / GOLD)
        text += std::to_string(gold) + "g ";
    if (uint32 const silver = (money % GOLD) / SILVER)
        text += std::to_string(silver) + "s ";
    return text + std::to_string(money % SILVER) + "c";
}

Marks MarksOf(Player const* first, Player const* second)
{
    std::lock_guard<std::mutex> lock(courierMutex);
    Marks both;
    for (Player const* head : { first, second })
        if (head)
            if (auto const found = marks.find(head->GetGUID().GetCounter()); found != marks.end())
                both.insert(found->second.begin(), found->second.end());
    return both;
}

bool QuestStillNeeds(Player const* holder, uint32 entry)
{
    auto const found = questsNeeding.find(entry);
    if (found == questsNeeding.end())
        return true;
    for (uint32 questId : found->second)
        if (!holder->GetQuestRewardStatus(questId))
            return true;
    return false;
}

Load LoadOf(Player const* holder, Item const* item, Marks const& marked)
{
    ItemTemplate const* proto = item ? item->GetTemplate() : nullptr;
    if (!proto)
        return Load::None;
    if (proto->Bonding == BIND_QUEST_ITEM)
        return !proto->StartQuest && !QuestStillNeeds(holder, proto->ItemId) ? Load::Leftover : Load::None;
    if (!proto->SellPrice)
        return Load::None;
    if (proto->Quality == ITEM_QUALITY_POOR)
        return Load::Junk;
    return marked.count(proto->ItemId) || marked.count(QualityMarkBase + proto->Quality) ? Load::Marked : Load::None;
}

template <typename Visit>
void ForEachCarried(Player* player, Visit visit)
{
    for (uint8 slot = INVENTORY_SLOT_ITEM_START; slot < INVENTORY_SLOT_ITEM_END; ++slot)
        if (Item* item = player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
            visit(INVENTORY_SLOT_BAG_0, slot, item);
    for (uint8 bagSlot = INVENTORY_SLOT_BAG_START; bagSlot < INVENTORY_SLOT_BAG_END; ++bagSlot)
        if (Bag* bag = player->GetBagByPos(bagSlot))
            for (uint32 slot = 0; slot < bag->GetBagSize(); ++slot)
                if (Item* item = bag->GetItemByPos(uint8(slot)))
                    visit(bagSlot, uint8(slot), item);
}

bool HasLoad(Player* player, Marks const& marked)
{
    bool found = false;
    ForEachCarried(player, [&](uint8, uint8, Item* item)
    {
        found = found || LoadOf(player, item, marked) != Load::None;
    });
    return found;
}

bool CourierOut(Player* player)
{
    std::lock_guard<std::mutex> lock(courierMutex);
    auto const found = payouts.find(player->GetGUID().GetCounter());
    return found != payouts.end() && !found->second.empty();
}

template <typename Pick>
Payout HandToCourier(Player* player, Pick pick)
{
    std::vector<std::pair<uint8, uint8>> junk;
    Payout payout;
    ForEachCarried(player, [&](uint8 bag, uint8 slot, Item* item)
    {
        Load const load = pick(item);
        if (load == Load::None)
            return;
        junk.emplace_back(bag, slot);
        if (load == Load::Leftover)
        {
            payout.Leftovers += item->GetCount();
            return;
        }
        payout.Money += item->GetTemplate()->SellPrice * item->GetCount();
        payout.Items += item->GetCount();
    });
    for (auto const& [bag, slot] : junk)
        player->DestroyItem(bag, slot, true);
    if (!payout.Items)
        return payout;
    payout.Due = getMSTime();
    std::lock_guard<std::mutex> lock(courierMutex);
    payouts[player->GetGUID().GetCounter()].push_back(payout);
    return payout;
}

Payout HandJunkToCourier(Player* player, Marks const& marked)
{
    return HandToCourier(player, [&](Item const* item) { return LoadOf(player, item, marked); });
}

std::string Count(uint32 count, std::string const& what)
{
    return std::to_string(count) + " " + what + (count == 1 ? "" : "s");
}

std::string CourierLeaves(Player* owner, Payout const& payout)
{
    std::string text;
    if (payout.Leftovers)
        text = owner->GetName() + " throws away " + Count(payout.Leftovers, "leftover quest item") + ".";
    if (!payout.Items)
        return text;
    return text + (text.empty() ? "" : " ") + owner->GetName() + " hands " + Count(payout.Items, "item") +
        " to a courier. Back in " + std::to_string(CourierMs() / (MINUTE * IN_MILLISECONDS)) + " minutes with " +
        Coins(payout.Money) + ".";
}

void PayWhatIsDue(Player* player)
{
    uint32 money = 0;
    uint32 items = 0;
    {
        std::lock_guard<std::mutex> lock(courierMutex);
        auto const found = payouts.find(player->GetGUID().GetCounter());
        if (found == payouts.end())
            return;
        uint32 const now = getMSTime();
        auto& due = found->second;
        for (auto it = due.begin(); it != due.end();)
        {
            if (getMSTimeDiff(it->Due, now) < CourierMs())
            {
                ++it;
                continue;
            }
            money += it->Money;
            items += it->Items;
            it = due.erase(it);
        }
        if (due.empty())
            payouts.erase(found);
    }
    if (!items)
        return;
    player->ModifyMoney(int32(money));
    std::string const line = "The courier is back: " + Coins(money) + " for " + player->GetName() + "'s " +
        Count(items, "item") + ".";
    Tell(player, line);
    if (TwoPlayersHeadIsBot(player))
        Tell(TwoPlayersSeatedPartner(player), line);
}
}

std::string TwoPlayersSendCourier(Player* presser)
{
    Player* partner = TwoPlayersSeatedPartner(presser);
    if (!partner)
        return "The courier only serves the two heads of one body.";
    std::string told;
    Marks const marked = MarksOf(presser, partner);
    for (Player* head : { presser, partner })
    {
        Payout const payout = HandJunkToCourier(head, marked);
        if (payout.Items || payout.Leftovers)
            told += (told.empty() ? "" : " ") + CourierLeaves(head, payout);
    }
    if (told.empty())
        return "Nothing for the courier: no grey, marked or leftover quest items.";
    Tell(presser, told);
    if (partner && !TwoPlayersHeadIsBot(partner))
        Tell(partner, told);
    return "";
}

void TwoPlayersCourierSellOne(Player* presser, std::string_view owner, uint32 entry)
{
    Player* partner = TwoPlayersSeatedPartner(presser);
    if (!partner)
    {
        Tell(presser, "The courier only serves the two heads of one body.");
        return;
    }
    Player* head = partner->GetName() == owner ? partner : presser;
    bool taken = false;
    Payout const payout = HandToCourier(head, [&](Item const* item)
    {
        ItemTemplate const* proto = item->GetTemplate();
        if (taken || item->GetEntry() != entry || !proto->SellPrice || proto->Bonding == BIND_QUEST_ITEM)
            return Load::None;
        taken = true;
        return Load::Marked;
    });
    std::string const told = taken ? CourierLeaves(head, payout) :
        head->GetName() + " carries no sellable item of that kind.";
    Tell(presser, told);
    if (!TwoPlayersHeadIsBot(partner) && taken)
        Tell(partner, told);
    TwoPlayersCourierList(presser);
}

void TwoPlayersCourierList(Player* presser)
{
    Player* partner = TwoPlayersSeatedPartner(presser);
    Marks const marked = MarksOf(presser, partner);
    std::map<std::tuple<std::string, char, uint32>, std::pair<uint32, uint32>> loads;
    uint32 worth = 0;
    for (Player* head : { presser, partner })
    {
        if (!head)
            continue;
        ForEachCarried(head, [&](uint8, uint8, Item* item)
        {
            Load load = LoadOf(head, item, marked);
            ItemTemplate const* proto = item->GetTemplate();
            if (load == Load::None && proto->SellPrice && proto->Bonding != BIND_QUEST_ITEM)
                load = Load::Markable;
            if (load == Load::None)
                return;
            auto& [count, money] = loads[{ head->GetName(), char(load), item->GetEntry() }];
            count += item->GetCount();
            if (load != Load::Leftover)
                money += proto->SellPrice * item->GetCount();
        });
    }
    for (auto const& [key, value] : loads)
    {
        auto const& [owner, load, entry] = key;
        if (load != char(Load::Markable))
            worth += value.second;
        TwoPlayersSendAddon(presser, "COURIER " + std::string(1, load) + " " + std::to_string(entry) + " " +
            std::to_string(value.first) + " " + std::to_string(value.second) + " " + owner);
    }
    for (uint32 entry : marked)
        TwoPlayersSendAddon(presser, "COURIERMARKED " + std::to_string(entry));
    TwoPlayersSendAddon(presser, "COURIEREND " + std::to_string(worth) + (partner ? "" : " ALONE"));
}

void TwoPlayersCourierMark(Player* presser, uint32 entry, bool mark)
{
    bool const quality = entry >= QualityMarkBase;
    if (quality && (entry - QualityMarkBase < ITEM_QUALITY_NORMAL || entry - QualityMarkBase > ITEM_QUALITY_EPIC))
        return;
    ItemTemplate const* proto = quality ? nullptr : sObjectMgr->GetItemTemplate(entry);
    if (!quality && !proto)
        return;
    if (proto && mark && (!proto->SellPrice || proto->Bonding == BIND_QUEST_ITEM))
    {
        Tell(presser, proto->Name1 + " cannot be sold, so the courier will not take it.");
        TwoPlayersCourierList(presser);
        return;
    }
    Player* partner = TwoPlayersSeatedPartner(presser);
    {
        std::lock_guard<std::mutex> lock(courierMutex);
        for (Player* head : { presser, partner })
        {
            if (!head || (mark && head != presser))
                continue;
            uint32 const guid = head->GetGUID().GetCounter();
            if (mark)
                marks[guid].insert(entry);
            else if (auto const found = marks.find(guid); found != marks.end())
                found->second.erase(entry);
        }
    }
    if (mark)
        CharacterDatabase.Execute("INSERT IGNORE INTO `mod_subclass_courier_mark` (`guid`, `item`) VALUES ({}, {})",
            presser->GetGUID().GetCounter(), entry);
    else
        for (Player* head : { presser, partner })
            if (head)
                CharacterDatabase.Execute("DELETE FROM `mod_subclass_courier_mark` WHERE `guid` = {} AND `item` = {}",
                    head->GetGUID().GetCounter(), entry);
    TwoPlayersCourierList(presser);
}

class mod_subclass_two_players_courier : public PlayerScript
{
public:
    mod_subclass_two_players_courier() : PlayerScript("mod_subclass_two_players_courier",
        { PLAYERHOOK_ON_UPDATE, PLAYERHOOK_ON_LOGOUT })
    {
    }

    void OnPlayerUpdate(Player* player, uint32 diff) override
    {
        uint32 const guid = player->GetGUID().GetCounter();
        {
            std::lock_guard<std::mutex> lock(courierMutex);
            uint32& since = sinceAutoCheck[guid];
            since += diff;
            if (since < AutoCheckMs)
                return;
            since = 0;
        }
        PayWhatIsDue(player);
        if (!TwoPlayersHeadIsBot(player) || player->IsInCombat() || player->GetFreeInventorySpace() ||
            CourierOut(player))
            return;
        Player* rider = TwoPlayersSeatedPartner(player);
        if (!rider)
            return;
        Marks const marked = MarksOf(player, rider);
        if (!HasLoad(player, marked))
            return;
        Payout const payout = HandJunkToCourier(player, marked);
        if (payout.Items || payout.Leftovers)
            Tell(rider, "Bags full. " + CourierLeaves(player, payout));
    }

    void OnPlayerLogout(Player* player) override
    {
        std::lock_guard<std::mutex> lock(courierMutex);
        sinceAutoCheck.erase(player->GetGUID().GetCounter());
    }
};

class mod_subclass_two_players_courier_world : public WorldScript
{
public:
    mod_subclass_two_players_courier_world() : WorldScript("mod_subclass_two_players_courier_world",
        { WORLDHOOK_ON_STARTUP })
    {
    }

    void OnStartup() override
    {
        CharacterDatabase.DirectExecute("CREATE TABLE IF NOT EXISTS `mod_subclass_courier_mark` (`guid` INT UNSIGNED "
            "NOT NULL, `item` INT UNSIGNED NOT NULL, PRIMARY KEY (`guid`, `item`)) ENGINE=InnoDB "
            "DEFAULT CHARSET=utf8mb4");
        std::unordered_map<uint32, Marks> loaded;
        if (QueryResult result = CharacterDatabase.Query("SELECT `guid`, `item` FROM `mod_subclass_courier_mark`"))
            do
            {
                Field* fields = result->Fetch();
                loaded[fields[0].Get<uint32>()].insert(fields[1].Get<uint32>());
            } while (result->NextRow());
        std::unordered_map<uint32, std::vector<uint32>> needing;
        for (auto const& [questId, quest] : sObjectMgr->GetQuestTemplates())
        {
            std::set<uint32> items(std::begin(quest->RequiredItemId), std::end(quest->RequiredItemId));
            items.insert(std::begin(quest->ItemDrop), std::end(quest->ItemDrop));
            items.insert(quest->GetSrcItemId());
            items.erase(0);
            for (uint32 entry : items)
                needing[entry].push_back(questId);
        }
        std::lock_guard<std::mutex> lock(courierMutex);
        marks = std::move(loaded);
        questsNeeding = std::move(needing);
    }
};

void AddSC_mod_subclass_two_players_courier()
{
    new mod_subclass_two_players_courier();
    new mod_subclass_two_players_courier_world();
}
