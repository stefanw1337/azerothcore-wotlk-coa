/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license.
 */

#include "AllCreatureScript.h"
#include "Chat.h"
#include "CommandScript.h"
#include "DatabaseEnv.h"
#include "GameTime.h"
#include "Log.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "CharmInfo.h"
#include "Pet.h"
#include "Player.h"
#include "PlayerScript.h"
#include "Random.h"
#include "StringConvert.h"
#include "ServerScript.h"
#include "SpellScript.h"
#include "SpellScriptLoader.h"
#include "SpellAuraEffects.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "TemporarySummon.h"
#include "Vehicle.h"
#include "WorldPacket.h"
#include "WorldSession.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <mutex>
#include <unordered_map>

using namespace Acore::ChatCommands;

bool SubclassChosen(Player const* player);
bool OtherHalfShoots(Player const* player);
bool MainHalfShoots(Player const* player);
bool BrotherOut(Player const* player);
void RefreshSubclassResources(Player* player);
bool IsBorrowedSpell(Player const* player, uint32 spellId);
void HandWeaponsToBrother(Player* player);
bool LendsTankSpecialization(Player const* player);
bool LendsRangedSpecialization(Player const* player);

namespace
{
constexpr uint32 TwinEntry = 9530030;
constexpr uint8 TwinRace = 96;
constexpr uint8 OgreRace = 85;
constexpr uint32 BrotherSpell = 9530100;
constexpr uint32 HeadsSpell = 9530101;
constexpr uint32 TeamingUpAura = 9530102;
constexpr uint32 LonelyAura = 9530103;
constexpr uint32 AgreedAura = 9530104;
constexpr uint32 DisagreedAura = 9530105;
constexpr std::array<uint32, 4> SelfAuras = { TeamingUpAura, LonelyAura, AgreedAura, DisagreedAura };
constexpr uint32 SelfAuraTick = 1000;
constexpr uint32 SheathEmote = 573;
constexpr uint32 ArrivalDelay = 2000;
constexpr uint32 LoginArrivalDelay = 10000;
constexpr float RangedStandoff = 15.0f;
constexpr uint8 BlackoutDrunk = 100;
constexpr uint32 CowboySpell = 9530106;
constexpr uint32 HalfBrainSpell = 9530107;
constexpr uint32 CowboyAura = 9530108;
constexpr uint32 HalfBrainAura = 9530109;
constexpr uint32 ReachAura = 9530110;
constexpr uint32 TwinLinkedSpell = 9530111;
constexpr uint32 ToleranceSpell = 9530112;
constexpr uint8 ToleratedDrunk = 89;
constexpr uint32 EasyTick = 600;
constexpr uint32 ManualPause = 3000;
constexpr float HealBelowPct = 60.0f;
constexpr int32 LastingBuff = 10000;
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
constexpr uint32 PointHold = 1000;
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
    float Follow = 0.0f;
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
std::unordered_map<uint32, uint32>& arrivals = *new std::unordered_map<uint32, uint32>;
std::unordered_map<uint32, uint8>& sips = *new std::unordered_map<uint32, uint8>;
std::unordered_map<uint32, bool>& hangovers = *new std::unordered_map<uint32, bool>;

struct Brain
{
    uint32 Tick = 0;
    uint32 Pause = 0;
    bool Busy = false;
};

std::unordered_map<uint32, Brain>& brains = *new std::unordered_map<uint32, Brain>;

struct BrotherCast
{
    ObjectGuid Player;
    ObjectGuid Twin;
    uint32 Spell = 0;
    uint32 Shown = 0;
};

thread_local BrotherCast castingAsBrother;
thread_local bool resendingAsBrother = false;

enum class Swing : uint8
{
    None,
    Melee,
    Shot
};

struct SwingPartner
{
    ObjectGuid Twin;
    Swing Style = Swing::None;
    uint32 ShotItem = 0;
};

std::unordered_map<ObjectGuid, SwingPartner>& swingPartners = *new std::unordered_map<ObjectGuid, SwingPartner>;
std::unordered_map<ObjectGuid, ObjectGuid>& shieldBearers = *new std::unordered_map<ObjectGuid, ObjectGuid>;
constexpr std::array<uint32, 4> TankForms = { FORM_BEAR, FORM_DIREBEAR, FORM_DEFENSIVESTANCE, FORM_TEST_OF_STRENGTH };
constexpr uint32 RighteousFury = 25780;
constexpr uint32 SneakAlong = 1784;
constexpr int32 TankThreatBonus = 40;
constexpr uint32 AutoShotSpell = 75;
constexpr uint32 ArrowProjectileDisplay = 3482;
constexpr uint32 BulletProjectileDisplay = 3483;
constexpr uint32 SplitHitFlags = HITINFO_FULL_ABSORB | HITINFO_PARTIAL_ABSORB | HITINFO_FULL_RESIST |
    HITINFO_PARTIAL_RESIST | HITINFO_BLOCK | HITINFO_RAGE_GAIN | HITINFO_UNK1;

struct PetOrder
{
    uint8 Type = 0;
    uint32 Action = 0;
    ObjectGuid Target;
};

struct Companion
{
    ObjectGuid Guid;
    uint32 PetNumber = 0;
    bool Staying = false;
    ReactStates React = REACT_DEFENSIVE;
    ObjectGuid Ordered;
    std::vector<PetOrder> Orders;
};

std::unordered_map<uint32, Companion>& companions = *new std::unordered_map<uint32, Companion>;
constexpr uint32 HunterPetLevelStats = 1;
constexpr uint32 HunterPetAttackTime = 2000;
constexpr float CompanionDistance = 1.5f;

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
    return player->GetQuestRewardStatus(TwinQuest) || SubclassChosen(player);
}

Wardrobe Worn(Player const* player)
{
    Wardrobe worn;
    for (std::size_t slot = 0; slot < WardrobeSlots; ++slot)
        worn.Items[slot] = player->GetUInt32Value(PLAYER_VISIBLE_ITEM_1_ENTRYID + Slots[slot].Slot * 2);
    return worn;
}

Item* OwnBow(Player const* player)
{
    return player->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_RANGED);
}

