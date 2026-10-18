/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license.
 */

#include "Chat.h"
#include "CommandScript.h"
#include "DatabaseEnv.h"
#include "GameTime.h"
#include "Log.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "PlayerScript.h"
#include "Random.h"
#include "StringConvert.h"
#include "ServerScript.h"
#include "SpellScript.h"
#include "SpellScriptLoader.h"
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
constexpr uint8 TwinRace = 32;
constexpr uint8 OgreRace = 53;
constexpr uint32 BrotherSpell = 9530100;
constexpr uint32 HeadsSpell = 9530101;
constexpr uint32 TeamingUpAura = 9530102;
constexpr uint32 LonelyAura = 9530103;
constexpr uint32 AgreedAura = 9530104;
constexpr uint32 DisagreedAura = 9530105;
constexpr std::array<uint32, 4> SelfAuras = { TeamingUpAura, LonelyAura, AgreedAura, DisagreedAura };
constexpr uint32 SelfAuraTick = 1000;
constexpr uint32 SheathEmote = 573;
constexpr uint32 TwinQuest = 9530020;
constexpr char const* EchoRefusal = "Your brother still wears whatever you wear. He will want his own clothes "
    "once you have earned the Bravery Badge together.";
constexpr float TwinDistance = 0.35f;
constexpr float TwinReach = 0.15f;
constexpr float TwinAngle = float(M_PI) * 1.5f;
constexpr std::size_t LookSlots = 11;
constexpr std::size_t WardrobeSlots = LookSlots + 3;
constexpr std::size_t MainHand = LookSlots;
constexpr std::size_t OffHand = LookSlots + 1;
constexpr std::size_t Ranged = LookSlots + 2;
constexpr float TwinFarAway = 40.0f;
constexpr uint32 GhostSpell = 8326;
constexpr uint32 PiggybackVehicle = 9530;
constexpr uint32 SaddleVehicle = 9531;
constexpr int8 TwinSeat = 1;
constexpr uint32 MotionTick = 200;
constexpr uint32 HopDelay = 1000;
constexpr uint32 PointMin = 20000;
constexpr uint32 PointMax = 45000;
constexpr uint32 PointHold = 1500;
constexpr uint32 AfootStride = 2000;
constexpr uint32 CallBackDelay = 5000;

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
    uint32 Afoot = 0;
    bool Split = false;
    bool Restyle = false;
    Carry Mode = Carry::Beside;
};

struct TwinSlot
{
    char const* Name;
    EquipmentSlots Slot;
};

constexpr std::array<TwinSlot, WardrobeSlots> Slots = { {
    { "head", EQUIPMENT_SLOT_HEAD }, { "shoulders", EQUIPMENT_SLOT_SHOULDERS }, { "shirt", EQUIPMENT_SLOT_BODY },
    { "chest", EQUIPMENT_SLOT_CHEST }, { "waist", EQUIPMENT_SLOT_WAIST }, { "legs", EQUIPMENT_SLOT_LEGS },
    { "feet", EQUIPMENT_SLOT_FEET }, { "wrists", EQUIPMENT_SLOT_WRISTS }, { "hands", EQUIPMENT_SLOT_HANDS },
    { "back", EQUIPMENT_SLOT_BACK }, { "tabard", EQUIPMENT_SLOT_TABARD }, { "mainhand", EQUIPMENT_SLOT_MAINHAND },
    { "offhand", EQUIPMENT_SLOT_OFFHAND }, { "ranged", EQUIPMENT_SLOT_RANGED } } };

