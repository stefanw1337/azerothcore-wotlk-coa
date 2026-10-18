/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license.
 */

#include "Chat.h"
#include "CommandScript.h"
#include "DatabaseEnv.h"
#include "Log.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "PlayerScript.h"
#include "Random.h"
#include "StringConvert.h"
#include "ServerScript.h"
#include "TemporarySummon.h"
#include "Vehicle.h"
#include "WorldPacket.h"
#include "WorldSession.h"
#include <array>
#include <cmath>
#include <mutex>
#include <unordered_map>

using namespace Acore::ChatCommands;

namespace
{
constexpr uint32 TwinEntry = 9530030;
constexpr float TwinDistance = 0.35f;
constexpr float TwinReach = 0.15f;
constexpr float TwinAngle = float(M_PI) * 1.5f;
constexpr std::size_t LookSlots = 11;
constexpr uint32 PiggybackVehicle = 9530;
constexpr uint32 SaddleVehicle = 9531;
constexpr int8 TwinSeat = 1;
constexpr uint32 MotionTick = 200;
constexpr uint32 HopDelay = 1000;
constexpr uint32 PointMin = 20000;
constexpr uint32 PointMax = 45000;

enum class Carry
{
    Beside,
    Piggyback,
    Saddle
};

struct Motion
{
    uint32 Tick = 0;
    uint32 Moving = 0;
    uint32 NextPoint = PointMin;
    uint32 Swing = 0;
    Carry Mode = Carry::Beside;
};

struct TwinSlot
{
    char const* Name;
    EquipmentSlots Slot;
};

constexpr std::array<TwinSlot, LookSlots> Slots = { {
    { "head", EQUIPMENT_SLOT_HEAD }, { "shoulders", EQUIPMENT_SLOT_SHOULDERS }, { "shirt", EQUIPMENT_SLOT_BODY },
    { "chest", EQUIPMENT_SLOT_CHEST }, { "waist", EQUIPMENT_SLOT_WAIST }, { "legs", EQUIPMENT_SLOT_LEGS },
    { "feet", EQUIPMENT_SLOT_FEET }, { "wrists", EQUIPMENT_SLOT_WRISTS }, { "hands", EQUIPMENT_SLOT_HANDS },
    { "back", EQUIPMENT_SLOT_BACK }, { "tabard", EQUIPMENT_SLOT_TABARD } } };

struct Wardrobe
{
    bool Enabled = false;
    std::array<uint32, LookSlots> Items = {};
};

struct Look
{
    uint32 DisplayId = 0;
    uint8 Race = 0;
    uint8 Gender = 0;
    uint8 Class = 0;
    std::array<uint8, 5> Features = {};
    std::array<uint32, LookSlots> Displays = {};
};

std::mutex& twinMutex = *new std::mutex;
std::unordered_map<uint32, Wardrobe>& wardrobes = *new std::unordered_map<uint32, Wardrobe>;
std::unordered_map<uint32, ObjectGuid>& twinOfOwner = *new std::unordered_map<uint32, ObjectGuid>;
std::unordered_map<ObjectGuid, Look>& looks = *new std::unordered_map<ObjectGuid, Look>;
std::unordered_map<uint32, Motion>& motions = *new std::unordered_map<uint32, Motion>;

Wardrobe WardrobeOf(Player const* player)
{
    uint32 const guid = player->GetGUID().GetCounter();
    {
        std::lock_guard<std::mutex> lock(twinMutex);
        if (auto itr = wardrobes.find(guid); itr != wardrobes.end())
            return itr->second;
    }

    Wardrobe wardrobe;
    if (QueryResult result = CharacterDatabase.Query("SELECT `enabled`, `head`, `shoulders`, `shirt`, `chest`, "
            "`waist`, `legs`, `feet`, `wrists`, `hands`, `back`, `tabard` FROM `mod_subclass_twin` WHERE `guid` = {}",
            guid))
    {
        Field* fields = result->Fetch();
        wardrobe.Enabled = fields[0].Get<bool>();
        for (std::size_t slot = 0; slot < LookSlots; ++slot)
            wardrobe.Items[slot] = fields[slot + 1].Get<uint32>();
    }

    std::lock_guard<std::mutex> lock(twinMutex);
    return wardrobes.try_emplace(guid, wardrobe).first->second;
}

void StoreWardrobe(Player const* player, Wardrobe const& wardrobe)
{
    uint32 const guid = player->GetGUID().GetCounter();
    auto const& items = wardrobe.Items;
    CharacterDatabase.Execute("REPLACE INTO `mod_subclass_twin` (`guid`, `enabled`, `head`, `shoulders`, `shirt`, "
        "`chest`, `waist`, `legs`, `feet`, `wrists`, `hands`, `back`, `tabard`) "
        "VALUES ({}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {})", guid, wardrobe.Enabled ? 1 : 0, items[0],
        items[1], items[2], items[3], items[4], items[5], items[6], items[7], items[8], items[9], items[10]);
    std::lock_guard<std::mutex> lock(twinMutex);
    wardrobes[guid] = wardrobe;
}

uint32 DisplayOfItem(uint32 itemId)
{
    ItemTemplate const* item = itemId ? sObjectMgr->GetItemTemplate(itemId) : nullptr;
    return item ? item->DisplayInfoID : 0;
}

Look LookOf(Player const* player, Wardrobe const& wardrobe)
{
    Look look;
    look.DisplayId = player->GetNativeDisplayId();
    look.Race = player->getRace();
    look.Gender = player->getGender();
    look.Class = player->getClass();
    look.Features = { player->GetByteValue(PLAYER_BYTES, 0), player->GetByteValue(PLAYER_BYTES, 1),
        player->GetByteValue(PLAYER_BYTES, 2), player->GetByteValue(PLAYER_BYTES, 3),
        player->GetByteValue(PLAYER_BYTES_2, 0) };
    for (std::size_t slot = 0; slot < LookSlots; ++slot)
        look.Displays[slot] = DisplayOfItem(wardrobe.Items[slot]);
    return look;
}

Creature* TwinOf(Player const* player)
{
    ObjectGuid guid;
    {
        std::lock_guard<std::mutex> lock(twinMutex);
        auto itr = twinOfOwner.find(player->GetGUID().GetCounter());
        if (itr == twinOfOwner.end())
            return nullptr;
        guid = itr->second;
    }
    return ObjectAccessor::GetCreature(*player, guid);
}

bool OwnsKit(Player const* player)
{
    Vehicle const* kit = player->GetVehicleKit();
    return kit && (kit->GetVehicleInfo()->m_ID == PiggybackVehicle || kit->GetVehicleInfo()->m_ID == SaddleVehicle);
}

void SendVehicleData(Player* player, uint32 vehicleId)
{
    WorldPacket data(SMSG_PLAYER_VEHICLE_DATA, player->GetPackGUID().size() + 4);
    data << player->GetPackGUID();
    data << uint32(vehicleId);
    player->SendMessageToSet(&data, true);
}

void DropKit(Player* player)
{
    if (!OwnsKit(player))
        return;
    SendVehicleData(player, 0);
    player->RemoveVehicleKit();
}

bool GiveKit(Player* player, uint32 vehicleId)
{
    if (Vehicle const* kit = player->GetVehicleKit())
    {
        if (kit->GetVehicleInfo()->m_ID == vehicleId)
            return true;
        if (!OwnsKit(player))
            return false;
        DropKit(player);
    }
    if (!player->CreateVehicleKit(vehicleId, 0))
        return false;
    player->GetVehicleKit()->Reset();
    player->RemoveNpcFlag(UNIT_NPC_FLAG_SPELLCLICK | UNIT_NPC_FLAG_PLAYER_VEHICLE);
    SendVehicleData(player, vehicleId);
    return true;
}

void SetCarry(Player* player, Creature* twin, Carry mode)
{
    if (mode == Carry::Beside)
    {
        if (twin->GetVehicle())
            twin->ExitVehicle();
        DropKit(player);
        twin->GetMotionMaster()->Clear();
        twin->GetMotionMaster()->MoveFollow(player, TwinDistance, TwinAngle);
        return;
    }
    if (!GiveKit(player, mode == Carry::Piggyback ? PiggybackVehicle : SaddleVehicle))
        return SetCarry(player, twin, Carry::Beside);
    twin->GetMotionMaster()->Clear();
    twin->EnterVehicle(player, TwinSeat);
}

Carry WantedCarry(Player const* player, Motion const& motion)
{
    if (!player->IsAlive() || player->IsInCombat() || player->IsInWater())
        return Carry::Beside;
    if (player->IsMounted())
        return player->GetVehicleKit() && !OwnsKit(player) ? Carry::Beside : Carry::Saddle;
    if (player->IsFlying())
        return Carry::Beside;
    if (motion.Mode == Carry::Piggyback)
        return Carry::Piggyback;
    return motion.Moving >= HopDelay ? Carry::Piggyback : Carry::Beside;
}

bool EchoSwing(Player* player, Creature* twin, uint32 swing)
{
    Unit* victim = player->GetVictim();
    if (!victim || twin->GetVehicle() || swing < std::max<uint32>(player->GetAttackTime(BASE_ATTACK), MotionTick))
        return false;
    twin->SetFacingToObject(victim);
    twin->HandleEmoteCommand(twin->GetUInt32Value(UNIT_VIRTUAL_ITEM_SLOT_ID) ?
        EMOTE_ONESHOT_ATTACK1H : EMOTE_ONESHOT_ATTACK_UNARMED);
    return true;
}

void UpdateCarry(Player* player, uint32 diff)
{
    Motion motion;
    bool pointNow = false;
    uint32 const guid = player->GetGUID().GetCounter();
    {
        std::lock_guard<std::mutex> lock(twinMutex);
        if (!twinOfOwner.count(guid))
            return;
        Motion& stored = motions[guid];
        stored.Tick += diff;
        if (stored.Tick < MotionTick)
            return;
        bool const moving = player->isMoving();
        stored.Swing = player->IsInCombat() ? stored.Swing + stored.Tick : 0;
        stored.Moving = moving ? stored.Moving + stored.Tick : 0;
        pointNow = false;
        if (stored.Mode == Carry::Piggyback && moving)
        {
            stored.NextPoint = stored.NextPoint > stored.Tick ? stored.NextPoint - stored.Tick : 0;
            if (!stored.NextPoint)
            {
                pointNow = true;
                stored.NextPoint = urand(PointMin, PointMax);
            }
        }
        stored.Tick = 0;
        motion = stored;
    }
    Creature* twin = TwinOf(player);
    if (!twin)
        return;
    if (pointNow && twin->GetVehicle())
        twin->HandleEmoteCommand(EMOTE_ONESHOT_POINT);
    if (EchoSwing(player, twin, motion.Swing))
    {
        std::lock_guard<std::mutex> lock(twinMutex);
        motions[guid].Swing = 0;
    }
    Carry const wanted = WantedCarry(player, motion);
    bool const seated = twin->GetVehicle() != nullptr;
    if (wanted == motion.Mode && (wanted == Carry::Beside) != seated)
        return;
    SetCarry(player, twin, wanted);
    std::lock_guard<std::mutex> lock(twinMutex);
    motions[guid].Mode = twin->GetVehicle() ? wanted : Carry::Beside;
}

void DismissTwin(Player* player)
{
    ObjectGuid guid;
    {
        std::lock_guard<std::mutex> lock(twinMutex);
        auto itr = twinOfOwner.find(player->GetGUID().GetCounter());
        if (itr == twinOfOwner.end())
            return;
        guid = itr->second;
        twinOfOwner.erase(itr);
        looks.erase(guid);
        motions.erase(player->GetGUID().GetCounter());
    }
    if (Creature* twin = ObjectAccessor::GetCreature(*player, guid))
    {
        if (twin->GetVehicle())
            twin->ExitVehicle();
        twin->DespawnOrUnsummon();
    }
    DropKit(player);
}

void SummonTwin(Player* player)
{
    DismissTwin(player);
    if (!player->IsInWorld() || !sObjectMgr->GetCreatureTemplate(TwinEntry))
        return;

    Position position = player->GetPosition();
    player->MovePositionToFirstCollision(position, TwinDistance, TwinAngle);
    TempSummon* twin = player->SummonCreature(TwinEntry, position, TEMPSUMMON_MANUAL_DESPAWN);
    if (!twin)
        return;

    Look const look = LookOf(player, WardrobeOf(player));
    {
        std::lock_guard<std::mutex> lock(twinMutex);
        twinOfOwner[player->GetGUID().GetCounter()] = twin->GetGUID();
        looks[twin->GetGUID()] = look;
    }
    twin->SetLevel(player->GetLevel());
    twin->SetDisplayId(player->GetNativeDisplayId());
    twin->SetNativeDisplayId(player->GetNativeDisplayId());
    twin->SetFloatValue(UNIT_FIELD_COMBATREACH, TwinReach);
    twin->SetFloatValue(UNIT_FIELD_BOUNDINGRADIUS, TwinReach);
    twin->SetUnitFlag2(UNIT_FLAG2_MIRROR_IMAGE);
    twin->SetUnitFlag(UNIT_FLAG_NOT_SELECTABLE);
    twin->SetReactState(REACT_PASSIVE);
    twin->SetOwnerGUID(player->GetGUID());
    twin->SetCreatorGUID(player->GetGUID());
    twin->SetFaction(player->GetFaction());
    twin->GetMotionMaster()->MoveFollow(player, TwinDistance, TwinAngle);
}

void RefreshTwin(Player* player)
{
    if (WardrobeOf(player).Enabled)
        SummonTwin(player);
    else
        DismissTwin(player);
}

bool AnswerMirrorImage(WorldSession* session, WorldPacket const& packet)
{
    if (packet.GetOpcode() != CMSG_GET_MIRRORIMAGE_DATA || packet.size() < sizeof(uint64))
        return true;
    ObjectGuid const guid = packet.read<ObjectGuid>(0);
    Look look;
    {
        std::lock_guard<std::mutex> lock(twinMutex);
        auto itr = looks.find(guid);
        if (itr == looks.end())
            return true;
        look = itr->second;
    }

    WorldPacket response(SMSG_MIRRORIMAGE_DATA, 68);
    response << guid << uint32(look.DisplayId) << uint8(look.Race) << uint8(look.Gender) << uint8(look.Class);
    for (uint8 feature : look.Features)
        response << uint8(feature);
    response << uint32(0);
    for (uint32 display : look.Displays)
        response << uint32(display);
    session->SendPacket(&response);
    return false;
}

int SlotIndex(std::string_view name)
{
    for (std::size_t slot = 0; slot < LookSlots; ++slot)
        if (name == Slots[slot].Name)
            return int(slot);
    return -1;
}

std::string SlotList()
{
    std::string list;
    for (TwinSlot const& slot : Slots)
        list += list.empty() ? slot.Name : std::string(", ") + slot.Name;
    return list;
}
}