bool BrotherTanks(Player const* player)
{
    if (!IsSplit(player))
        return false;
    if (LendsTankSpecialization(player))
        for (AuraEffect const* threat : player->GetAuraEffectsByType(SPELL_AURA_MOD_THREAT))
            if (threat->GetAmount() >= TankThreatBonus && IsBorrowedSpell(player, threat->GetId()))
                return true;
    for (AuraEffect const* form : player->GetAuraEffectsByType(SPELL_AURA_MOD_SHAPESHIFT))
        if (std::find(TankForms.begin(), TankForms.end(), uint32(form->GetMiscValue())) != TankForms.end() &&
            IsBorrowedSpell(player, form->GetId()))
            return true;
    return player->HasAura(RighteousFury) && IsBorrowedSpell(player, RighteousFury);
}

void LendWeapon(Player const* player, Wardrobe& wardrobe, std::size_t slot, uint8 equipmentSlot)
{
    if (!wardrobe.Items[slot])
        if (Item const* item = player->GetItemByPos(INVENTORY_SLOT_BAG_0, equipmentSlot))
            wardrobe.Items[slot] = item->GetEntry();
}

Wardrobe Dressed(Player const* player)
{
    Wardrobe wardrobe = WardrobeOf(player);
    if (!IsSplit(player))
    {
        wardrobe.Items = Worn(player).Items;
        return wardrobe;
    }
    if (OtherHalfShoots(player))
        LendWeapon(player, wardrobe, Ranged, EQUIPMENT_SLOT_RANGED);
    if (BrotherTanks(player))
    {
        LendWeapon(player, wardrobe, MainHand, EQUIPMENT_SLOT_MAINHAND);
        LendWeapon(player, wardrobe, OffHand, EQUIPMENT_SLOT_OFFHAND);
    }
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
        std::lock_guard<std::mutex> lock(twinMutex);
        motions[player->GetGUID().GetCounter()].Follow = TwinDistance;
        return;
    }
    if (!GiveKit(player, mode == Carry::Piggyback ? PiggybackVehicle : SaddleVehicle))
        return SetCarry(player, twin, Carry::Beside);
    twin->GetMotionMaster()->Clear();
    twin->EnterVehicle(player, TwinSeat);
}