struct Wardrobe
{
    bool Enabled = false;
    std::array<uint32, WardrobeSlots> Items = {};
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
std::unordered_map<uint32, uint32>& sentHome = *new std::unordered_map<uint32, uint32>;
std::unordered_map<uint32, uint32>& auraTicks = *new std::unordered_map<uint32, uint32>;

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
            "`waist`, `legs`, `feet`, `wrists`, `hands`, `back`, `tabard`, `mainhand`, `offhand`, `ranged` "
            "FROM `mod_subclass_twin` WHERE `guid` = {}",
            guid))
    {
        Field* fields = result->Fetch();
        wardrobe.Enabled = fields[0].Get<bool>();
        for (std::size_t slot = 0; slot < WardrobeSlots; ++slot)
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
        "`chest`, `waist`, `legs`, `feet`, `wrists`, `hands`, `back`, `tabard`, `mainhand`, `offhand`, `ranged`) "
        "VALUES ({}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {})", guid, wardrobe.Enabled ? 1 : 0,
        items[0], items[1], items[2], items[3], items[4], items[5], items[6], items[7], items[8], items[9], items[10],
        items[MainHand], items[OffHand], items[Ranged]);
    std::lock_guard<std::mutex> lock(twinMutex);
    wardrobes[guid] = wardrobe;
}

bool IsSplit(Player const* player)
{
    return player->GetQuestRewardStatus(TwinQuest);
}

Wardrobe Worn(Player const* player)
{
    Wardrobe worn;
    for (std::size_t slot = 0; slot < WardrobeSlots; ++slot)
        worn.Items[slot] = player->GetUInt32Value(PLAYER_VISIBLE_ITEM_1_ENTRYID + Slots[slot].Slot * 2);
    return worn;
}

Wardrobe Dressed(Player const* player)
{
    Wardrobe wardrobe = WardrobeOf(player);
    if (!IsSplit(player))
        wardrobe.Items = Worn(player).Items;
    return wardrobe;
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
    if (!player->IsAlive() || player->IsInCombat() || player->IsInWater() || !player->IsStandState())
        return Carry::Beside;
    if (player->IsMounted())
        return player->GetVehicleKit() && !OwnsKit(player) ? Carry::Beside : Carry::Saddle;
    if (player->IsFlying())
        return Carry::Beside;
    if (motion.Afoot && motion.Afoot <= AfootStride)
        return Carry::Beside;
    if (motion.Mode == Carry::Piggyback)
        return Carry::Piggyback;
    return motion.Moving >= HopDelay ? Carry::Piggyback : Carry::Beside;
}

void Arm(Creature* twin, Wardrobe const& wardrobe)
{
    twin->SetVirtualItem(0, wardrobe.Items[MainHand]);
    twin->SetVirtualItem(1, wardrobe.Items[OffHand]);
    twin->SetVirtualItem(2, wardrobe.Items[Ranged]);
}

ItemTemplate const* RangedOf(Player const* player)
{
    uint32 const itemId = Dressed(player).Items[Ranged];
    return itemId ? sObjectMgr->GetItemTemplate(itemId) : nullptr;
}

uint32 RangedEmote(ItemTemplate const* ranged)
{
    switch (ranged->SubClass)
    {
        case ITEM_SUBCLASS_WEAPON_BOW:
            return EMOTE_ONESHOT_ATTACK_BOW;
        case ITEM_SUBCLASS_WEAPON_GUN:
        case ITEM_SUBCLASS_WEAPON_CROSSBOW:
            return EMOTE_ONESHOT_ATTACK_RIFLE;
        case ITEM_SUBCLASS_WEAPON_THROWN:
            return EMOTE_ONESHOT_ATTACK_THROWN;
        default:
            return EMOTE_ONESHOT_SPELL_CAST_OMNI;
    }
}

SheathState DrawnLike(Player const* player)
{
    SheathState const sheath = player->GetSheath();
    if (sheath == SHEATH_STATE_UNARMED || !IsSplit(player))
        return sheath;
    return RangedOf(player) ? SHEATH_STATE_RANGED : SHEATH_STATE_MELEE;
}