void HandleTwinLook(Player* player, std::vector<std::string_view> const& words)
{
    constexpr std::array<int, 12> SlotOfCategory = { -1, 0, 1, 9, 3, 10, 2, 7, 8, 4, 5, 6 };
    Wardrobe wardrobe = WardrobeOf(player);
    uint32 changed = 0;
    for (std::size_t index = 1; index < words.size(); ++index)
    {
        std::string_view const pair = words[index];
        std::size_t const colon = pair.find(':');
        if (colon == std::string_view::npos)
            continue;
        Optional<uint32> const category = Acore::StringTo<uint32>(pair.substr(0, colon));
        Optional<uint32> const itemId = Acore::StringTo<uint32>(pair.substr(colon + 1));
        if (!category || !itemId || *category >= SlotOfCategory.size() || SlotOfCategory[*category] < 0)
            continue;
        if (*itemId && !DisplayOfItem(*itemId))
            continue;
        wardrobe.Items[SlotOfCategory[*category]] = *itemId;
        ++changed;
    }
    if (!changed)
        return;
    StoreWardrobe(player, wardrobe);
    RefreshTwin(player);
    ChatHandler(player->GetSession()).PSendSysMessage("Your twin changes into his new outfit ({} piece(s)).", changed);
}

class mod_subclass_twin_packets : public ServerScript
{
public:
    mod_subclass_twin_packets() : ServerScript("mod_subclass_twin_packets", { SERVERHOOK_CAN_PACKET_RECEIVE }) { }

