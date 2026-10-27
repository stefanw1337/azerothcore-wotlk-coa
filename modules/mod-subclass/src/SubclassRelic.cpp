/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license.
 */

#include "Chat.h"
#include "DatabaseEnv.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "PlayerScript.h"
#include "StringConvert.h"
#include "StringFormat.h"
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

bool SubclassChosen(Player const* player);

namespace
{
constexpr uint32 RelicTick = 1000;

struct HeldRelic
{
    uint32 Entry = 0;
    bool Loaded = false;
    bool Applied = false;
    uint32 Tick = 0;
};

std::mutex& relicMutex = *new std::mutex;
std::unordered_map<uint32, HeldRelic>& heldRelics = *new std::unordered_map<uint32, HeldRelic>;

bool UsesRelics(Player const* player)
{
    switch (player->getClass())
    {
        case CLASS_PALADIN:
        case CLASS_SHAMAN:
        case CLASS_DRUID:
        case CLASS_DEATH_KNIGHT:
            return true;
        default:
            return false;
    }
}

bool MayHoldRelic(Player const* player)
{
    return UsesRelics(player) && SubclassChosen(player);
}

HeldRelic RelicOf(Player const* player)
{
    uint32 const guid = player->GetGUID().GetCounter();
    {
        std::lock_guard<std::mutex> lock(relicMutex);
        if (auto itr = heldRelics.find(guid); itr != heldRelics.end() && itr->second.Loaded)
            return itr->second;
    }
    HeldRelic held;
    held.Loaded = true;
    if (QueryResult result = CharacterDatabase.Query("SELECT `item` FROM `mod_subclass_relic` WHERE `guid` = {}", guid))
        held.Entry = result->Fetch()[0].Get<uint32>();
    std::lock_guard<std::mutex> lock(relicMutex);
    HeldRelic& stored = heldRelics[guid];
    stored.Entry = held.Entry;
    stored.Loaded = true;
    return stored;
}

void StoreRelic(Player const* player, uint32 entry)
{
    uint32 const guid = player->GetGUID().GetCounter();
    if (entry)
        CharacterDatabase.Execute("REPLACE INTO `mod_subclass_relic` (`guid`, `item`) VALUES ({}, {})", guid, entry);
    else
        CharacterDatabase.Execute("DELETE FROM `mod_subclass_relic` WHERE `guid` = {}", guid);
    std::lock_guard<std::mutex> lock(relicMutex);
    HeldRelic& stored = heldRelics[guid];
    stored.Entry = entry;
    stored.Loaded = true;
}

void SetApplied(Player const* player, bool applied)
{
    std::lock_guard<std::mutex> lock(relicMutex);
    heldRelics[player->GetGUID().GetCounter()].Applied = applied;
}

void ApplyRelic(Player* player, uint32 entry, bool apply)
{
    ItemTemplate const* proto = sObjectMgr->GetItemTemplate(entry);
    if (!proto)
        return;
    player->_ApplyItemBonuses(proto, EQUIPMENT_SLOT_RANGED, apply);
    for (_Spell const& spell : proto->Spells)
        if (spell.SpellId > 0 && spell.SpellTrigger == ITEM_SPELLTRIGGER_ON_EQUIP)
        {
            if (apply)
                player->CastSpell(player, uint32(spell.SpellId), true);
            else
                player->RemoveAurasDueToSpell(uint32(spell.SpellId));
        }
    SetApplied(player, apply);
}

void RefreshRelic(Player* player)
{
    HeldRelic const held = RelicOf(player);
    bool const wanted = held.Entry && MayHoldRelic(player) && player->IsAlive();
    if (wanted != held.Applied && held.Entry)
        ApplyRelic(player, held.Entry, wanted);
}

bool FitsRelicHolder(Player const* player, ItemTemplate const* proto)
{
    return proto && proto->InventoryType == INVTYPE_RELIC && player->CanUseItem(proto) == EQUIP_ERR_OK;
}

std::string GiveBack(Player* player, uint32 entry)
{
    ItemPosCountVec destination;
    if (player->CanStoreNewItem(NULL_BAG, NULL_SLOT, destination, entry, 1) != EQUIP_ERR_OK)
        return "Your bags are full. Make room before taking the relic off.";
    HeldRelic const held = RelicOf(player);
    if (held.Applied)
        ApplyRelic(player, entry, false);
    StoreRelic(player, 0);
    if (Item* item = player->StoreNewItem(destination, entry, true))
        player->SendNewItem(item, 1, true, false);
    return "";
}

std::string WearRelic(Player* player, uint8 bag, uint8 slot)
{
    if (!MayHoldRelic(player))
        return "Only a subclassed Paladin, Shaman, Druid or Death Knight can hold a relic beside a ranged weapon.";
    Item* item = player->GetItemByPos(bag, slot);
    if (!item || item->IsEquipped() || !FitsRelicHolder(player, item->GetTemplate()))
        return "That is not a relic you can use.";
    uint32 const entry = item->GetEntry();
    HeldRelic const held = RelicOf(player);
    if (held.Entry)
    {
        std::string const refusal = GiveBack(player, held.Entry);
        if (!refusal.empty())
            return refusal;
    }
    player->DestroyItem(bag, slot, true);
    StoreRelic(player, entry);
    RefreshRelic(player);
    return "";
}

std::string UseRelic(Player* player)
{
    HeldRelic const held = RelicOf(player);
    ItemTemplate const* proto = held.Entry && held.Applied ? sObjectMgr->GetItemTemplate(held.Entry) : nullptr;
    if (!proto)
        return "";
    Unit* target = player->GetSelectedUnit();
    for (_Spell const& spell : proto->Spells)
        if (spell.SpellId > 0 && spell.SpellTrigger == ITEM_SPELLTRIGGER_ON_USE)
            player->CastSpell(target ? target : player, uint32(spell.SpellId), false);
    return "";
}
}

