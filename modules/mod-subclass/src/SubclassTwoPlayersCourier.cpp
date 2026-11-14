/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license.
 */

#include "Bag.h"
#include "Chat.h"
#include "Config.h"
#include "Item.h"
#include "Player.h"
#include "PlayerScript.h"
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

Player* TwoPlayersSeatedPartner(Player const* head);
bool TwoPlayersHeadIsBot(Player const* head);

namespace
{
constexpr uint32 AutoCheckMs = 5000;

struct Payout
{
    uint32 Due = 0;
    uint32 Money = 0;
    uint32 Items = 0;
};

std::mutex& courierMutex = *new std::mutex;
std::unordered_map<uint32, std::vector<Payout>>& payouts = *new std::unordered_map<uint32, std::vector<Payout>>;
std::unordered_map<uint32, uint32>& sinceAutoCheck = *new std::unordered_map<uint32, uint32>;

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

bool IsJunk(Item const* item)
{
    ItemTemplate const* proto = item ? item->GetTemplate() : nullptr;
    return proto && proto->Quality == ITEM_QUALITY_POOR && proto->SellPrice > 0;
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

bool HasJunk(Player* player)
{
    bool found = false;
    ForEachCarried(player, [&found](uint8, uint8, Item* item) { found = found || IsJunk(item); });
    return found;
}

bool CourierOut(Player* player)
{
    std::lock_guard<std::mutex> lock(courierMutex);
    auto const found = payouts.find(player->GetGUID().GetCounter());
    return found != payouts.end() && !found->second.empty();
}

Payout HandJunkToCourier(Player* player)
{
    std::vector<std::pair<uint8, uint8>> junk;
    Payout payout;
    ForEachCarried(player, [&](uint8 bag, uint8 slot, Item* item)
    {
        if (!IsJunk(item))
            return;
        junk.emplace_back(bag, slot);
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

std::string CourierLeaves(Player* owner, Payout const& payout)
{
    return owner->GetName() + " hands " + std::to_string(payout.Items) + " junk item" + (payout.Items == 1 ? "" : "s") +
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
        std::to_string(items) + " junk item" + (items == 1 ? "" : "s") + ".";
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
    for (Player* head : { presser, partner })
    {
        if (!head)
            continue;
        Payout const payout = HandJunkToCourier(head);
        if (payout.Items)
            told += (told.empty() ? "" : " ") + CourierLeaves(head, payout);
    }
    if (told.empty())
        return "No junk to send off.";
    Tell(presser, told);
    if (partner && !TwoPlayersHeadIsBot(partner))
        Tell(partner, told);
    return "";
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
            CourierOut(player) || !HasJunk(player))
            return;
        Player* rider = TwoPlayersSeatedPartner(player);
        if (!rider)
            return;
        Payout const payout = HandJunkToCourier(player);
        if (payout.Items)
            Tell(rider, "Bags full. " + CourierLeaves(player, payout));
    }

    void OnPlayerLogout(Player* player) override
    {
        std::lock_guard<std::mutex> lock(courierMutex);
        sinceAutoCheck.erase(player->GetGUID().GetCounter());
    }
};

void AddSC_mod_subclass_two_players_courier()
{
    new mod_subclass_two_players_courier();
}