    bool CanPacketReceive(WorldSession* session, WorldPacket const& packet) override
    {
        return !session || AnswerMirrorImage(session, packet);
    }
};

class mod_subclass_twin_player : public PlayerScript
{
public:
    mod_subclass_twin_player() : PlayerScript("mod_subclass_twin_player", { PLAYERHOOK_ON_LOGIN,
        PLAYERHOOK_ON_LOGOUT, PLAYERHOOK_ON_MAP_CHANGED, PLAYERHOOK_ON_DELETE, PLAYERHOOK_ON_UPDATE })
    {
    }

    void OnPlayerUpdate(Player* player, uint32 diff) override
    {
        UpdateCarry(player, diff);
    }

    void OnPlayerLogin(Player* player) override
    {
        RefreshTwin(player);
    }

    void OnPlayerMapChanged(Player* player) override
    {
        RefreshTwin(player);
    }

    void OnPlayerLogout(Player* player) override
    {
        DismissTwin(player);
        std::lock_guard<std::mutex> lock(twinMutex);
        wardrobes.erase(player->GetGUID().GetCounter());
    }

    void OnPlayerDelete(ObjectGuid guid, uint32 /*accountId*/) override
    {
        CharacterDatabase.Execute("DELETE FROM `mod_subclass_twin` WHERE `guid` = {}", guid.GetCounter());
        std::lock_guard<std::mutex> lock(twinMutex);
        wardrobes.erase(guid.GetCounter());
    }
};