Carry WantedCarry(Player const* player, Motion const& motion)
{
    if (!player->IsAlive() || player->IsInCombat() || player->IsInWater() || !player->IsStandState() ||
        player->HasStealthAura() || player->GetSheath() != SHEATH_STATE_UNARMED)
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

bool FightsFromRange(Player const* player, Creature const* twin)
{
    if (IsSplit(player))
        return !BrotherTanks(player) && (OtherHalfShoots(player) || LendsRangedSpecialization(player) ||
            !twin->GetUInt32Value(UNIT_VIRTUAL_ITEM_SLOT_ID));
    return RangedOf(player) != nullptr;
}

float FollowDistanceFor(Player const* player, Creature const* twin)
{
    return player->IsInCombat() && FightsFromRange(player, twin) ? RangedStandoff : TwinDistance;
}

void MirrorStealth(Player* player, Creature* twin)
{
    bool const hidden = player->HasStealthAura();
    if (hidden != twin->HasAura(SneakAlong))
    {
        if (hidden)
            twin->AddAura(SneakAlong, twin);
        else
            twin->RemoveAurasDueToSpell(SneakAlong);
    }
}

void MirrorStance(Player* player, Creature* twin)
{
    MirrorStealth(player, twin);
    if (!player->IsInCombat())
    {
        SheathState const drawn = DrawnLike(player);
        if (twin->GetSheath() != drawn)
        {
            if (!twin->GetVehicle())
                twin->HandleEmoteCommand(SheathEmote);
            twin->SetSheath(drawn);
        }
    }
    if (twin->GetVehicle())
        return;
    if (player->HasStealthAura())
    {
        if (twin->getStandState() != UNIT_STAND_STATE_KNEEL)
        {
            twin->GetMotionMaster()->Clear();
            twin->StopMoving();
            twin->SetStandState(UNIT_STAND_STATE_KNEEL);
        }
        return;
    }
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
    if (player->IsAlive() && twin->GetMotionMaster()->GetCurrentMovementGeneratorType() != FOLLOW_MOTION_TYPE)
        twin->GetMotionMaster()->MoveFollow(player, FollowDistanceFor(player, twin), TwinAngle);
}

void KeepStandoff(Player* player, Creature* twin, float current)
{
    if (twin->GetVehicle() || player->HasStealthAura() || !player->IsAlive())
        return;
    float const wanted = FollowDistanceFor(player, twin);
    if (current == wanted)
        return;
    twin->GetMotionMaster()->MoveFollow(player, wanted, TwinAngle);
    std::lock_guard<std::mutex> lock(twinMutex);
    motions[player->GetGUID().GetCounter()].Follow = wanted;
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
    if (IsSplit(player))
        return false;
    Unit* victim = TwinTarget(player);
    ItemTemplate const* ranged = player->GetVictim() ? nullptr : RangedOf(player);
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
PetStable::PetInfo const* ParkedHunterPet(Player const* player)
{
    PetStable const* stable = const_cast<Player*>(player)->GetPetStable();
    if (!stable)
        return nullptr;
    for (PetStable::PetInfo const& pet : stable->UnslottedPets)
        if (pet.Type == HUNTER_PET && pet.Health)
            return &pet;
    return nullptr;
}

bool WantsCompanion(Player* player)
{
    Pet* own = player->GetPet();
    return player->getClass() != CLASS_HUNTER && player->IsClass(CLASS_HUNTER, CLASS_CONTEXT_PET) &&
        BrotherOut(player) && own && own->getPetType() != HUNTER_PET && ParkedHunterPet(player);
}

void DismissCompanion(Player* player)
{
    ObjectGuid guid;
    {
        std::lock_guard<std::mutex> lock(twinMutex);
        auto itr = companions.find(player->GetGUID().GetCounter());
        if (itr == companions.end())
            return;
        guid = itr->second.Guid;
        companions.erase(itr);
    }
    if (Creature* beast = ObjectAccessor::GetCreature(*player, guid))
        beast->DespawnOrUnsummon();
}

void SummonCompanion(Player* player, Creature* twin, PetStable::PetInfo const& pet)
{
    Creature* beast = player->SummonCreature(pet.CreatureId, twin->GetPositionX(), twin->GetPositionY(),
        twin->GetPositionZ(), twin->GetOrientation(), TEMPSUMMON_MANUAL_DESPAWN);
    if (!beast)
        return;
    beast->SetOwnerGUID(player->GetGUID());
    beast->SetCreatorGUID(player->GetGUID());
    beast->SetFaction(player->GetFaction());
    beast->ReplaceAllNpcFlags(UNIT_NPC_FLAG_NONE);
    beast->SetReactState(REACT_PASSIVE);
    beast->SetLevel(pet.Level);
    if (pet.DisplayId)
    {
        beast->SetDisplayId(pet.DisplayId);
        beast->SetNativeDisplayId(pet.DisplayId);
    }
    if (PetLevelInfo const* stats = sObjectMgr->GetPetLevelInfo(HunterPetLevelStats, pet.Level))
    {
        beast->SetCreateHealth(stats->health);
        beast->SetMaxHealth(stats->health);
        beast->SetHealth(stats->health);
    }
    beast->SetBaseWeaponDamage(BASE_ATTACK, MINDAMAGE, float(pet.Level - pet.Level / 4));
    beast->SetBaseWeaponDamage(BASE_ATTACK, MAXDAMAGE, float(pet.Level + pet.Level / 4));
    beast->SetAttackTime(BASE_ATTACK, HunterPetAttackTime);
    beast->UpdateDamagePhysical(BASE_ATTACK);
    beast->GetMotionMaster()->MoveFollow(twin, CompanionDistance, TwinAngle);
    std::lock_guard<std::mutex> lock(twinMutex);
    Companion& companion = companions[player->GetGUID().GetCounter()];
    companion = Companion{};
    companion.Guid = beast->GetGUID();
    companion.PetNumber = pet.PetNumber;
}

void FollowBrother(Creature* beast, Creature* twin)
{
    if (beast->GetVictim())
        beast->AttackStop();
    if (beast->GetMotionMaster()->GetCurrentMovementGeneratorType() != FOLLOW_MOTION_TYPE)
        beast->GetMotionMaster()->MoveFollow(twin, CompanionDistance, TwinAngle);
}

void UpdateCompanion(Player* player, Creature* twin)
{
    bool const wanted = WantsCompanion(player);
    Companion companion;
    bool exists = false;
    {
        std::lock_guard<std::mutex> lock(twinMutex);
        auto itr = companions.find(player->GetGUID().GetCounter());
        if (itr != companions.end())
        {
            exists = true;
            for (PetOrder const& order : itr->second.Orders)
            {
                if (order.Type == ACT_REACTION)
                    itr->second.React = ReactStates(order.Action);
                else if (order.Action == COMMAND_STAY)
                    itr->second.Staying = true;
                else if (order.Action == COMMAND_FOLLOW)
                {
                    itr->second.Staying = false;
                    itr->second.Ordered.Clear();
                }
                else if (order.Action == COMMAND_ATTACK)
                {
                    itr->second.Staying = false;
                    itr->second.Ordered = order.Target;
                }
            }
            itr->second.Orders.clear();
            companion = itr->second;
        }
    }
    if (!wanted)
    {
        if (exists)
            DismissCompanion(player);
        return;
    }
    if (!exists)
    {
        if (PetStable::PetInfo const* pet = ParkedHunterPet(player))
            SummonCompanion(player, twin, *pet);
        return;
    }
    Creature* beast = ObjectAccessor::GetCreature(*player, companion.Guid);
    if (!beast)
    {
        std::lock_guard<std::mutex> lock(twinMutex);
        companions.erase(player->GetGUID().GetCounter());
        return;
    }
    if (!beast->IsAlive())
        return;
    if (companion.Staying)
    {
        if (!beast->GetVictim() && beast->isMoving())
        {
            beast->GetMotionMaster()->Clear();
            beast->StopMoving();
        }
        return;
    }
    Unit* target = companion.Ordered ? ObjectAccessor::GetUnit(*player, companion.Ordered) : nullptr;
    if (!target && companion.React != REACT_PASSIVE)
    {
        target = player->GetVictim();
        if (!target)
            if (Pet* own = player->GetPet())
                target = own->GetVictim();
    }
    if (!target || !target->IsAlive() || !beast->IsValidAttackTarget(target))
        return FollowBrother(beast, twin);
    if (beast->GetVictim() != target)
    {
        beast->Attack(target, true);
        beast->GetMotionMaster()->MoveChase(target);
    }
}

void OrderCompanion(WorldSession* session, WorldPacket const& packet)
{
    Player* player = session->GetPlayer();
    if (!player || packet.GetOpcode() != CMSG_PET_ACTION || packet.size() < 20)
        return;
    WorldPacket reader(packet);
    reader.rpos(0);
    ObjectGuid pet;
    uint32 data = 0;
    ObjectGuid target;
    reader >> pet >> data >> target;
    uint8 const type = UNIT_ACTION_BUTTON_TYPE(data);
    if (pet != player->GetPetGUID() || (type != ACT_COMMAND && type != ACT_REACTION))
        return;
    std::lock_guard<std::mutex> lock(twinMutex);
    auto itr = companions.find(player->GetGUID().GetCounter());
    if (itr != companions.end())
        itr->second.Orders.push_back({ type, UNIT_ACTION_BUTTON_ACTION(data), target });
}

bool Helpless(Player const* player)
{
    return player->HasUnitState(UNIT_STATE_STUNNED | UNIT_STATE_CONFUSED | UNIT_STATE_FLEEING);
}

void MirrorStun(Player* player, Creature* twin)
{
    bool const helpless = Helpless(player);
    if (helpless == twin->HasUnitFlag(UNIT_FLAG_STUNNED))
        return;
    if (helpless)
    {
        twin->SetUnitFlag(UNIT_FLAG_STUNNED);
        twin->GetMotionMaster()->Clear();
        twin->StopMoving();
        return;
    }
    twin->RemoveUnitFlag(UNIT_FLAG_STUNNED);
    twin->GetMotionMaster()->MoveFollow(player, FollowDistanceFor(player, twin), TwinAngle);
}

uint32 CursedShape(Player const* player)
{
    for (AuraEffect const* effect : player->GetAuraEffectsByType(SPELL_AURA_TRANSFORM))
        if (!effect->GetSpellInfo()->IsPositive())
            return player->GetDisplayId();
    return 0;
}

void MirrorCurse(Player* player, Creature* twin)
{
    uint32 const cursed = CursedShape(player);
    uint32 const wanted = cursed ? cursed : player->GetNativeDisplayId();
    if (twin->GetDisplayId() == wanted)
        return;
    if (cursed)
        twin->RemoveUnitFlag2(UNIT_FLAG2_MIRROR_IMAGE);
    else
        twin->SetUnitFlag2(UNIT_FLAG2_MIRROR_IMAGE);
    twin->SetDisplayId(wanted);
}

void UpdateSwingPartner(Player* player, Creature* twin)
{
    SwingPartner partner;
    if (twin && IsSplit(player))
    {
        uint32 const shot = twin->GetUInt32Value(UNIT_VIRTUAL_ITEM_SLOT_ID + 2);
        if (shot && OtherHalfShoots(player))
            partner = { twin->GetGUID(), Swing::Shot, shot };
        else if (twin->GetUInt32Value(UNIT_VIRTUAL_ITEM_SLOT_ID))
            partner = { twin->GetGUID(), Swing::Melee, 0 };
        if (player->IsInCombat() && partner.Style != Swing::None)
        {
            SheathState const drawn = partner.Style == Swing::Shot ? SHEATH_STATE_RANGED : SHEATH_STATE_MELEE;
            if (twin->GetSheath() != drawn)
                twin->SetSheath(drawn);
        }
    }
    std::lock_guard<std::mutex> lock(twinMutex);
    if (partner.Style == Swing::None)
        swingPartners.erase(player->GetGUID());
    else
        swingPartners[player->GetGUID()] = partner;
}

void UpdateShieldBearer(Player* player, Creature* twin)
{
    bool const bears = BrotherOut(player) && BrotherTanks(player) && !twin->HasUnitFlag(UNIT_FLAG_STUNNED);
    std::lock_guard<std::mutex> lock(twinMutex);
    if (bears)
        shieldBearers[player->GetGUID()] = twin->GetGUID();
    else
        shieldBearers.erase(player->GetGUID());
}

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
    MirrorStun(player, twin);
    MirrorCurse(player, twin);
    UpdateCompanion(player, twin);
    HandWeaponsToBrother(player);
    UpdateShieldBearer(player, twin);
    if (twin->HasUnitFlag(UNIT_FLAG_STUNNED))
        return;
    MirrorStance(player, twin);
    UpdateSwingPartner(player, twin);
    KeepStandoff(player, twin, motion.Follow);
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
        swingPartners.erase(player->GetGUID());
        shieldBearers.erase(player->GetGUID());
    }
    DismissCompanion(player);
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

bool LonelyTwin(Player const* player)
{
    return HasTwin(player) && !WardrobeOf(player).Enabled;
}

void OfferStoryLater(Player* player);

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

bool OtherHalfAwake(Player const* player)
{
    uint32 const aura = WantedSelfAura(player);
    return aura == TeamingUpAura || aura == DisagreedAura;
}

bool BrotherOut(Player const* player);

bool OnlyOtherHalfShoots(Player const* player)
{
    return OtherHalfAwake(player) && OtherHalfShoots(player) && !MainHalfShoots(player);
}

void UpdateReach(Player* player)
{
    bool const wanted = OnlyOtherHalfShoots(player);
    if (wanted && !player->HasAura(ReachAura))
        player->AddAura(ReachAura, player);
    else if (!wanted && player->HasAura(ReachAura))
        player->RemoveAurasDueToSpell(ReachAura);
}

void HandWeaponsToBrother(Player* player)
{
    if (BrotherOut(player))
        if (Creature* twin = TwinOf(player))
        {
            Wardrobe const dressed = Dressed(player);
            std::array<std::size_t, 3> const slots = { MainHand, OffHand, Ranged };
            for (uint8 index = 0; index < slots.size(); ++index)
                if (uint32 const shown = dressed.Items[slots[index]];
                    twin->GetUInt32Value(UNIT_VIRTUAL_ITEM_SLOT_ID + index) != shown)
                    twin->SetVirtualItem(index, shown);
        }
    uint16 const field = PLAYER_VISIBLE_ITEM_1_ENTRYID + EQUIPMENT_SLOT_RANGED * 2;
    if (Item* bow = OwnBow(player); bow && !player->GetUInt32Value(field))
        player->SetVisibleItemSlot(EQUIPMENT_SLOT_RANGED, bow);
}

void UpdateSelfAura(Player* player)
{
    if (!player->IsAlive())
        return;
    UpdateReach(player);
    HandWeaponsToBrother(player);
    if (!OtherHalfAwake(player))
    {
        player->RemoveAurasDueToSpell(CowboyAura);
        player->RemoveAurasDueToSpell(HalfBrainAura);
    }
    uint32 const wanted = WantedSelfAura(player);
    for (uint32 aura : SelfAuras)
    {
        if (aura == wanted && !player->HasAura(aura))
            player->AddAura(aura, player);
        else if (aura != wanted && player->HasAura(aura))
            player->RemoveAurasDueToSpell(aura);
    }
}

void ArriveLater(Player const* player, uint32 delay)
{
    std::lock_guard<std::mutex> lock(twinMutex);
    arrivals[player->GetGUID().GetCounter()] = delay;
}

bool Arrives(Player const* player, uint32 diff)
{
    std::lock_guard<std::mutex> lock(twinMutex);
    auto itr = arrivals.find(player->GetGUID().GetCounter());
    if (itr == arrivals.end())
        return false;
    if (itr->second > diff)
    {
        itr->second -= diff;
        return false;
    }
    arrivals.erase(itr);
    return true;
}

void OneSipTooMany(Player* player)
{
    if (!HasTwin(player))
        return;
    uint8 const drunk = player->GetDrunkValue();
    uint8 before = 0;
    {
        std::lock_guard<std::mutex> lock(twinMutex);
        uint8& last = sips[player->GetGUID().GetCounter()];
        before = last;
        last = drunk;
    }
    if (drunk < before)
    {
        bool linger = false;
        {
            std::lock_guard<std::mutex> lock(twinMutex);
            bool& skip = hangovers[player->GetGUID().GetCounter()];
            skip = !skip;
            linger = skip;
            if (linger)
                sips[player->GetGUID().GetCounter()] = before;
        }
        if (linger)
            player->SetDrunkValue(before);
        return;
    }
    if (drunk > before && drunk < BlackoutDrunk)
    {
        player->SetDrunkValue(BlackoutDrunk);
        {
            std::lock_guard<std::mutex> lock(twinMutex);
            sips[player->GetGUID().GetCounter()] = BlackoutDrunk;
        }
        ChatHandler(player->GetSession()).SendSysMessage(
            "One sip and the floor is very interesting. Kul Tiran blood, Kul Tiran tolerance: none.");
    }
}

void HoldYourDrink(Player* player)
{
    if (player->getRace() != OgreRace)
        return;
    uint8 const drunk = player->GetDrunkValue();
    uint8 before = 0;
    {
        std::lock_guard<std::mutex> lock(twinMutex);
        before = sips[player->GetGUID().GetCounter()];
    }
    uint8 tolerated = drunk;
    if (drunk > before)
        tolerated = uint8(before + (drunk - before + 1) / 2);
    else if (drunk < before)
        tolerated = uint8(std::max(0, 2 * int32(drunk) - int32(before)));
    tolerated = std::min(tolerated, ToleratedDrunk);
    {
        std::lock_guard<std::mutex> lock(twinMutex);
        sips[player->GetGUID().GetCounter()] = tolerated;
    }
    if (tolerated != drunk)
        player->SetDrunkValue(tolerated);
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
    OneSipTooMany(player);
    HoldYourDrink(player);
}

void LearnRacials(Player* player)
{
    if (HasTwin(player) && !player->HasSpell(BrotherSpell))
        player->learnSpell(BrotherSpell, false);
    if (HasSecondHead(player) && !player->HasSpell(HeadsSpell))
        player->learnSpell(HeadsSpell, false);
    if (HasTwin(player) && !player->HasSpell(CowboySpell))
        player->learnSpell(CowboySpell, false);
    if (HasSecondHead(player) && !player->HasSpell(HalfBrainSpell))
        player->learnSpell(HalfBrainSpell, false);
    if (HasTwin(player) && !player->HasSpell(TwinLinkedSpell))
        player->learnSpell(TwinLinkedSpell, false);
    if (player->getRace() == OgreRace && !player->HasSpell(ToleranceSpell))
        player->learnSpell(ToleranceSpell, false);
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
    RefreshSubclassResources(player);
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
    else if (wardrobe.Enabled)
        OfferStoryLater(player);
    UpdateSelfAura(player);
    RefreshSubclassResources(player);
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

WorldPacket TwinSwingPacket(SwingPartner const& partner, ObjectGuid target, uint32 hitInfo, uint32 school,
    uint32 damage, uint8 targetState)
{
    WorldPacket swing(SMSG_ATTACKERSTATEUPDATE, 48);
    swing << uint32(hitInfo & ~SplitHitFlags);
    swing << partner.Twin.WriteAsPacked();
    swing << target.WriteAsPacked();
    swing << uint32(damage) << uint32(0) << uint8(1);
    swing << uint32(school) << float(damage) << uint32(damage);
    swing << uint8(targetState) << uint32(0) << uint32(0);
    return swing;
}

WorldPacket TwinShotPacket(SwingPartner const& partner, ObjectGuid target)
{
    ItemTemplate const* item = sObjectMgr->GetItemTemplate(partner.ShotItem);
    bool const gun = item && item->SubClass == ITEM_SUBCLASS_WEAPON_GUN;
    WorldPacket shot(SMSG_SPELL_GO, 64);
    shot << partner.Twin.WriteAsPacked() << partner.Twin.WriteAsPacked();
    shot << uint8(0) << uint32(AutoShotSpell) << uint32(CAST_FLAG_UNKNOWN_9 | CAST_FLAG_PROJECTILE);
    shot << uint32(GameTime::GetGameTimeMS().count());
    shot << uint8(1) << target << uint8(0);
    shot << uint32(TARGET_FLAG_UNIT) << target.WriteAsPacked();
    shot << uint32(gun ? BulletProjectileDisplay : ArrowProjectileDisplay) << uint32(INVTYPE_AMMO);
    return shot;
}

WorldPacket TwinShotDamagePacket(SwingPartner const& partner, ObjectGuid target, uint32 hitInfo, uint32 school,
    uint32 damage)
{
    WorldPacket log(SMSG_SPELLNONMELEEDAMAGELOG, 48);
    log << target.WriteAsPacked() << partner.Twin.WriteAsPacked();
    log << uint32(AutoShotSpell) << uint32(damage) << uint32(0) << uint8(school);
    log << uint32(0) << uint32(0) << uint8(1) << uint8(0) << uint32(0);
    log << uint32((hitInfo & HITINFO_CRITICALHIT) ? SPELL_HIT_TYPE_CRIT : 0) << uint8(0);
    return log;
}

bool ShareSwingWithBrother(WorldSession* session, WorldPacket const& packet)
{
    if (resendingAsBrother || packet.GetOpcode() != SMSG_ATTACKERSTATEUPDATE)
        return true;
    WorldPacket reader(packet);
    reader.rpos(0);
    uint32 hitInfo = 0;
    ObjectGuid attacker;
    ObjectGuid target;
    uint32 total = 0;
    uint32 overkill = 0;
    uint8 count = 0;
    reader >> hitInfo >> attacker.ReadAsPacked() >> target.ReadAsPacked() >> total >> overkill >> count;
    if (total < 2 || !count || count > 2)
        return true;
    SwingPartner partner;
    {
        std::lock_guard<std::mutex> lock(twinMutex);
        auto itr = swingPartners.find(attacker);
        if (itr == swingPartners.end())
            return true;
        partner = itr->second;
    }
    std::array<uint32, 2> schools{};
    std::array<uint32, 2> damages{};
    for (uint8 index = 0; index < count; ++index)
    {
        float shown = 0.0f;
        reader >> schools[index] >> shown >> damages[index];
    }
    std::size_t const afterSubs = reader.rpos();
    std::size_t statePos = afterSubs;
    if (hitInfo & (HITINFO_FULL_ABSORB | HITINFO_PARTIAL_ABSORB))
        statePos += 4 * count;
    if (hitInfo & (HITINFO_FULL_RESIST | HITINFO_PARTIAL_RESIST))
        statePos += 4 * count;
    if (statePos >= packet.size())
        return true;
    uint8 const targetState = packet.contents()[statePos];

    uint32 ownTotal = 0;
    WorldPacket own(SMSG_ATTACKERSTATEUPDATE, packet.size());
    std::array<uint32, 2> ownDamages{};
    for (uint8 index = 0; index < count; ++index)
    {
        ownDamages[index] = damages[index] - damages[index] / 2;
        ownTotal += ownDamages[index];
    }
    uint32 const brotherShare = total > ownTotal ? total - ownTotal : 0;
    if (!brotherShare)
        return true;
    own << hitInfo << attacker.WriteAsPacked() << target.WriteAsPacked() << ownTotal << overkill << count;
    for (uint8 index = 0; index < count; ++index)
        own << schools[index] << float(ownDamages[index]) << ownDamages[index];
    own.append(packet.contents() + afterSubs, packet.size() - afterSubs);

    resendingAsBrother = true;
    session->SendPacket(&own);
    if (partner.Style == Swing::Shot)
    {
        WorldPacket shot = TwinShotPacket(partner, target);
        WorldPacket damage = TwinShotDamagePacket(partner, target, hitInfo, schools[0], brotherShare);
        session->SendPacket(&shot);
        session->SendPacket(&damage);
    }
    else
    {
        WorldPacket swing = TwinSwingPacket(partner, target, hitInfo, schools[0], brotherShare, targetState);
        session->SendPacket(&swing);
    }
    resendingAsBrother = false;
    return false;
}

bool ShowHitsOnBrother(WorldSession* session, WorldPacket const& packet)
{
    if (resendingAsBrother || packet.GetOpcode() != SMSG_ATTACKERSTATEUPDATE)
        return true;
    WorldPacket reader(packet);
    reader.rpos(0);
    uint32 hitInfo = 0;
    ObjectGuid attacker;
    ObjectGuid target;
    reader >> hitInfo >> attacker.ReadAsPacked() >> target.ReadAsPacked();
    ObjectGuid bearer;
    {
        std::lock_guard<std::mutex> lock(twinMutex);
        auto itr = shieldBearers.find(target);
        if (itr == shieldBearers.end())
            return true;
        bearer = itr->second;
    }
    WorldPacket shown(SMSG_ATTACKERSTATEUPDATE, packet.size() + 8);
    shown << hitInfo << attacker.WriteAsPacked() << bearer.WriteAsPacked();
    shown.append(packet.contents() + reader.rpos(), packet.size() - reader.rpos());
    resendingAsBrother = true;
    session->SendPacket(&shown);
    resendingAsBrother = false;
    return false;
}

bool ShowCastOnBrother(WorldSession* session, WorldPacket const& packet)
{
    if (resendingAsBrother || !castingAsBrother.Spell ||
        (packet.GetOpcode() != SMSG_SPELL_START && packet.GetOpcode() != SMSG_SPELL_GO))
        return true;
    WorldPacket reader(packet);
    reader.rpos(0);
    ObjectGuid source;
    ObjectGuid caster;
    uint8 castCount = 0;
    uint32 spellId = 0;
    reader >> source.ReadAsPacked();
    reader >> caster.ReadAsPacked();
    std::size_t const afterCasters = reader.rpos();
    reader >> castCount >> spellId;
    if (caster != castingAsBrother.Player || spellId != castingAsBrother.Spell)
        return true;
    WorldPacket shown(packet.GetOpcode(), packet.size());
    shown << castingAsBrother.Twin.WriteAsPacked();
    shown << castingAsBrother.Twin.WriteAsPacked();
    shown.append(packet.contents() + afterCasters, packet.size() - afterCasters);
    ++castingAsBrother.Shown;
    resendingAsBrother = true;
    session->SendPacket(&shown);
    resendingAsBrother = false;
    return false;
}

bool SummonedByOtherHalf(Creature const* creature)
{
    if (!castingAsBrother.Spell || creature->GetEntry() == TwinEntry)
        return false;
    ObjectGuid summoner = creature->GetOwnerGUID();
    if (TempSummon const* summon = creature->ToTempSummon(); summon && !summoner)
        summoner = summon->GetSummonerGUID();
    return summoner == castingAsBrother.Player;
}

void PlantAtBrothersFeet(Creature* creature)
{
    constexpr float AtCasterFeet = 3.0f;
    if (!SummonedByOtherHalf(creature))
        return;
    Player* player = ObjectAccessor::GetPlayer(*creature, castingAsBrother.Player);
    Creature* twin = ObjectAccessor::GetCreature(*creature, castingAsBrother.Twin);
    if (!player || !twin || twin->GetVehicle() || !creature->IsWithinDist(player, AtCasterFeet))
        return;
    Position const feet = twin->GetPosition();
    creature->m_Events.AddEventAtOffset([creature, feet]()
    {
        creature->NearTeleportTo(feet.GetPositionX(), feet.GetPositionY(), feet.GetPositionZ(),
            feet.GetOrientation());
    }, Milliseconds(1));
}

class mod_subclass_twin_creatures : public AllCreatureScript
{
public:
    mod_subclass_twin_creatures() : AllCreatureScript("mod_subclass_twin_creatures") { }

    void OnCreatureAddWorld(Creature* creature) override
    {
        PlantAtBrothersFeet(creature);
    }
};

class mod_subclass_twin_packets : public ServerScript
{
public:
    mod_subclass_twin_packets() : ServerScript("mod_subclass_twin_packets", { SERVERHOOK_CAN_PACKET_RECEIVE,
        SERVERHOOK_CAN_PACKET_SEND }) { }

    bool CanPacketSend(WorldSession* session, WorldPacket const& packet) override
    {
        return !session || (ShowCastOnBrother(session, packet) && ShareSwingWithBrother(session, packet) &&
            ShowHitsOnBrother(session, packet));
    }

    bool CanPacketReceive(WorldSession* session, WorldPacket const& packet) override
    {
        if (session)
            OrderCompanion(session, packet);
        return !session || AnswerMirrorImage(session, packet);
    }
};

void PlayEasy(Player* player, uint32 diff);

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
        if (Arrives(player, diff))
            RefreshTwin(player);
        UpdateCarry(player, diff);
        TickSelfAura(player, diff);
        PlayEasy(player, diff);
    }

    void OnPlayerTextEmote(Player* player, uint32 /*textEmote*/, uint32 /*emoteNum*/, ObjectGuid /*guid*/) override
    {
        HopOffFor(player, AfootStride);
    }

    void OnPlayerAfterSetVisibleItemSlot(Player* player, uint8 slot, Item* /*item*/) override
    {
        MarkRestyle(player);
    }

    void OnPlayerLogin(Player* player) override
    {
        ArriveLater(player, LoginArrivalDelay);
        LearnRacials(player);
        {
            std::lock_guard<std::mutex> lock(twinMutex);
            sips[player->GetGUID().GetCounter()] = player->GetDrunkValue();
        }
        if (SubclassResting(player))
            SetBorrowedPassives(player, false);
        UpdateSelfAura(player);
    }

    void OnPlayerMapChanged(Player* player) override
    {
        DismissTwin(player);
        ArriveLater(player, ArrivalDelay);
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

std::vector<uint32> KnownBorrowedActives(Player const* player);

bool BrotherOut(Player const* player)
{
    return HasTwin(player) && IsSplit(player) && WardrobeOf(player).Enabled && TwinOf(player);
}

bool Affordable(Player const* player, SpellInfo const* info)
{
    if (player->HasSpellCooldown(info->Id))
        return false;
    int32 const cost = info->CalcPowerCost(player, info->GetSchoolMask());
    return cost <= 0 || info->PowerType >= MAX_POWERS || player->GetPower(Powers(info->PowerType)) >= cost;
}

void SetBusy(Player const* player, bool busy, uint32 pause)
{
    std::lock_guard<std::mutex> lock(twinMutex);
    Brain& brain = brains[player->GetGUID().GetCounter()];
    brain.Busy = busy;
    brain.Pause = std::max(brain.Pause, pause);
}

void ActOutCast(Player* player, SpellInfo const* info, Unit* target)
{
    Creature* twin = TwinOf(player);
    if (!twin)
        return;
    if (target && target != player)
        twin->SetFacingToObject(target);
    if (info->HasAttribute(SPELL_ATTR0_USES_RANGED_SLOT))
        if (ItemTemplate const* ranged = RangedOf(player))
        {
            twin->HandleEmoteCommand(RangedEmote(ranged));
            return;
        }
    twin->HandleEmoteCommand(info->IsPositive() ? EMOTE_ONESHOT_SPELL_CAST_OMNI : EMOTE_ONESHOT_SPELL_CAST);
}

void ReleaseOtherHalf(Player* player, uint32 spellId, ObjectGuid targetGuid)
{
    SetBusy(player, false, 0);
    SpellInfo const* info = sSpellMgr->GetSpellInfo(spellId);
    Unit* target = targetGuid == player->GetGUID() ? player : ObjectAccessor::GetUnit(*player, targetGuid);
    if (!info || !target || !player->IsInWorld() || !player->IsAlive() || !OtherHalfAwake(player) ||
        !Affordable(player, info) || Helpless(player))
        return;
    Creature* twin = HasTwin(player) ? TwinOf(player) : nullptr;
    ActOutCast(player, info, target);
    if (twin)
        castingAsBrother = { player->GetGUID(), twin->GetGUID(), spellId };
    SpellCastResult const result = player->CastSpell(target, spellId, TriggerCastFlags(TRIGGERED_IGNORE_GCD |
        TRIGGERED_IGNORE_CAST_IN_PROGRESS | TRIGGERED_CAST_DIRECTLY | TRIGGERED_IGNORE_SET_FACING |
        TRIGGERED_DONT_REPORT_CAST_ERROR));
    LOG_INFO("module", "The other half of {} casts {} ({}) at {}: result {}, {} cast packet(s) shown on the twin",
        player->GetName(), info->SpellName[0], spellId, target->GetName(), uint32(result), castingAsBrother.Shown);
    castingAsBrother = {};
}

bool IsShot(SpellInfo const* info)
{
    constexpr uint32 Shooters = (1 << ITEM_SUBCLASS_WEAPON_BOW) | (1 << ITEM_SUBCLASS_WEAPON_GUN) |
        (1 << ITEM_SUBCLASS_WEAPON_CROSSBOW);
    return info->HasAttribute(SPELL_ATTR0_USES_RANGED_SLOT) ||
        (info->EquippedItemClass == ITEM_CLASS_WEAPON && (info->EquippedItemSubClassMask & Shooters));
}

bool BrotherReaches(Player* player, SpellInfo const* info, Unit* target)
{
    Creature* twin = HasTwin(player) ? TwinOf(player) : nullptr;
    if (!twin || target == player || !IsShot(info))
        return true;
    float const reach = info->GetMaxRange(false, player);
    return (reach <= 0.0f || twin->IsWithinCombatRange(target, reach)) && twin->IsWithinLOSInMap(target);
}

bool OrderOtherHalf(Player* player, uint32 spellId, Unit* target, uint32 pause)
{
    SpellInfo const* info = sSpellMgr->GetSpellInfo(spellId);
    if (!info || !target || !OtherHalfAwake(player) || !Affordable(player, info) || Helpless(player) ||
        !BrotherReaches(player, info, target))
        return false;
    {
        std::lock_guard<std::mutex> lock(twinMutex);
        Brain& brain = brains[player->GetGUID().GetCounter()];
        if (brain.Busy)
            return true;
        brain.Busy = true;
        brain.Pause = std::max(brain.Pause, pause);
    }
    uint32 const castTime = info->CalcCastTime(player);
    if (castTime)
        if (Creature* twin = HasTwin(player) ? TwinOf(player) : nullptr)
        {
            if (target != player)
                twin->SetFacingToObject(target);
            twin->HandleEmoteCommand(EMOTE_ONESHOT_SPELL_PRECAST);
        }
    ObjectGuid const targetGuid = target->GetGUID();
    player->m_Events.AddEventAtOffset([player, spellId, targetGuid]()
    {
        ReleaseOtherHalf(player, spellId, targetGuid);
    }, Milliseconds(std::max<uint32>(castTime, 1)));
    return true;
}

bool OrderBrother(Player* player, uint32 spellId, Unit* target)
{
    if (!HasTwin(player) || !IsSplit(player))
        return false;
    bool const ordered = BrotherOut(player) && OrderOtherHalf(player, spellId, target, ManualPause);
    LOG_INFO("module", "{} presses {} for the brother at {}: {}", player->GetName(), spellId, target->GetName(),
        ordered ? "ordered" : "refused");
    if (!ordered)
    {
        SpellInfo const* info = sSpellMgr->GetSpellInfo(spellId);
        bool const outOfReach = info && BrotherOut(player) && !BrotherReaches(player, info, target);
        ChatHandler(player->GetSession()).SendNotification(outOfReach ?
            "Your brother can't reach that from where he stands." : "Your brother can't cast that right now.");
    }
    return true;
}

std::pair<uint32, Unit*> PickSpell(Player* player)
{
    Unit* foe = TwinTarget(player);
    uint32 attack = 0;
    uint32 attackRecovery = 0;
    uint32 buff = 0;
    for (uint32 spellId : KnownBorrowedActives(player))
    {
        SpellInfo const* info = sSpellMgr->GetSpellInfo(spellId);
        if (!info || !Affordable(player, info))
            continue;
        bool const heals = info->HasEffect(SPELL_EFFECT_HEAL) || info->HasAura(SPELL_AURA_PERIODIC_HEAL);
        if (heals)
        {
            if (player->GetHealthPct() < HealBelowPct)
                return { spellId, player };
            continue;
        }
        if (info->IsPositive())
        {
            if (!buff && info->HasEffect(SPELL_EFFECT_APPLY_AURA) && info->GetDuration() >= LastingBuff &&
                !player->HasAura(spellId))
                buff = spellId;
            continue;
        }
        if (!foe)
            continue;
        float const range = info->GetMaxRange(false, player);
        bool const reaches = IsShot(info) ? BrotherReaches(player, info, foe) :
            (range <= 0.0f || player->IsWithinDistInMap(foe, range)) && player->IsWithinLOSInMap(foe);
        if (!reaches)
            continue;
        uint32 const recovery = std::max(info->RecoveryTime, info->CategoryRecoveryTime);
        if (!attack || recovery > attackRecovery)
        {
            attack = spellId;
            attackRecovery = recovery;
        }
    }
    if (attack)
        return { attack, foe };
    if (buff)
        return { buff, player };
    return { 0, nullptr };
}

void PlayEasy(Player* player, uint32 diff)
{
    {
        std::lock_guard<std::mutex> lock(twinMutex);
        Brain& brain = brains[player->GetGUID().GetCounter()];
        brain.Pause = brain.Pause > diff ? brain.Pause - diff : 0;
        brain.Tick += diff;
        if (brain.Tick < EasyTick)
            return;
        brain.Tick = 0;
        if (brain.Busy || brain.Pause)
            return;
    }
    uint32 const easyAura = HasTwin(player) ? CowboyAura : HalfBrainAura;
    if (!player->HasAura(easyAura) || !player->IsInCombat() || !player->IsAlive() || player->HasStealthAura() ||
        !OtherHalfAwake(player) || (HasTwin(player) && !BrotherOut(player)))
        return;
    auto const [spellId, target] = PickSpell(player);
    if (spellId)
        OrderOtherHalf(player, spellId, target, 0);
}

std::string ToggleEasy(Player* player)
{
    bool const twin = HasTwin(player);
    if (!twin && !HasSecondHead(player))
        return "Only the Thin Twins and two-headed ogres have another half to let loose.";
    if (!OtherHalfAwake(player))
        return twin ? "Your brother has to be with you for that." :
            "Your other head is too busy agreeing with you to do anything on her own.";
    uint32 const aura = twin ? CowboyAura : HalfBrainAura;
    if (player->HasAura(aura))
    {
        player->RemoveAurasDueToSpell(aura);
        return twin ? "You take the reins back from your brother." : "You stop letting the dumb one drive.";
    }
    player->AddAura(aura, player);
    return twin ? "Play easy, cowboy: your brother picks his own spells now." :
        "Two heads, half a brain: your other head does what she likes now.";
}

class spell_subclass_easy : public SpellScript
{
    PrepareSpellScript(spell_subclass_easy);

    void HandleDummy(SpellEffIndex /*effIndex*/)
    {
        if (Player* player = GetCaster() ? GetCaster()->ToPlayer() : nullptr)
            ChatHandler(player->GetSession()).SendSysMessage(ToggleEasy(player));
    }

    void Register() override
    {
        OnEffectHitTarget += SpellEffectFn(spell_subclass_easy::HandleDummy, EFFECT_0, SPELL_EFFECT_DUMMY);
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
    RegisterSpellScript(spell_subclass_easy);
    new mod_subclass_twin_packets();
    new mod_subclass_twin_creatures();
    new mod_subclass_twin_player();
    new mod_subclass_twin_commands();
}
