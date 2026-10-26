#include "Chat.h"
#include "ChatCommand.h"
#include "CommandScript.h"
#include "DatabaseEnv.h"
#include "Player.h"
#include "PlayerScript.h"
#include "ScriptMgr.h"
#include "StringConvert.h"
#include "Tokenize.h"
#include <mutex>
#include <string>
#include <unordered_map>

using namespace Acore::ChatCommands;

namespace
{
    struct HideSlot
    {
        char const* name;
        uint8 slot;
    };

    constexpr HideSlot HideSlots[] =
    {
        {"head", EQUIPMENT_SLOT_HEAD}, {"helm", EQUIPMENT_SLOT_HEAD},
        {"shoulder", EQUIPMENT_SLOT_SHOULDERS}, {"shoulders", EQUIPMENT_SLOT_SHOULDERS},
        {"shirt", EQUIPMENT_SLOT_BODY},
        {"chest", EQUIPMENT_SLOT_CHEST},
        {"waist", EQUIPMENT_SLOT_WAIST}, {"belt", EQUIPMENT_SLOT_WAIST},
        {"pants", EQUIPMENT_SLOT_LEGS}, {"legs", EQUIPMENT_SLOT_LEGS},
        {"boots", EQUIPMENT_SLOT_FEET}, {"feet", EQUIPMENT_SLOT_FEET},
        {"wrist", EQUIPMENT_SLOT_WRISTS}, {"bracers", EQUIPMENT_SLOT_WRISTS},
        {"gloves", EQUIPMENT_SLOT_HANDS}, {"hands", EQUIPMENT_SLOT_HANDS},
        {"cloak", EQUIPMENT_SLOT_BACK}, {"back", EQUIPMENT_SLOT_BACK},
        {"tabard", EQUIPMENT_SLOT_TABARD},
    };

    constexpr char const* Source = "coahide";
    std::mutex Lock;
    std::unordered_map<ObjectGuid::LowType, uint32> Masks;

    uint32 MaskOf(Player const* player)
    {
        std::lock_guard<std::mutex> guard(Lock);
        auto found = Masks.find(player->GetGUID().GetCounter());
        return found == Masks.end() ? 0 : found->second;
    }

    void Refresh(Player* player, uint8 slot)
    {

        player->SetVisibleItemSlot(slot, player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot));
    }

    void Save(Player const* player, uint32 mask)
    {
        CharacterDatabase.Execute("REPLACE INTO character_settings (guid, source, data) VALUES ({}, '{}', '{}')",
            player->GetGUID().GetCounter(), Source, mask);
    }
}

class CoAHideSlotsPlayer final : public PlayerScript
{
public:
    CoAHideSlotsPlayer() : PlayerScript("CoAHideSlotsPlayer", {PLAYERHOOK_ON_LOGIN, PLAYERHOOK_ON_AFTER_SET_VISIBLE_ITEM_SLOT}) { }

    void OnPlayerLogin(Player* player) override
    {
        uint32 mask = 0;
        if (QueryResult result = CharacterDatabase.Query("SELECT data FROM character_settings WHERE guid = {} AND source = '{}'",
            player->GetGUID().GetCounter(), Source))
            mask = Acore::StringTo<uint32>((*result)[0].Get<std::string>()).value_or(0);
        {
            std::lock_guard<std::mutex> guard(Lock);
            if (mask)
                Masks[player->GetGUID().GetCounter()] = mask;
            else
                Masks.erase(player->GetGUID().GetCounter());
        }
        for (HideSlot const& hide : HideSlots)
            if (mask & (1u << hide.slot))
                Refresh(player, hide.slot);
    }

    void OnPlayerAfterSetVisibleItemSlot(Player* player, uint8 slot, Item*) override
    {
        if (slot < EQUIPMENT_SLOT_END && (MaskOf(player) & (1u << slot)))
        {
            player->SetUInt32Value(PLAYER_VISIBLE_ITEM_1_ENTRYID + slot * 2, 0);
            player->SetUInt32Value(PLAYER_VISIBLE_ITEM_1_ENCHANTMENT + slot * 2, 0);
        }
    }
};

class CoAHideSlotsCommands final : public CommandScript
{
public:
    CoAHideSlotsCommands() : CommandScript("CoAHideSlotsCommands") { }

    ChatCommandTable GetCommands() const override
    {
        static ChatCommandTable commandTable =
        {
            { "coahide", HandleHide, SEC_PLAYER, Console::No },
        };
        return commandTable;
    }

    static bool HandleHide(ChatHandler* handler, Optional<std::string> what)
    {
        Player* player = handler->GetPlayer();
        if (!player)
            return false;
        uint32 mask = MaskOf(player);
        std::string name = what ? *what : "";
        for (char& c : name)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

        if (name == "show" || name == "all" || name == "reset")
        {
            {
                std::lock_guard<std::mutex> guard(Lock);
                Masks.erase(player->GetGUID().GetCounter());
            }
            Save(player, 0);
            for (HideSlot const& hide : HideSlots)
                Refresh(player, hide.slot);
            handler->SendSysMessage("All your armor is shown again.");
            return true;
        }
        for (HideSlot const& hide : HideSlots)
        {
            if (name != hide.name)
                continue;
            mask ^= 1u << hide.slot;
            {
                std::lock_guard<std::mutex> guard(Lock);
                if (mask)
                    Masks[player->GetGUID().GetCounter()] = mask;
                else
                    Masks.erase(player->GetGUID().GetCounter());
            }
            Save(player, mask);
            Refresh(player, hide.slot);
            handler->PSendSysMessage("{} is now {}.", hide.name, (mask & (1u << hide.slot)) ? "hidden" : "shown");
            return true;
        }
        handler->SendSysMessage("Hide a worn piece: head, shoulder, shirt, chest, waist, pants, boots, wrist, gloves, cloak, tabard. 'show' shows everything again.");
        return true;
    }
};

void AddSC_CoAHideSlots()
{
    new CoAHideSlotsPlayer();
    new CoAHideSlotsCommands();
}