class mod_subclass_twin_commands : public CommandScript
{
public:
    mod_subclass_twin_commands() : CommandScript("mod_subclass_twin_commands") { }

    ChatCommandTable GetCommands() const override
    {
        static ChatCommandTable twinCommands =
        {
            { "", HandleToggle, SEC_GAMEMASTER, Console::No },
            { "wear", HandleWear, SEC_GAMEMASTER, Console::No },
            { "strip", HandleStrip, SEC_GAMEMASTER, Console::No },
            { "copy", HandleCopy, SEC_GAMEMASTER, Console::No },
            { "info", HandleInfo, SEC_GAMEMASTER, Console::No }
        };
        static ChatCommandTable commands = { { "twin", twinCommands } };
        return commands;
    }

    static bool HandleToggle(ChatHandler* handler)
    {
        Player* player = handler->GetSession()->GetPlayer();
        Wardrobe wardrobe = WardrobeOf(player);
        wardrobe.Enabled = !wardrobe.Enabled;
        StoreWardrobe(player, wardrobe);
        RefreshTwin(player);
        handler->SendSysMessage(wardrobe.Enabled ? "Your twin steps out beside you." : "Your twin steps back.");
        return true;
    }

    static bool Dress(ChatHandler* handler, Wardrobe const& wardrobe)
    {
        Player* player = handler->GetSession()->GetPlayer();
        StoreWardrobe(player, wardrobe);
        RefreshTwin(player);
        return true;
    }