void BecomeGhost(Player* player, Creature* twin, bool ghost)
{
    uint32 const seen = ghost ? GHOST_VISIBILITY_GHOST : GHOST_VISIBILITY_ALIVE | GHOST_VISIBILITY_GHOST;
    twin->m_serverSideVisibility.SetValue(SERVERSIDE_VISIBILITY_GHOST, seen);
    twin->m_serverSideVisibilityDetect.SetValue(SERVERSIDE_VISIBILITY_GHOST, seen);
    if (ghost)
        twin->AddAura(GhostSpell, twin);
    else
        twin->RemoveAurasDueToSpell(GhostSpell);
    twin->SetStandState(UNIT_STAND_STATE_STAND);
    twin->UpdateObjectVisibility();
    twin->GetMotionMaster()->Clear();
    twin->GetMotionMaster()->MoveFollow(player, TwinDistance, TwinAngle);
}

void MirrorStance(Player* player, Creature* twin)
{
    if (!player->IsInCombat())
    {
        SheathState const drawn = DrawnLike(player);
        if (twin->GetSheath() != drawn)
        {
            twin->HandleEmoteCommand(SheathEmote);
            twin->SetSheath(drawn);
        }
    }
    if (twin->GetVehicle())
        return;
    bool const ghost = player->HasPlayerFlag(PLAYER_FLAGS_GHOST);
    if (!player->IsAlive() && !ghost)
    {
        if (twin->getStandState() != UNIT_STAND_STATE_DEAD)
        {
            twin->GetMotionMaster()->Clear();
            twin->StopMoving();
            twin->SetStandState(UNIT_STAND_STATE_DEAD);
        }
        return;
    }
    if (ghost != twin->HasAura(GhostSpell))
        BecomeGhost(player, twin, ghost);
    else if (twin->getStandState() == UNIT_STAND_STATE_DEAD)
    {
        twin->SetStandState(UNIT_STAND_STATE_STAND);
        twin->GetMotionMaster()->MoveFollow(player, TwinDistance, TwinAngle);
    }
    if (!twin->IsWithinDist(player, TwinFarAway))
    {
        Position beside = player->GetPosition();
        player->MovePositionToFirstCollision(beside, TwinDistance, TwinAngle);
        twin->NearTeleportTo(beside.GetPositionX(), beside.GetPositionY(), beside.GetPositionZ(),
            player->GetOrientation());
        twin->GetMotionMaster()->MoveFollow(player, TwinDistance, TwinAngle);
    }
    if (twin->getStandState() != player->getStandState())
        twin->SetStandState(player->getStandState());
}

void HopOffFor(Player* player, uint32 duration)
{
    std::lock_guard<std::mutex> lock(twinMutex);
    auto itr = motions.find(player->GetGUID().GetCounter());
    if (itr != motions.end())
        itr->second.Afoot = std::max(itr->second.Afoot, duration);
}

Unit* TwinTarget(Player* player)
{
    if (Unit* victim = player->GetVictim())
        return victim;
    Unit* selected = player->GetSelectedUnit();
    if (selected && selected->IsAlive() && player->IsValidAttackTarget(selected))
        return selected;
    auto const& attackers = player->getAttackers();
    return attackers.empty() ? nullptr : *attackers.begin();
}

bool EchoSwing(Player* player, Creature* twin, uint32 swing)
{
    Unit* victim = TwinTarget(player);
    ItemTemplate const* ranged = IsSplit(player) || !player->GetVictim() ? RangedOf(player) : nullptr;
    uint32 const pace = ranged && ranged->Delay ? ranged->Delay : player->GetAttackTime(BASE_ATTACK);
    if (!victim || twin->GetVehicle() || swing < std::max<uint32>(pace, MotionTick))
        return false;
    twin->SetFacingToObject(victim);
    if (ranged)
    {
        twin->SetSheath(SHEATH_STATE_RANGED);
        twin->HandleEmoteCommand(RangedEmote(ranged));
        return true;
    }
    twin->SetSheath(SHEATH_STATE_MELEE);
    twin->HandleEmoteCommand(twin->GetUInt32Value(UNIT_VIRTUAL_ITEM_SLOT_ID) ?
        EMOTE_ONESHOT_ATTACK1H : EMOTE_ONESHOT_ATTACK_UNARMED);
    return true;
}

void SummonTwin(Player* player);