std::string RelicState(Player* player)
{
    HeldRelic const held = RelicOf(player);
    return Acore::StringFormat("RELIC {} {}", MayHoldRelic(player) ? 1 : 0, held.Entry);
}

std::string HandleRelicMessage(Player* player, std::vector<std::string_view> const& words)
{
    if (words.size() == 3 && words[0] == "RELICWEAR")
    {
        Optional<uint8> const bag = Acore::StringTo<uint8>(words[1]);
        Optional<uint8> const slot = Acore::StringTo<uint8>(words[2]);
        return bag && slot ? WearRelic(player, *bag, *slot) : "";
    }
    if (words.size() == 1 && words[0] == "RELICTAKE")
    {
        HeldRelic const held = RelicOf(player);
        return held.Entry ? GiveBack(player, held.Entry) : "";
    }
    if (words.size() == 1 && words[0] == "RELICUSE")
        return UseRelic(player);
    return "";
}

class mod_subclass_relic_player : public PlayerScript
{
public:
    mod_subclass_relic_player() : PlayerScript("mod_subclass_relic_player", { PLAYERHOOK_ON_LOGIN,
        PLAYERHOOK_ON_LOGOUT, PLAYERHOOK_ON_UPDATE, PLAYERHOOK_ON_CREATE, PLAYERHOOK_ON_DELETE })
    {
    }

    void OnPlayerLogin(Player* player) override
    {
        RefreshRelic(player);
    }

    void OnPlayerUpdate(Player* player, uint32 diff) override
    {
        {
            std::lock_guard<std::mutex> lock(relicMutex);
            uint32& tick = heldRelics[player->GetGUID().GetCounter()].Tick;
            tick += diff;
            if (tick < RelicTick)
                return;
            tick = 0;
        }
        RefreshRelic(player);
    }

    void OnPlayerLogout(Player* player) override
    {
        std::lock_guard<std::mutex> lock(relicMutex);
        heldRelics.erase(player->GetGUID().GetCounter());
    }

    void OnPlayerCreate(Player* player) override
    {
        ForgetRelicOf(player->GetGUID().GetCounter());
    }

    void OnPlayerDelete(ObjectGuid guid, uint32 /*accountId*/) override
    {
        ForgetRelicOf(guid.GetCounter());
    }

private:
    static void ForgetRelicOf(uint32 guid)
    {
        CharacterDatabase.DirectExecute("DELETE FROM `mod_subclass_relic` WHERE `guid` = {}", guid);
        std::lock_guard<std::mutex> lock(relicMutex);
        heldRelics.erase(guid);
    }
};

void AddSC_mod_subclass_relic()
{
    new mod_subclass_relic_player();
}