    static bool HandleWear(ChatHandler* handler, std::string slotName, Variant<Hyperlink<item>, uint32> itemArg)
    {
        int const slot = SlotIndex(slotName);
        if (slot < 0)
        {
            handler->SendErrorMessage("Unknown slot. Slots: {}.", SlotList());
            return false;
        }
        uint32 const itemId = itemArg.holds_alternative<Hyperlink<item>>() ?
            itemArg.get<Hyperlink<item>>()->Item->ItemId : itemArg.get<uint32>();
        ItemTemplate const* item = sObjectMgr->GetItemTemplate(itemId);
        if (!item || !item->DisplayInfoID)
        {
            handler->SendErrorMessage("Item {} has no appearance.", itemId);
            return false;
        }
        Wardrobe wardrobe = WardrobeOf(handler->GetSession()->GetPlayer());
        wardrobe.Items[slot] = itemId;
        handler->PSendSysMessage("Your twin now wears {} on his {}.", item->Name1, Slots[slot].Name);
        return Dress(handler, wardrobe);
    }

    static bool HandleStrip(ChatHandler* handler, Optional<std::string> slotName)
    {
        Wardrobe wardrobe = WardrobeOf(handler->GetSession()->GetPlayer());
        if (!slotName || *slotName == "all")
            wardrobe.Items = {};
        else if (int const slot = SlotIndex(*slotName); slot >= 0)
            wardrobe.Items[slot] = 0;
        else
        {
            handler->SendErrorMessage("Unknown slot. Slots: {}, or all.", SlotList());
            return false;
        }
        handler->SendSysMessage("Your twin takes it off.");
        return Dress(handler, wardrobe);
    }

    static bool HandleCopy(ChatHandler* handler)
    {
        Player* player = handler->GetSession()->GetPlayer();
        Wardrobe wardrobe = WardrobeOf(player);
        for (std::size_t slot = 0; slot < LookSlots; ++slot)
            wardrobe.Items[slot] = player->GetUInt32Value(PLAYER_VISIBLE_ITEM_1_ENTRYID + Slots[slot].Slot * 2);
        handler->SendSysMessage("Your twin copies your outfit.");
        return Dress(handler, wardrobe);
    }

    static bool HandleInfo(ChatHandler* handler)
    {
        Player* player = handler->GetSession()->GetPlayer();
        Wardrobe const wardrobe = WardrobeOf(player);
        std::string worn;
        for (std::size_t slot = 0; slot < LookSlots; ++slot)
            if (wardrobe.Items[slot])
                worn += Acore::StringFormat(" {}={}", Slots[slot].Name, wardrobe.Items[slot]);
        handler->PSendSysMessage("Twin: {}, {}. Wears:{}", wardrobe.Enabled ? "out" : "away",
            TwinOf(player) ? "spawned" : "not spawned", worn.empty() ? " nothing" : worn);
        return true;
    }
};

void AddSC_mod_subclass_twin()
{
    new mod_subclass_twin_packets();
    new mod_subclass_twin_player();
    new mod_subclass_twin_commands();
}