void Restyle(Player* player, bool split)
{
    if (split)
    {
        Wardrobe wardrobe = WardrobeOf(player);
        wardrobe.Items = Worn(player).Items;
        StoreWardrobe(player, wardrobe);
        ChatHandler(player->GetSession()).SendSysMessage(
            "Your brother looks down at his clothes, then at yours. From now on he picks his own.");
    }
    SummonTwin(player);
}

void MarkRestyle(Player const* player)
{
    std::lock_guard<std::mutex> lock(twinMutex);
    auto itr = motions.find(player->GetGUID().GetCounter());
    if (itr != motions.end() && !itr->second.Split)
        itr->second.Restyle = true;
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
        stored.Afoot = stored.Afoot > stored.Tick ? stored.Afoot - stored.Tick : 0;
        if (stored.Mode == Carry::Piggyback && moving && !stored.Afoot)
        {
            stored.NextPoint = stored.NextPoint > stored.Tick ? stored.NextPoint - stored.Tick : 0;
            if (!stored.NextPoint)
            {
                pointNow = true;
                stored.Afoot = PointHold + AfootStride;
                stored.NextPoint = urand(PointMin, PointMax);
            }
        }
        stored.Tick = 0;
        motion = stored;
    }
    Creature* twin = TwinOf(player);
    if (!twin)
        return;
    if (motion.Restyle || motion.Split != IsSplit(player))
        return Restyle(player, !motion.Split && IsSplit(player));
    if (pointNow && twin->GetVehicle())
        twin->HandleEmoteCommand(EMOTE_ONESHOT_POINT);
    if (EchoSwing(player, twin, motion.Swing))
    {
        std::lock_guard<std::mutex> lock(twinMutex);
        motions[guid].Swing = 0;
    }
    MirrorStance(player, twin);
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

    Wardrobe const dressed = Dressed(player);
    Look const look = LookOf(player, dressed);
    {
        std::lock_guard<std::mutex> lock(twinMutex);
        twinOfOwner[player->GetGUID().GetCounter()] = twin->GetGUID();
        looks[twin->GetGUID()] = look;
        motions[player->GetGUID().GetCounter()].Split = IsSplit(player);
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
    Arm(twin, dressed);
    twin->GetMotionMaster()->MoveFollow(player, TwinDistance, TwinAngle);
}

bool HasTwin(Player const* player)
{
    return player->getRace() == TwinRace && player->getGender() == GENDER_MALE;
}

void RefreshTwin(Player* player)
{
    if (HasTwin(player) && WardrobeOf(player).Enabled)
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
    for (std::size_t slot = 0; slot < WardrobeSlots; ++slot)
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

std::string TwinWearing(Player const* player)
{
    std::string reply = "TWINWEAR";
    if (!HasTwin(player))
        return reply;
    Wardrobe const wardrobe = Dressed(player);
    for (uint32 itemId : wardrobe.Items)
        reply += Acore::StringFormat(" {}", itemId);
    return reply;
}

void SetBorrowedPassives(Player* player, bool active);

std::string TwinState(Player const* player)
{
    return Acore::StringFormat("TWINSTATE {} {} {}", HasTwin(player) ? 1 : 0, WardrobeOf(player).Enabled ? 1 : 0,
        IsSplit(player) ? 1 : 0);
}

bool SubclassChosen(Player const* player);

bool HasSecondHead(Player const* player)
{
    return player->getRace() == OgreRace && player->getGender() == GENDER_FEMALE;
}

bool SubclassResting(Player const* player)
{
    if (HasTwin(player))
        return IsSplit(player) && !WardrobeOf(player).Enabled;
    return HasSecondHead(player) && WardrobeOf(player).Enabled;
}

uint32 WantedSelfAura(Player const* player)
{
    if (HasTwin(player) && IsSplit(player))
        return WardrobeOf(player).Enabled ? TeamingUpAura : LonelyAura;
    if (HasSecondHead(player) && SubclassChosen(player))
        return WardrobeOf(player).Enabled ? AgreedAura : DisagreedAura;
    return 0;
}

void UpdateSelfAura(Player* player)
{
    if (!player->IsAlive())
        return;
    uint32 const wanted = WantedSelfAura(player);
    for (uint32 aura : SelfAuras)
    {
        if (aura == wanted && !player->HasAura(aura))
            player->AddAura(aura, player);
        else if (aura != wanted && player->HasAura(aura))
            player->RemoveAurasDueToSpell(aura);
    }
}

void TickSelfAura(Player* player, uint32 diff)
{
    {
        std::lock_guard<std::mutex> lock(twinMutex);
        uint32& elapsed = auraTicks[player->GetGUID().GetCounter()];
        elapsed += diff;
        if (elapsed < SelfAuraTick)
            return;
        elapsed = 0;
    }
    UpdateSelfAura(player);
}

void LearnRacials(Player* player)
{
    if (HasTwin(player) && !player->HasSpell(BrotherSpell))
        player->learnSpell(BrotherSpell, false);
    if (HasSecondHead(player) && !player->HasSpell(HeadsSpell))
        player->learnSpell(HeadsSpell, false);
}

bool WaitingToReturn(Player const* player, bool returning)
{
    uint32 const guid = player->GetGUID().GetCounter();
    uint32 const now = GameTime::GetGameTimeMS().count();
    std::lock_guard<std::mutex> lock(twinMutex);
    auto itr = sentHome.find(guid);
    if (returning && itr != sentHome.end() && now - itr->second < CallBackDelay)
        return true;
    if (!returning)
        sentHome[guid] = now;
    return false;
}

std::string ToggleHeads(Player* player)
{
    if (!HasSecondHead(player))
        return "Only a two-headed ogre can argue with herself.";
    if (!SubclassChosen(player))
        return "Your heads have nothing to argue about yet.";
    if (player->IsInCombat())
        return "Your heads are far too busy fighting to argue about that now.";
    Wardrobe wardrobe = WardrobeOf(player);
    bool const agreeing = !wardrobe.Enabled;
    if (WaitingToReturn(player, !agreeing))
        return "Your heads only just agreed. Give them a few seconds.";
    wardrobe.Enabled = agreeing;
    StoreWardrobe(player, wardrobe);
    SetBorrowedPassives(player, !agreeing);
    UpdateSelfAura(player);
    return agreeing ? "Your heads agree. For once." : "Your heads disagree again.";
}

std::string ToggleTwin(Player* player)
{
    if (!HasTwin(player))
        return "Only the Thin Twins have a brother to call.";
    if (player->IsInCombat())
        return "Your brother can't come or go in the middle of a fight.";
    Wardrobe wardrobe = WardrobeOf(player);
    if (WaitingToReturn(player, !wardrobe.Enabled))
        return "Your brother is still on his way home. Give him a few seconds.";
    wardrobe.Enabled = !wardrobe.Enabled;
    StoreWardrobe(player, wardrobe);
    RefreshTwin(player);
    if (IsSplit(player))
        SetBorrowedPassives(player, wardrobe.Enabled);
    UpdateSelfAura(player);
    return wardrobe.Enabled ? "Your brother comes running." : "Your brother goes home.";
}

void HandleTwinLook(Player* player, std::vector<std::string_view> const& words)
{
    constexpr std::array<int, 15> SlotOfCategory = { -1, 0, 1, 9, 3, 10, 2, 7, 8, 4, 5, 6, int(Ranged),
        int(MainHand), int(OffHand) };
    if (!HasTwin(player))
        return;
    if (!IsSplit(player))
    {
        ChatHandler(player->GetSession()).SendSysMessage(EchoRefusal);
        return;
    }
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
        PLAYERHOOK_ON_LOGOUT, PLAYERHOOK_ON_MAP_CHANGED, PLAYERHOOK_ON_DELETE, PLAYERHOOK_ON_UPDATE,
        PLAYERHOOK_ON_TEXT_EMOTE, PLAYERHOOK_ON_AFTER_SET_VISIBLE_ITEM_SLOT })
    {
    }

    void OnPlayerUpdate(Player* player, uint32 diff) override
    {
        UpdateCarry(player, diff);
        TickSelfAura(player, diff);
    }

    void OnPlayerTextEmote(Player* player, uint32 /*textEmote*/, uint32 /*emoteNum*/, ObjectGuid /*guid*/) override
    {
        HopOffFor(player, AfootStride);
    }

    void OnPlayerAfterSetVisibleItemSlot(Player* player, uint8 /*slot*/, Item* /*item*/) override
    {
        MarkRestyle(player);
    }

    void OnPlayerLogin(Player* player) override
    {
        RefreshTwin(player);
        LearnRacials(player);
        if (SubclassResting(player))
            SetBorrowedPassives(player, false);
        UpdateSelfAura(player);
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

    static bool RefuseWithoutTwin(ChatHandler* handler)
    {
        if (HasTwin(handler->GetSession()->GetPlayer()))
            return false;
        handler->SendSysMessage("Only the Thin Twins have a brother to call.");
        return true;
    }

    static bool HandleToggle(ChatHandler* handler)
    {
        if (RefuseWithoutTwin(handler))
            return true;
        handler->SendSysMessage(ToggleTwin(handler->GetSession()->GetPlayer()));
        return true;
    }

    static bool RefuseWhileEcho(ChatHandler* handler)
    {
        if (IsSplit(handler->GetSession()->GetPlayer()))
            return false;
        handler->SendSysMessage(EchoRefusal);
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
        if (RefuseWithoutTwin(handler) || RefuseWhileEcho(handler))
            return true;
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
        if (RefuseWithoutTwin(handler) || RefuseWhileEcho(handler))
            return true;
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
        if (RefuseWithoutTwin(handler) || RefuseWhileEcho(handler))
            return true;
        Player* player = handler->GetSession()->GetPlayer();
        Wardrobe wardrobe = WardrobeOf(player);
        for (std::size_t slot = 0; slot < WardrobeSlots; ++slot)
            wardrobe.Items[slot] = player->GetUInt32Value(PLAYER_VISIBLE_ITEM_1_ENTRYID + Slots[slot].Slot * 2);
        handler->SendSysMessage("Your twin copies your outfit.");
        return Dress(handler, wardrobe);
    }

    static bool HandleInfo(ChatHandler* handler)
    {
        if (RefuseWithoutTwin(handler))
            return true;
        Player* player = handler->GetSession()->GetPlayer();
        Wardrobe const wardrobe = WardrobeOf(player);
        std::string worn;
        for (std::size_t slot = 0; slot < WardrobeSlots; ++slot)
            if (wardrobe.Items[slot])
                worn += Acore::StringFormat(" {}={}", Slots[slot].Name, wardrobe.Items[slot]);
        handler->PSendSysMessage("Twin: {}, {}. Wears:{}", wardrobe.Enabled ? "out" : "away",
            TwinOf(player) ? "spawned" : "not spawned", worn.empty() ? " nothing" : worn);
        return true;
    }
};

class spell_subclass_second_self : public SpellScript
{
    PrepareSpellScript(spell_subclass_second_self);

    void HandleDummy(SpellEffIndex /*effIndex*/)
    {
        Player* player = GetCaster() ? GetCaster()->ToPlayer() : nullptr;
        if (!player)
            return;
        std::string const reply = HasTwin(player) ? ToggleTwin(player) : ToggleHeads(player);
        ChatHandler(player->GetSession()).SendSysMessage(reply);
    }

    void Register() override
    {
        OnEffectHitTarget += SpellEffectFn(spell_subclass_second_self::HandleDummy, EFFECT_0, SPELL_EFFECT_DUMMY);
    }
};

void AddSC_mod_subclass_twin()
{
    RegisterSpellScript(spell_subclass_second_self);
    new mod_subclass_twin_packets();
    new mod_subclass_twin_player();
    new mod_subclass_twin_commands();
}
