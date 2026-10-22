/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license.
 */

#include "AllCreatureScript.h"
#include "AscensionAppearanceChanges.h"
#include "CellImpl.h"
#include "Chat.h"
#include "CommandScript.h"
#include "DatabaseEnv.h"
#include "GameTime.h"
#include "GridNotifiersImpl.h"
#include "Group.h"
#include "Log.h"
#include "LootMgr.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "CharmInfo.h"
#include "Pet.h"
#include "Player.h"
#include "PlayerScript.h"
#include "Random.h"
#include "StringConvert.h"
#include "SubclassRules.h"
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
#include <limits>
#include <list>
#include <mutex>
#include <optional>
#include <unordered_map>

using namespace Acore::ChatCommands;

bool SubclassChosen(Player const* player);
bool ForcedBot(Player const* player);
void NoteBotCast(Player const* player, uint32 spellId, uint32 result);
bool OtherHalfShoots(Player const* player);
bool MainHalfShoots(Player const* player);
bool BrotherOut(Player const* player);
void RefreshSubclassResources(Player* player);
bool IsBorrowedSpell(Player const* player, uint32 spellId);
void HandWeaponsToBrother(Player* player);
bool LendsTankSpecialization(Player const* player);
bool LendsRangedSpecialization(Player const* player);
bool SubclassRaceEnabled(uint8 race);

void SendSubclassMessage(Player* player, std::string const& message);
uint32 StowBorrowedGear(Player* player);

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
constexpr uint32 EndlessAmmoAura = 46699;
constexpr uint32 EndlessArrow = 2512;
constexpr uint32 EndlessShot = 2516;

constexpr uint32 TwinLinkedSpell = 9530111;
constexpr uint32 ToleranceSpell = 9530112;
constexpr uint32 HandMeDownsSpell = 9530113;
constexpr uint32 HandMeDownsPercent = 10;
constexpr uint32 HandMeDownsTreasurePercent = 25;
constexpr uint8 ToleratedDrunk = 89;
constexpr uint32 EasyTick = 600;
constexpr uint32 FirstCyclopsHelmet = 9531000;
constexpr uint32 LastCyclopsHelmet = 9539999;
constexpr std::size_t HeadSlot = 0;
constexpr uint32 HeadCategory = 1;
constexpr char const* CyclopsEchoRefusal = "Your heads still share one helmet. Your right head picks her own once "
    "your heads have each chosen their way.";
constexpr uint32 ManualPause = 3000;
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
    std::unordered_map<uint32, uint32> RefusedUntil;
};

constexpr uint32 RefusedSpellRest = 15000;

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

constexpr uint32 CopyShownMs = 4000;
std::mutex shownCopiesMutex;
std::unordered_map<uint64, uint32>& shownCopies = *new std::unordered_map<uint64, uint32>;

uint64 CopyKey(ObjectGuid player, uint32 spellId)
{
    return (uint64(player.GetCounter()) << 32) | spellId;
}

void ShowCastAsCopy(Player const* player, uint32 spellId, bool shown)
{
    std::lock_guard<std::mutex> lock(shownCopiesMutex);
    if (shown)
        shownCopies[CopyKey(player->GetGUID(), spellId)] = GameTime::GetGameTimeMS().count() + CopyShownMs;
    else
        shownCopies.erase(CopyKey(player->GetGUID(), spellId));
}

bool CastShownAsCopy(ObjectGuid caster, uint32 spellId)
{
    if (!caster.IsPlayer())
        return false;
    std::lock_guard<std::mutex> lock(shownCopiesMutex);
    auto const itr = shownCopies.find(CopyKey(caster, spellId));
    if (itr == shownCopies.end())
        return false;
    if (itr->second >= GameTime::GetGameTimeMS().count())
        return true;
    shownCopies.erase(itr);
    return false;
}

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
    Item* ranged = player->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_RANGED);
    return ranged && ranged->GetTemplate()->Class == ITEM_CLASS_WEAPON ? ranged : nullptr;
}

bool HeldInHand(Item const* ranged)
{
    switch (ranged->GetTemplate()->SubClass)
    {
        case ITEM_SUBCLASS_WEAPON_BOW:
        case ITEM_SUBCLASS_WEAPON_GUN:
        case ITEM_SUBCLASS_WEAPON_CROSSBOW:
            return true;
        default:
            return false;
    }
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
    return player->getRace() == TwinRace && player->getGender() == GENDER_MALE && SubclassRaceEnabled(TwinRace);
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

uint32 ShownCyclopsHelmet(Player const* player);
bool HasSecondHead(Player const* player);

std::string TwinWearing(Player const* player)
{
    std::string reply = "TWINWEAR";
    if (HasSecondHead(player))
        return reply + Acore::StringFormat(" {}", ShownCyclopsHelmet(player));
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
    return player->getRace() == OgreRace && player->getGender() == GENDER_FEMALE && SubclassRaceEnabled(OgreRace);
}

bool LonelyTwin(Player const* player)
{
    return HasTwin(player) && !WardrobeOf(player).Enabled;
}

void OfferStoryLater(Player* player);

bool SubclassResting(Player const* player)
{
    if (ForcedBot(player))
        return false;
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

bool BotPlaysOtherHalf(Player const* player)
{
    return ForcedBot(player) && SubclassChosen(player);
}

bool OtherHalfActs(Player const* player)
{
    return OtherHalfAwake(player) || BotPlaysOtherHalf(player);
}

bool BrotherOut(Player const* player);

bool OnlyOtherHalfShoots(Player const* player)
{
    return OtherHalfAwake(player) && OtherHalfShoots(player) && !MainHalfShoots(player);
}

uint32 EndlessAmmoFor(Player const* player)
{
    Item const* ranged = OwnBow(player);
    if (!ranged)
        return 0;
    switch (ranged->GetTemplate()->SubClass)
    {
        case ITEM_SUBCLASS_WEAPON_BOW:
        case ITEM_SUBCLASS_WEAPON_CROSSBOW:
            return EndlessArrow;
        case ITEM_SUBCLASS_WEAPON_GUN:
            return EndlessShot;
        default:
            return 0;
    }
}

bool HasUsableAmmo(Player* player)
{
    uint32 const loaded = player->GetUInt32Value(PLAYER_AMMO_ID);
    ItemTemplate const* proto = loaded ? sObjectMgr->GetItemTemplate(loaded) : nullptr;
    return proto && player->HasItemCount(loaded, 1) && player->CheckAmmoCompatibility(proto);
}

void DropOtherEndlessAmmo(Player* player, uint32 ammo)
{
    uint32 const other = ammo == EndlessArrow ? EndlessShot : EndlessArrow;
    if (uint32 const count = player->GetItemCount(other))
        player->DestroyItemCount(other, count, true);
}

void LoadEndlessAmmo(Player* player, bool wanted)
{
    uint32 const ammo = wanted ? EndlessAmmoFor(player) : 0;
    if (!ammo)
        return;
    if (HasUsableAmmo(player))
    {
        if (player->GetUInt32Value(PLAYER_AMMO_ID) == ammo)
            DropOtherEndlessAmmo(player, ammo);
        return;
    }
    if (!player->HasItemCount(ammo, 1))
    {
        ItemPosCountVec destination;
        if (player->CanStoreNewItem(NULL_BAG, NULL_SLOT, destination, ammo, 1) != EQUIP_ERR_OK)
            return;
        if (Item* item = player->StoreNewItem(destination, ammo, true))
            player->SendNewItem(item, 1, true, false);
    }
    player->RemoveAurasDueToSpell(EndlessAmmoAura);
    player->SetAmmo(ammo);
}

void UpdateReach(Player* player)
{
    bool const wanted = OnlyOtherHalfShoots(player);
    LoadEndlessAmmo(player, wanted);
    for (uint32 aura : { ReachAura, EndlessAmmoAura })
    {
        if (wanted && !player->HasAura(aura) && sSpellMgr->GetSpellInfo(aura))
            player->AddAura(aura, player);
        else if (!wanted && player->HasAura(aura))
            player->RemoveAurasDueToSpell(aura);
    }
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
    if (Item* bow = OwnBow(player); bow && !player->GetUInt32Value(field) && !HasSecondHead(player))
        player->SetVisibleItemSlot(EQUIPMENT_SLOT_RANGED, bow);
}

bool OtherHeadShoots(Player* player)
{
    return HasSecondHead(player) && OnlyOtherHalfShoots(player) && OwnBow(player);
}

bool IsBow(Item const* gun)
{
    return gun->GetTemplate()->SubClass == ITEM_SUBCLASS_WEAPON_BOW;
}

struct StowedLooks
{
    std::optional<uint32> MainHand;
    std::optional<uint32> Ranged;
    uint32 PlacedMainHand = 0;
};

std::unordered_map<uint32, StowedLooks>& stowedLooks = *new std::unordered_map<uint32, StowedLooks>;

std::mutex& gunsMutex = *new std::mutex;
std::unordered_set<uint32>& gunsInHand = *new std::unordered_set<uint32>;

bool GunInHand(Player const* player)
{
    std::lock_guard<std::mutex> lock(gunsMutex);
    return gunsInHand.count(player->GetGUID().GetCounter());
}

void NoteGunInHand(Player const* player, bool held)
{
    std::lock_guard<std::mutex> lock(gunsMutex);
    if (held)
        gunsInHand.insert(player->GetGUID().GetCounter());
    else
        gunsInHand.erase(player->GetGUID().GetCounter());
}

void ForgetStowedLook(Player const* player, uint8 slot)
{
    std::lock_guard<std::mutex> lock(twinMutex);
    auto const itr = stowedLooks.find(player->GetGUID().GetCounter());
    if (itr == stowedLooks.end())
        return;
    if (slot == EQUIPMENT_SLOT_MAINHAND)
        itr->second.MainHand.reset();
    else if (slot == EQUIPMENT_SLOT_RANGED)
        itr->second.Ranged.reset();
    if (!itr->second.MainHand && !itr->second.Ranged)
        stowedLooks.erase(itr);
}

uint32 ShownIn(Player const* player, uint8 slot)
{
    return player->GetUInt32Value(PLAYER_VISIBLE_ITEM_1_ENTRYID + slot * 2);
}

void SetShown(Player* player, uint8 slot, uint32 entry)
{
    if (ShownIn(player, slot) != entry)
        player->SetUInt32Value(PLAYER_VISIBLE_ITEM_1_ENTRYID + slot * 2, entry);
}

constexpr uint32 FirstHeldWeapon = 9540000;
constexpr uint32 LastHeldWeapon = 9549999;

std::unordered_map<uint32, uint32> const& HeldWeaponAuras()
{
    static std::unordered_map<uint32, uint32> const auras = []
    {
        std::unordered_map<uint32, uint32> loaded;
        bool const exists = bool(WorldDatabase.Query("SELECT 1 FROM information_schema.TABLES WHERE "
            "TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'mod_subclass_held_weapon'"));
        if (exists)
            if (QueryResult result =
                    WorldDatabase.Query("SELECT `DisplayId`, `SpellId` FROM `mod_subclass_held_weapon`"))
                do
                {
                    Field* fields = result->Fetch();
                    loaded[fields[0].Get<uint32>()] = fields[1].Get<uint32>();
                } while (result->NextRow());
        LOG_INFO("module", "mod-subclass: {} weapon looks she can hold while casting", loaded.size());
        return loaded;
    }();
    return auras;
}

uint32 HeldWeaponAura(uint32 itemId)
{
    auto const& auras = HeldWeaponAuras();
    auto const itr = itemId ? auras.find(DisplayOfItem(itemId)) : auras.end();
    return itr != auras.end() && sSpellMgr->GetSpellInfo(itr->second) ? itr->second : 0;
}

void HoldWeaponLook(Player* player, uint32 itemId)
{
    uint32 const wanted = player->IsAlive() ? HeldWeaponAura(itemId) : 0;
    std::vector<uint32> stale;
    for (auto const& [spellId, application] : player->GetAppliedAuras())
        if (spellId >= FirstHeldWeapon && spellId <= LastHeldWeapon && spellId != wanted)
            stale.push_back(spellId);
    for (uint32 spellId : stale)
        player->RemoveAurasDueToSpell(spellId);
    if (wanted && !player->HasAura(wanted))
        player->AddAura(wanted, player);
}

std::unordered_set<uint32>& castsNoted = *new std::unordered_set<uint32>;

void NoteHands(Player* player, char const* moment)
{
    LOG_INFO("module", "mod-subclass: {} {}: main {}, off {}, ranged {}, sheath {}, emote state {}, gun in hand {}, "
        "melee {}, casting {}", player->GetName(), moment, ShownIn(player, EQUIPMENT_SLOT_MAINHAND),
        ShownIn(player, EQUIPMENT_SLOT_OFFHAND), ShownIn(player, EQUIPMENT_SLOT_RANGED), uint32(player->GetSheath()),
        player->GetUInt32Value(UNIT_NPC_EMOTESTATE), GunInHand(player),
        player->GetVictim() && player->IsWithinMeleeRange(player->GetVictim()),
        player->HasUnitState(UNIT_STATE_CASTING));
}

std::unordered_set<uint32>& emptyHanded = *new std::unordered_set<uint32>;

void NoteEmptyHands(Player* player)
{
    bool const armed = player->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_MAINHAND);
    bool const empty = armed && !ShownIn(player, EQUIPMENT_SLOT_MAINHAND) && !ShownIn(player, EQUIPMENT_SLOT_OFFHAND);
    bool fresh = false;
    {
        std::lock_guard<std::mutex> lock(twinMutex);
        uint32 const guid = player->GetGUID().GetCounter();
        fresh = empty ? emptyHanded.insert(guid).second : (emptyHanded.erase(guid), false);
    }
    if (fresh)
        NoteHands(player, "shows no weapon in hand");
}

void NoteCastLook(Player* player, bool freeOffHand, uint32 heldLook)
{
    bool const casting = player->HasUnitState(UNIT_STATE_CASTING);
    {
        std::lock_guard<std::mutex> lock(twinMutex);
        uint32 const guid = player->GetGUID().GetCounter();
        if (!casting)
        {
            castsNoted.erase(guid);
            return;
        }
        if (!castsNoted.insert(guid).second)
            return;
    }
    uint32 const main = ShownIn(player, EQUIPMENT_SLOT_MAINHAND);
    LOG_INFO("module", "mod-subclass: {} casts with off hand free {}, other half awake {}, sheath {}, main look {}, "
        "held look {} (display {}, aura {})", player->GetName(), freeOffHand, OtherHalfAwake(player),
        uint32(player->GetSheath()), main, heldLook, DisplayOfItem(heldLook), HeldWeaponAura(heldLook));
}

bool CastsOwnSpell(Player const* player)
{
    for (CurrentSpellTypes type : { CURRENT_GENERIC_SPELL, CURRENT_CHANNELED_SPELL })
        if (Spell const* spell = player->GetCurrentSpell(type))
            return !IsBorrowedSpell(player, spell->GetSpellInfo()->Id);
    return false;
}

bool CastsWithOtherHeadArmed(Player const* player, bool freeOffHand)
{
    return freeOffHand && player->IsAlive() && OtherHalfAwake(player) && player->HasUnitState(UNIT_STATE_CASTING) &&
        player->IsInCombat() && CastsOwnSpell(player);
}

bool HoldsHerMaceForTheCast(Player const* player)
{
    return CastsWithOtherHeadArmed(player, !player->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_OFFHAND));
}

bool ShowsAsCopy(Player const* player, SpellInfo const* info)
{
    if (info->DmgClass == SPELL_DAMAGE_CLASS_MELEE)
        return HoldsHerMaceForTheCast(player);
    if (!info->HasAttribute(SPELL_ATTR0_USES_RANGED_SLOT))
        return true;
    Item const* gun = OwnBow(player);
    return gun && !IsBow(gun);
}

void HoldTheGun(Player* player, bool afar)
{
    uint32 const guid = player->GetGUID().GetCounter();
    Item* gun = OwnBow(player);
    if (gun && !HeldInHand(gun))
        gun = nullptr;
    bool const freeOffHand = !player->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_OFFHAND);
    bool const casting = CastsWithOtherHeadArmed(player, freeOffHand);
    bool const aimingAfar = afar && OtherHeadShoots(player);
    bool const drawn = gun && freeOffHand && player->IsAlive() && OtherHeadShoots(player) &&
        player->GetSheath() == SHEATH_STATE_MELEE && !casting;
    afar = afar && drawn;
    uint32 const shownMain = ShownIn(player, EQUIPMENT_SLOT_MAINHAND);
    uint32 const shownRanged = ShownIn(player, EQUIPMENT_SLOT_RANGED);
    uint32 offHand = 0;
    uint32 placed = 0;
    uint32 heldLook = 0;
    bool placing = false;
    bool rangedAway = false;
    bool gunHeld = false;
    std::optional<uint32> restoreMain;
    std::optional<uint32> restoreRanged;
    {
        std::lock_guard<std::mutex> lock(twinMutex);
        StowedLooks& stowed = stowedLooks[guid];
        uint32 const mace = stowed.MainHand.value_or(shownMain);
        uint32 const gunLook = gun ? stowed.Ranged.value_or(shownRanged ? shownRanged : gun->GetEntry()) : 0;
        if (casting)
            heldLook = aimingAfar && gunLook ? gunLook : mace ? mace : gunLook;
        bool const handedOver = heldLook != 0;
        rangedAway = drawn || (handedOver && heldLook == gunLook);
        if (rangedAway && shownRanged)
            stowed.Ranged = shownRanged;
        placing = afar || handedOver;
        placed = handedOver || (gun && IsBow(gun)) ? 0 : mace;
        if (placing && !stowed.MainHand && shownMain != placed)
            stowed.MainHand = shownMain;
        if (placing)
            stowed.PlacedMainHand = placed;
        if (!placing && stowed.MainHand)
        {
            if (shownMain == stowed.PlacedMainHand)
                restoreMain = stowed.MainHand;
            stowed.MainHand.reset();
        }
        if (!rangedAway && stowed.Ranged)
        {
            if (!shownRanged)
                restoreRanged = stowed.Ranged;
            stowed.Ranged.reset();
        }
        offHand = drawn && gun ? gunLook : 0;
        gunHeld = offHand || (handedOver && heldLook == gunLook);
        if (!stowed.MainHand && !stowed.Ranged)
            stowedLooks.erase(guid);
    }
    if (rangedAway)
        SetShown(player, EQUIPMENT_SLOT_RANGED, 0);
    else if (restoreRanged)
        SetShown(player, EQUIPMENT_SLOT_RANGED, *restoreRanged);
    if (placing)
        SetShown(player, EQUIPMENT_SLOT_MAINHAND, placed);
    else if (restoreMain)
        SetShown(player, EQUIPMENT_SLOT_MAINHAND, *restoreMain);
    if (freeOffHand)
        SetShown(player, EQUIPMENT_SLOT_OFFHAND, offHand);
    NoteGunInHand(player, gunHeld);
    HoldWeaponLook(player, heldLook);
    NoteCastLook(player, freeOffHand, heldLook);
    NoteEmptyHands(player);
}

constexpr float AlongShotRange = 35.0f;

WorldPacket ProjectilePacket(ObjectGuid shooter, uint32 spellId, uint32 shotItem, ObjectGuid target, bool ammo = true)
{
    ItemTemplate const* item = sObjectMgr->GetItemTemplate(shotItem);
    bool const gun = item && item->SubClass == ITEM_SUBCLASS_WEAPON_GUN;
    WorldPacket shot(SMSG_SPELL_GO, 64);
    shot << shooter.WriteAsPacked() << shooter.WriteAsPacked();
    shot << uint8(0) << uint32(spellId) << uint32(CAST_FLAG_UNKNOWN_9 | (ammo ? CAST_FLAG_PROJECTILE : 0));
    shot << uint32(GameTime::GetGameTimeMS().count());
    shot << uint8(1) << target << uint8(0);
    shot << uint32(TARGET_FLAG_UNIT) << target.WriteAsPacked();
    if (ammo)
        shot << uint32(gun ? BulletProjectileDisplay : ArrowProjectileDisplay) << uint32(INVTYPE_AMMO);
    return shot;
}

void MeleeUpClose(Player* player)
{
    Spell* repeating = player->GetCurrentSpell(CURRENT_AUTOREPEAT_SPELL);
    Unit* victim = repeating ? repeating->m_targets.GetUnitTarget() : nullptr;
    if (!repeating || repeating->m_spellInfo->Id != AutoShotSpell || !victim || !OtherHeadShoots(player) ||
        !player->IsWithinMeleeRange(victim))
        return;
    player->InterruptSpell(CURRENT_AUTOREPEAT_SPELL);
    player->Attack(victim, true);
}

constexpr uint32 AimAfterShotMs = 2500;
std::unordered_map<uint32, uint32>& aimingUntil = *new std::unordered_map<uint32, uint32>;

void NoteOtherHeadShot(Player const* player)
{
    uint32 const now = GameTime::GetGameTimeMS().count();
    std::lock_guard<std::mutex> lock(twinMutex);
    aimingUntil[player->GetGUID().GetCounter()] = now + AimAfterShotMs;
}

struct AttackIntent
{
    ObjectGuid Foe;
    uint32 AskedAt = 0;
    uint32 StoppedAt = 0;
};

std::unordered_map<uint32, AttackIntent>& attackIntents = *new std::unordered_map<uint32, AttackIntent>;

void NoteAttackIntent(WorldSession* session, WorldPacket const& packet)
{
    Player* player = session->GetPlayer();
    if (!player || packet.GetOpcode() != CMSG_ATTACKSWING || packet.size() < sizeof(uint64))
        return;
    std::lock_guard<std::mutex> lock(twinMutex);
    AttackIntent& intent = attackIntents[player->GetGUID().GetCounter()];
    intent.Foe = ObjectGuid(packet.read<uint64>(0));
    intent.AskedAt = GameTime::GetGameTimeMS().count();
}

void NoteAttackStopped(WorldSession* session, WorldPacket const& packet)
{
    Player* player = session->GetPlayer();
    if (!player || packet.GetOpcode() != SMSG_ATTACKSTOP)
        return;
    std::lock_guard<std::mutex> lock(twinMutex);
    auto const itr = attackIntents.find(player->GetGUID().GetCounter());
    if (itr != attackIntents.end())
        itr->second.StoppedAt = GameTime::GetGameTimeMS().count();
}

void AimAtTarget(Player* player)
{
    std::lock_guard<std::mutex> lock(twinMutex);
    AttackIntent& intent = attackIntents[player->GetGUID().GetCounter()];
    intent.Foe = player->GetTarget();
    intent.AskedAt = GameTime::GetGameTimeMS().count();
}

AttackIntent IntentOf(Player const* player)
{
    std::lock_guard<std::mutex> lock(twinMutex);
    auto const itr = attackIntents.find(player->GetGUID().GetCounter());
    return itr == attackIntents.end() ? AttackIntent() : itr->second;
}

Unit* IntendedFoe(Player* player)
{
    ObjectGuid const foe = IntentOf(player).Foe;
    if (!foe || player->GetTarget() != foe)
        return nullptr;
    Unit* unit = ObjectAccessor::GetUnit(*player, foe);
    return unit && unit->IsAlive() && player->IsValidAttackTarget(unit) ? unit : nullptr;
}

Unit* AimedAt(Player* player)
{
    if (Unit* victim = player->GetVictim())
        return victim;
    if (Spell* repeating = player->GetCurrentSpell(CURRENT_AUTOREPEAT_SPELL))
        return repeating->m_targets.GetUnitTarget();
    return IntendedFoe(player);
}

void AimFromAfar(Player* player)
{
    Unit* target = AimedAt(player);
    Item* gun = OwnBow(player);
    bool const aiming = target && gun && player->IsAlive() && OtherHeadShoots(player) &&
        player->GetSheath() == SHEATH_STATE_MELEE &&
        target->IsAlive() && !player->IsWithinMeleeRange(target) && !player->isMoving() &&
        !player->IsNonMeleeSpellCast(false, true, true);
    uint32 const wanted = !aiming ? 0 : IsBow(gun) ? EMOTE_STATE_READY_BOW : EMOTE_STATE_READY_RIFLE;
    uint32 const current = player->GetUInt32Value(UNIT_NPC_EMOTESTATE);
    bool const ours = current == EMOTE_STATE_READY_BOW || current == EMOTE_STATE_READY_RIFLE;
    if (current == wanted || (!wanted && !ours))
        return;
    player->SetUInt32Value(UNIT_NPC_EMOTESTATE, wanted);
    LOG_INFO("module", "mod-subclass: {} {} aiming at {} (victim {}, in melee range {}, in combat {})",
        player->GetName(), wanted ? "starts" : "stops", target ? target->GetName() : "nobody",
        player->GetVictim() ? player->GetVictim()->GetName() : "none",
        target && player->IsWithinMeleeRange(target), player->IsInCombat());
    if (wanted && !player->IsInCombat())
    {
        player->ForceValuesUpdateAtIndex(UNIT_FIELD_BYTES_2);
        player->ForceValuesUpdateAtIndex(PLAYER_VISIBLE_ITEM_1_ENTRYID + EQUIPMENT_SLOT_OFFHAND * 2);
    }
}

bool RecentlyShot(Player const* player)
{
    std::lock_guard<std::mutex> lock(twinMutex);
    auto const itr = aimingUntil.find(player->GetGUID().GetCounter());
    return itr != aimingUntil.end() && itr->second > GameTime::GetGameTimeMS().count();
}

bool ShootsFromAfar(Player* player)
{
    if (!OtherHeadShoots(player) || !(player->GetCurrentSpell(CURRENT_AUTOREPEAT_SPELL) || RecentlyShot(player)))
        return false;
    Unit* victim = player->GetVictim();
    return !victim || !player->IsWithinMeleeRange(victim);
}

bool AimsFromAfar(Player* player)
{
    Unit* target = AimedAt(player);
    return target && OtherHeadShoots(player) && !player->IsWithinMeleeRange(target);
}

std::unordered_set<uint32>& drawnToAim = *new std::unordered_set<uint32>;

void NoteSheathRequest(WorldSession* session, WorldPacket const& packet)
{
    Player* player = session->GetPlayer();
    if (!player || packet.GetOpcode() != CMSG_SET_SHEATHED || packet.size() < sizeof(uint32) ||
        !HasSecondHead(player))
        return;
    LOG_INFO("module", "mod-subclass: {} switches her own sheath to {} (server had {})", player->GetName(),
        packet.read<uint32>(0), uint32(player->GetSheath()));
}

void DrawForTheFight(Player* player)
{
    if (!HasSecondHead(player))
        return;
    MeleeUpClose(player);
    bool const fighting = (player->HasUnitState(UNIT_STATE_MELEE_ATTACKING) && player->GetVictim()) ||
        ShootsFromAfar(player) || (OtherHeadShoots(player) && IntendedFoe(player));
    bool const aimingOnly = fighting && !player->IsInCombat();
    bool drewToAim = false;
    {
        std::lock_guard<std::mutex> lock(twinMutex);
        uint32 const guid = player->GetGUID().GetCounter();
        drewToAim = drawnToAim.count(guid);
        if (aimingOnly)
            drawnToAim.insert(guid);
        else
            drawnToAim.erase(guid);
    }
    if (aimingOnly && !drewToAim && player->GetSheath() != SHEATH_STATE_MELEE &&
        !player->IsNonMeleeSpellCast(false, true, true))
    {
        NoteHands(player, "draws mace and gun to aim");
        player->SetSheath(SHEATH_STATE_MELEE);
    }
    else if (!fighting && drewToAim && !player->IsInCombat())
        player->SetSheath(SHEATH_STATE_UNARMED);
    HoldTheGun(player, fighting && AimsFromAfar(player));
    AimFromAfar(player);
}

void ShootAlong(Player* player)
{
    DrawForTheFight(player);
    Unit* victim = player->GetVictim();
    Item* gun = OwnBow(player);
    if (!gun || IsBow(gun) || !OtherHeadShoots(player) || !GunInHand(player) || !victim ||
        !player->HasUnitState(UNIT_STATE_MELEE_ATTACKING) ||
        !player->IsValidAttackTarget(victim) || player->GetCurrentSpell(CURRENT_AUTOREPEAT_SPELL) ||
        !player->isAttackReady(RANGED_ATTACK) || !player->IsWithinDistInMap(victim, AlongShotRange) ||
        !player->IsWithinLOSInMap(victim))
        return;
    player->resetAttackTimer(RANGED_ATTACK);
    ShowCastAsCopy(player, AutoShotSpell, true);
    WorldPacket shot = ProjectilePacket(player->GetGUID(), AutoShotSpell, gun->GetEntry(), victim->GetGUID(), false);
    player->SendMessageToSet(&shot, true);
    player->HandleEmoteCommand(EMOTE_ONESHOT_ATTACK_OFF_PIERCE);
    bool const critical = roll_chance_f(player->GetUnitCriticalChance(RANGED_ATTACK, victim));
    uint32 damage = Unit::CalcArmorReducedDamage(player, victim, player->CalculateDamage(RANGED_ATTACK, false, true),
        nullptr, 0, RANGED_ATTACK);
    if (critical)
        damage *= 2;
    if (SpellInfo const* autoShot = sSpellMgr->GetSpellInfo(AutoShotSpell))
        player->SendSpellNonMeleeDamageLog(victim, autoShot, damage, SPELL_SCHOOL_MASK_NORMAL, 0, 0, true, 0, critical);
    Unit::DealDamage(player, victim, damage, nullptr, DIRECT_DAMAGE, SPELL_SCHOOL_MASK_NORMAL, nullptr, true);
}

void UpdateSelfAura(Player* player)
{
    if (!player->IsAlive())
        return;
    UpdateReach(player);
    StowBorrowedGear(player);
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

struct CyclopsLook
{
    uint32 SpellId = 0;
    std::string Model;
};

std::unordered_map<uint32, CyclopsLook> const& CyclopsHelmetLooks()
{
    static std::unordered_map<uint32, CyclopsLook> const looks = []
    {
        std::unordered_map<uint32, CyclopsLook> loaded;
        bool const hasModel = bool(WorldDatabase.Query("SELECT 1 FROM information_schema.COLUMNS WHERE "
            "TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'mod_subclass_cyclops_helmet' AND COLUMN_NAME = 'Model'"));
        if (QueryResult result = WorldDatabase.Query(hasModel ?
                "SELECT `DisplayId`, `SpellId`, `Model` FROM `mod_subclass_cyclops_helmet`" :
                "SELECT `DisplayId`, `SpellId`, '' FROM `mod_subclass_cyclops_helmet`"))
            do
            {
                Field* fields = result->Fetch();
                loaded[fields[0].Get<uint32>()] = { fields[1].Get<uint32>(), fields[2].Get<std::string>() };
            } while (result->NextRow());
        LOG_INFO("module", "mod-subclass: {} helmet looks for the cyclops head", loaded.size());
        return loaded;
    }();
    return looks;
}

uint32 ShownCyclopsHelmetItem(Player const* player)
{
    if (!HasSecondHead(player))
        return 0;
    return Subclass::CyclopsHelmet(IsSplit(player) && AscensionShowsItemAppearances(const_cast<Player*>(player)),
        player->HasPlayerFlag(PLAYER_FLAGS_HIDE_HELM),
        player->GetUInt32Value(PLAYER_VISIBLE_ITEM_1_ENTRYID + EQUIPMENT_SLOT_HEAD * 2),
        WardrobeOf(player).Items[HeadSlot]);
}

CyclopsLook const* CyclopsLookOfItem(uint32 itemId)
{
    auto const& looks = CyclopsHelmetLooks();
    auto const itr = itemId ? looks.find(DisplayOfItem(itemId)) : looks.end();
    return itr == looks.end() ? nullptr : &itr->second;
}

uint32 WantedCyclopsHelmetAura(Player const* player)
{
    CyclopsLook const* look = CyclopsLookOfItem(ShownCyclopsHelmetItem(player));
    return look && sSpellMgr->GetSpellInfo(look->SpellId) ? look->SpellId : 0;
}

std::string CyclopsModelState(Player const* player)
{
    CyclopsLook const* look = HasSecondHead(player) ? CyclopsLookOfItem(ShownCyclopsHelmetItem(player)) : nullptr;
    return "CYCLOPSMODEL " + (look && !look->Model.empty() ? look->Model : std::string("-"));
}

std::string CyclopsPreview(uint32 itemId)
{
    CyclopsLook const* look = CyclopsLookOfItem(itemId);
    return Acore::StringFormat("CYCLOPSPREVIEW {} {}", itemId, look && !look->Model.empty() ? look->Model : "-");
}

constexpr uint8 LeftMouth = 1;
constexpr uint8 RightMouth = 2;

std::unordered_map<uint32, uint8> const& MouthCovers()
{
    static std::unordered_map<uint32, uint8> const covers = []
    {
        std::unordered_map<uint32, uint8> loaded;
        bool const present = bool(WorldDatabase.Query("SELECT 1 FROM information_schema.TABLES WHERE "
            "TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'mod_subclass_mouth_cover'"));
        if (QueryResult result = present ?
                WorldDatabase.Query("SELECT `DisplayId`, `Heads` FROM `mod_subclass_mouth_cover`") : nullptr)
            do
            {
                Field* fields = result->Fetch();
                loaded[fields[0].Get<uint32>()] = fields[1].Get<uint8>();
            } while (result->NextRow());
        LOG_INFO("module", "mod-subclass: {} helmet looks cover an ogre mouth", loaded.size());
        return loaded;
    }();
    return covers;
}

bool CoversMouth(uint32 itemId, uint8 mouth)
{
    if (!itemId)
        return false;
    auto const& covers = MouthCovers();
    auto const itr = covers.find(DisplayOfItem(itemId));
    return itr != covers.end() && (itr->second & mouth);
}

void HideCoveredJaws(Player* player)
{
    if (!HasSecondHead(player) || player->GetSession()->PlayerLogout())
        return;
    uint32 const leftHelmet = player->HasPlayerFlag(PLAYER_FLAGS_HIDE_HELM) ? 0 :
        player->GetUInt32Value(PLAYER_VISIBLE_ITEM_1_ENTRYID + EQUIPMENT_SLOT_HEAD * 2);
    uint8 const jaws = (CoversMouth(leftHelmet, LeftMouth) ? LeftMouth : 0) |
        (CoversMouth(ShownCyclopsHelmetItem(player), RightMouth) ? RightMouth : 0);
    if (player->GetByteValue(PLAYER_BYTES_2, PLAYER_BYTES_2_OFFSET_FACIAL_STYLE) != jaws)
        player->SetByteValue(PLAYER_BYTES_2, PLAYER_BYTES_2_OFFSET_FACIAL_STYLE, jaws);
}

void ShowCharacterSelectJaws(Player* player)
{
    if (!HasSecondHead(player))
        return;
    uint32 const leftHelmet = player->HasPlayerFlag(PLAYER_FLAGS_HIDE_HELM) ? 0 :
        player->GetUInt32Value(PLAYER_VISIBLE_ITEM_1_ENTRYID + EQUIPMENT_SLOT_HEAD * 2);
    player->SetByteValue(PLAYER_BYTES_2, PLAYER_BYTES_2_OFFSET_FACIAL_STYLE,
        CoversMouth(leftHelmet, LeftMouth) ? LeftMouth : 0);
}

void UpdateCyclopsHelmet(Player* player)
{
    HideCoveredJaws(player);
    if (!player->IsAlive())
        return;
    uint32 const wanted = WantedCyclopsHelmetAura(player);
    std::vector<uint32> stale;
    for (auto const& [spellId, application] : player->GetAppliedAuras())
        if (spellId >= FirstCyclopsHelmet && spellId <= LastCyclopsHelmet && spellId != wanted)
            stale.push_back(spellId);
    for (uint32 spellId : stale)
        player->RemoveAurasDueToSpell(spellId);
    bool const changed = !stale.empty() || (wanted && !player->HasAura(wanted));
    if (wanted && !player->HasAura(wanted))
        player->AddAura(wanted, player);
    if (changed)
        SendSubclassMessage(player, CyclopsModelState(player));
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
    if (player->IsAlive())
        ShootAlong(player);
    else if (HasSecondHead(player))
    {
        HoldTheGun(player, false);
        AimFromAfar(player);
    }
    {
        std::lock_guard<std::mutex> lock(twinMutex);
        uint32& elapsed = auraTicks[player->GetGUID().GetCounter()];
        elapsed += diff;
        if (elapsed < SelfAuraTick)
            return;
        elapsed = 0;
    }
    UpdateSelfAura(player);
    UpdateCyclopsHelmet(player);
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
    if (player->getRace() == OgreRace && SubclassRaceEnabled(OgreRace) && !player->HasSpell(ToleranceSpell))
        player->learnSpell(ToleranceSpell, false);
    if (player->getRace() == TwinRace && SubclassRaceEnabled(TwinRace) && !player->HasSpell(HandMeDownsSpell))
        player->learnSpell(HandMeDownsSpell, false);
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

uint32 ShownCyclopsHelmet(Player const* player)
{
    return Subclass::CyclopsHelmet(IsSplit(player), player->HasPlayerFlag(PLAYER_FLAGS_HIDE_HELM),
        player->GetUInt32Value(PLAYER_VISIBLE_ITEM_1_ENTRYID + EQUIPMENT_SLOT_HEAD * 2),
        WardrobeOf(player).Items[HeadSlot]);
}

void ChooseCyclopsHelmet(Player* player, uint32 helmet, std::string_view done)
{
    if (!IsSplit(player))
    {
        ChatHandler(player->GetSession()).SendSysMessage(CyclopsEchoRefusal);
        return;
    }
    Wardrobe wardrobe = WardrobeOf(player);
    wardrobe.Items[HeadSlot] = helmet;
    StoreWardrobe(player, wardrobe);
    UpdateCyclopsHelmet(player);
    ChatHandler(player->GetSession()).SendSysMessage(done);
}

void KeepRightHelmet(Player* player)
{
    if (IsSplit(player) && !WardrobeOf(player).Items[HeadSlot])
    {
        uint32 const shown = ShownCyclopsHelmet(player);
        Wardrobe wardrobe = WardrobeOf(player);
        wardrobe.Items[HeadSlot] = shown ? shown : Subclass::BareCyclopsHead;
        StoreWardrobe(player, wardrobe);
    }
}

void DressLeftHead(Player* player, std::string_view choice)
{
    ChatHandler chat(player->GetSession());
    if (choice == "BARE")
    {
        KeepRightHelmet(player);
        player->SetPlayerFlag(PLAYER_FLAGS_HIDE_HELM);
        chat.SendSysMessage("Your left head goes bare-headed.");
    }
    else if (choice == "WEAR")
    {
        player->RemovePlayerFlag(PLAYER_FLAGS_HIDE_HELM);
        chat.SendSysMessage("Your left head wears her helmet again.");
    }
    else if (choice == "ECHO")
    {
        if (!IsSplit(player) || !WardrobeOf(player).Items[HeadSlot])
        {
            chat.SendSysMessage("Your heads already wear the same helmet.");
            return;
        }
        uint32 const right = ShownCyclopsHelmet(player);
        if (!right)
        {
            player->SetPlayerFlag(PLAYER_FLAGS_HIDE_HELM);
            chat.SendSysMessage("Your left head goes bare-headed like the right head.");
        }
        else if (uint32 const appearance = AscensionAppearanceOfItem(right);
            appearance && ApplyAscensionAppearanceChanges(player, { { HeadCategory, appearance } }))
        {
            player->RemovePlayerFlag(PLAYER_FLAGS_HIDE_HELM);
            chat.SendSysMessage("Your left head wears the same helmet as the right head.");
        }
        else
            chat.SendSysMessage("Your left head cannot wear that helmet.");
    }
    UpdateCyclopsHelmet(player);
}

void EchoCyclopsHelmet(Player* player)
{
    ChooseCyclopsHelmet(player, 0, "Your right head wears the same helmet as the left head again.");
}

void DressCyclops(Player* player, std::vector<std::string_view> const& words)
{
    for (std::size_t index = 1; index < words.size(); ++index)
    {
        std::string_view const pair = words[index];
        std::size_t const colon = pair.find(':');
        if (colon == std::string_view::npos)
            continue;
        Optional<uint32> const category = Acore::StringTo<uint32>(pair.substr(0, colon));
        Optional<uint32> const itemId = Acore::StringTo<uint32>(pair.substr(colon + 1));
        if (!category || *category != HeadCategory || !itemId || (*itemId && !DisplayOfItem(*itemId)))
            continue;
        if (*itemId)
            return ChooseCyclopsHelmet(player, *itemId, "Your right head tries on her new helmet.");
        return ChooseCyclopsHelmet(player, Subclass::BareCyclopsHead, "Your right head goes bare-headed.");
    }
}

void HandleTwinLook(Player* player, std::vector<std::string_view> const& words)
{
    if (HasSecondHead(player))
        return DressCyclops(player, words);
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
    return ProjectilePacket(partner.Twin, AutoShotSpell, partner.ShotItem, target);
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

bool IsShot(SpellInfo const* info);

bool HideShotWithoutGun(WorldPacket const& packet)
{
    if (packet.GetOpcode() != SMSG_SPELL_START && packet.GetOpcode() != SMSG_SPELL_GO)
        return true;
    WorldPacket reader(packet);
    reader.rpos(0);
    ObjectGuid source;
    ObjectGuid caster;
    uint8 castCount = 0;
    uint32 spellId = 0;
    reader >> source.ReadAsPacked();
    reader >> caster.ReadAsPacked();
    reader >> castCount >> spellId;
    SpellInfo const* info = caster.IsPlayer() ? sSpellMgr->GetSpellInfo(spellId) : nullptr;
    if (!info || !IsShot(info))
        return true;
    Player* shooter = ObjectAccessor::FindConnectedPlayer(caster);
    if (!shooter || !HasSecondHead(shooter) || GunInHand(shooter) || ShowsAsCopy(shooter, info))
        return true;
    if (packet.GetOpcode() == SMSG_SPELL_GO)
        LOG_INFO("module", "mod-subclass: hides the shooting animation of {} ({}) without a gun in {}'s hand",
            info->SpellName[0], spellId, shooter->GetName());
    return false;
}

std::unordered_map<uint32, uint32> const& OtherHeadSpells()
{
    static std::unordered_map<uint32, uint32> const shown = []
    {
        std::unordered_map<uint32, uint32> loaded;
        bool const exists = bool(WorldDatabase.Query("SELECT 1 FROM information_schema.TABLES WHERE "
            "TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'mod_subclass_other_head_spell'"));
        if (exists)
            if (QueryResult result =
                    WorldDatabase.Query("SELECT `SpellId`, `ShownId` FROM `mod_subclass_other_head_spell`"))
                do
                {
                    Field* fields = result->Fetch();
                    loaded[fields[0].Get<uint32>()] = fields[1].Get<uint32>();
                } while (result->NextRow());
        LOG_INFO("module", "mod-subclass: {} class spells have a copy the other head casts without her arm",
            loaded.size());
        return loaded;
    }();
    return shown;
}

uint32 OtherHeadShownSpell(uint32 spellId)
{
    auto const& shown = OtherHeadSpells();
    auto const itr = shown.find(spellId);
    return itr == shown.end() ? 0 : itr->second;
}

constexpr std::array<uint32, 10> ClassSpellFamilies = { SPELLFAMILY_MAGE, SPELLFAMILY_WARRIOR, SPELLFAMILY_WARLOCK,
    SPELLFAMILY_PRIEST, SPELLFAMILY_DRUID, SPELLFAMILY_ROGUE, SPELLFAMILY_HUNTER, SPELLFAMILY_PALADIN,
    SPELLFAMILY_SHAMAN, SPELLFAMILY_DEATHKNIGHT };

bool LeavesTheArmsAlone(uint32 spellId)
{
    SpellInfo const* info = sSpellMgr->GetSpellInfo(spellId);
    return info && std::find(ClassSpellFamilies.begin(), ClassSpellFamilies.end(), info->SpellFamilyName) !=
        ClassSpellFamilies.end();
}

struct SpellPacket
{
    ObjectGuid Caster;
    uint32 SpellId = 0;
    std::size_t SpellAt = 0;
};

std::optional<SpellPacket> ReadSpellPacket(WorldPacket const& packet)
{
    WorldPacket reader(packet);
    reader.rpos(0);
    SpellPacket read;
    ObjectGuid other;
    uint8 castCount = 0;
    switch (packet.GetOpcode())
    {
        case SMSG_SPELL_START:
        case SMSG_SPELL_GO:
            reader >> other.ReadAsPacked() >> read.Caster.ReadAsPacked() >> castCount;
            break;
        case SMSG_SPELL_FAILURE:
        case SMSG_SPELL_FAILED_OTHER:
            reader >> read.Caster.ReadAsPacked() >> castCount;
            break;
        case SMSG_SPELLLOGEXECUTE:
            reader >> read.Caster.ReadAsPacked();
            break;
        case SMSG_SPELLNONMELEEDAMAGELOG:
        case SMSG_SPELLHEALLOG:
        case SMSG_SPELLENERGIZELOG:
            reader >> other.ReadAsPacked() >> read.Caster.ReadAsPacked();
            break;
        case SMSG_SPELLLOGMISS:
            reader >> read.SpellId >> read.Caster;
            return read;
        default:
            return std::nullopt;
    }
    read.SpellAt = reader.rpos();
    reader >> read.SpellId;
    return read;
}

bool CarriesASpell(uint16 opcode)
{
    switch (opcode)
    {
        case SMSG_SPELL_START:
        case SMSG_SPELL_GO:
        case SMSG_SPELL_FAILURE:
        case SMSG_SPELL_FAILED_OTHER:
        case SMSG_SPELLLOGEXECUTE:
        case SMSG_SPELLNONMELEEDAMAGELOG:
        case SMSG_SPELLHEALLOG:
        case SMSG_SPELLENERGIZELOG:
        case SMSG_SPELLLOGMISS:
            return true;
        default:
            return false;
    }
}

bool ShowOtherHeadStrikeAsCopy(WorldSession* session, WorldPacket const& packet)
{
    if (!CarriesASpell(packet.GetOpcode()))
        return true;
    std::optional<SpellPacket> const read = ReadSpellPacket(packet);
    if (!read || !CastShownAsCopy(read->Caster, read->SpellId))
        return true;
    uint32 const shown = OtherHeadShownSpell(read->SpellId);
    if (!shown)
        return (packet.GetOpcode() != SMSG_SPELL_START && packet.GetOpcode() != SMSG_SPELL_GO) ||
            LeavesTheArmsAlone(read->SpellId);
    WorldPacket copy(packet);
    copy.put<uint32>(read->SpellAt, shown);
    session->SendPacket(&copy);
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
        if (session)
            NoteAttackStopped(session, packet);
        return !session || (HideShotWithoutGun(packet) && ShowOtherHeadStrikeAsCopy(session, packet) &&
            ShowCastOnBrother(session, packet) &&
            ShareSwingWithBrother(session, packet) && ShowHitsOnBrother(session, packet));
    }

    bool CanPacketReceive(WorldSession* session, WorldPacket const& packet) override
    {
        if (session)
        {
            OrderCompanion(session, packet);
            NoteAttackIntent(session, packet);
            NoteSheathRequest(session, packet);
        }
        return !session || AnswerMirrorImage(session, packet);
    }
};

void PlayEasy(Player* player, uint32 diff);

class mod_subclass_twin_player : public PlayerScript
{
public:
    mod_subclass_twin_player() : PlayerScript("mod_subclass_twin_player", { PLAYERHOOK_ON_LOGIN,
        PLAYERHOOK_ON_BEFORE_LOGOUT, PLAYERHOOK_ON_LOGOUT, PLAYERHOOK_ON_MAP_CHANGED, PLAYERHOOK_ON_DELETE,
        PLAYERHOOK_ON_UPDATE,
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
        ForgetStowedLook(player, slot);
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

    void OnPlayerBeforeLogout(Player* player) override
    {
        ShowCharacterSelectJaws(player);
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

std::vector<std::string> ExplainBrain(Player* player);

class TwinCommands
{
public:
    static ChatCommandTable const& Table()
    {
        static ChatCommandTable const table =
        {
            { "", HandleToggle, SEC_GAMEMASTER, Console::No },
            { "wear", HandleWear, SEC_GAMEMASTER, Console::No },
            { "strip", HandleStrip, SEC_GAMEMASTER, Console::No },
            { "copy", HandleCopy, SEC_GAMEMASTER, Console::No },
            { "info", HandleInfo, SEC_GAMEMASTER, Console::No }
        };
        return table;
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

bool SwingsAWeapon(SpellInfo const* info);

void ReleaseOtherHalf(Player* player, uint32 spellId, ObjectGuid targetGuid)
{
    SetBusy(player, false, 0);
    SpellInfo const* info = sSpellMgr->GetSpellInfo(spellId);
    Unit* target = targetGuid == player->GetGUID() ? player : ObjectAccessor::GetUnit(*player, targetGuid);
    if (!info || !target || !player->IsInWorld() || !player->IsAlive() || !OtherHalfActs(player) ||
        !Affordable(player, info) || Helpless(player))
        return;
    Creature* twin = HasTwin(player) ? TwinOf(player) : nullptr;
    ActOutCast(player, info, target);
    if (twin)
        castingAsBrother = { player->GetGUID(), twin->GetGUID(), spellId };
    else if (HasSecondHead(player))
        ShowCastAsCopy(player, spellId, ShowsAsCopy(player, info));
    SpellCastResult const result = player->CastSpell(target, spellId, TriggerCastFlags(TRIGGERED_IGNORE_GCD |
        TRIGGERED_IGNORE_CAST_IN_PROGRESS | TRIGGERED_CAST_DIRECTLY | TRIGGERED_IGNORE_SET_FACING |
        TRIGGERED_DONT_REPORT_CAST_ERROR));
    LOG_INFO("module", "The other half of {} casts {} ({}) at {}: result {}, {} cast packet(s) shown on the twin",
        player->GetName(), info->SpellName[0], spellId, target->GetName(), uint32(result), castingAsBrother.Shown);
    SendSubclassMessage(player, Acore::StringFormat("OTHERCAST {} {}", spellId, uint32(result)));
    NoteBotCast(player, spellId, uint32(result));
    if (result != SPELL_CAST_OK)
    {
        std::lock_guard<std::mutex> lock(twinMutex);
        brains[player->GetGUID().GetCounter()].RefusedUntil[spellId] = GameTime::GetGameTimeMS().count() +
            RefusedSpellRest;
    }
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
    if (!info || !target || !OtherHalfActs(player) || !Affordable(player, info) || Helpless(player) ||
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

void PressForOtherHalf(Player* player, uint32 spellId)
{
    SpellInfo const* info = sSpellMgr->GetSpellInfo(spellId);
    if (!info || !player->IsAlive() || !player->HasSpell(spellId) || !IsBorrowedSpell(player, spellId))
        return;
    Unit* selected = player->GetSelectedUnit();
    Unit* target = player;
    if (info->NeedsExplicitUnitTarget())
    {
        if (info->IsPositive())
            target = selected && !player->IsValidAttackTarget(selected) ? selected : player;
        else
            target = selected && player->IsValidAttackTarget(selected) ? selected : nullptr;
    }
    ChatHandler chat(player->GetSession());
    if (!target)
    {
        chat.SendNotification("That needs an enemy target.");
        return;
    }
    if (HasTwin(player))
    {
        OrderBrother(player, spellId, target);
        return;
    }
    if (!HasSecondHead(player))
        return;
    if (!Affordable(player, info))
        chat.SendNotification("Your other head can't cast that yet.");
    else if (!OrderOtherHalf(player, spellId, target, ManualPause))
        chat.SendNotification("Your other head can't cast that right now.");
}

bool HasAnyAura(SpellInfo const* info, std::initializer_list<AuraType> types)
{
    return std::any_of(types.begin(), types.end(), [info](AuraType type) { return info->HasAura(type); });
}

bool HasAnyEffect(SpellInfo const* info, std::initializer_list<SpellEffects> effects)
{
    return std::any_of(effects.begin(), effects.end(), [info](SpellEffects effect) { return info->HasEffect(effect); });
}

bool HurtsOrHinders(SpellInfo const* info)
{
    return HasAnyEffect(info, { SPELL_EFFECT_SCHOOL_DAMAGE, SPELL_EFFECT_WEAPON_DAMAGE,
        SPELL_EFFECT_WEAPON_DAMAGE_NOSCHOOL, SPELL_EFFECT_NORMALIZED_WEAPON_DMG, SPELL_EFFECT_WEAPON_PERCENT_DAMAGE,
        SPELL_EFFECT_HEALTH_LEECH, SPELL_EFFECT_POWER_BURN, SPELL_EFFECT_POWER_DRAIN, SPELL_EFFECT_TRANS_DOOR }) ||
        HasAnyAura(info, { SPELL_AURA_PERIODIC_DAMAGE, SPELL_AURA_PERIODIC_DAMAGE_PERCENT, SPELL_AURA_PERIODIC_LEECH,
        SPELL_AURA_PERIODIC_TRIGGER_SPELL, SPELL_AURA_MOD_STUN, SPELL_AURA_MOD_ROOT, SPELL_AURA_MOD_DECREASE_SPEED,
        SPELL_AURA_MOD_DAMAGE_PERCENT_TAKEN, SPELL_AURA_MOD_RESISTANCE, SPELL_AURA_MOD_STALKED,
        SPELL_AURA_RANGED_ATTACK_POWER_ATTACKER_BONUS });
}

bool BreaksOnDamageOrSendsAway(SpellInfo const* info)
{
    return (info->AuraInterruptFlags & (AURA_INTERRUPT_FLAG_TAKE_DAMAGE | AURA_INTERRUPT_FLAG_DIRECT_DAMAGE)) ||
        HasAnyAura(info, { SPELL_AURA_MOD_CONFUSE, SPELL_AURA_MOD_FEAR });
}

bool UsesReagents(SpellInfo const* info)
{
    return std::any_of(info->Reagent.begin(), info->Reagent.end(), [](int32 reagent) { return reagent > 0; });
}

uint32 LongestRecovery(SpellInfo const* info)
{
    return std::max(info->RecoveryTime, info->CategoryRecoveryTime);
}

constexpr uint32 BigCooldown = 60000;
constexpr int32 ShortSummon = 60000;

bool SummonsAShortHelper(SpellInfo const* info)
{
    int32 const duration = info->GetMaxDuration();
    return duration > 0 && duration <= ShortSummon && LongestRecovery(info) >= BigCooldown;
}

bool RainsOnAnArea(SpellInfo const* info)
{
    return info->HasEffect(SPELL_EFFECT_PERSISTENT_AREA_AURA);
}

bool NeverAlone(SpellInfo const* info)
{
    if ((info->IsChanneled() && !RainsOnAnArea(info)) || UsesReagents(info) || info->PowerType == POWER_HEALTH)
        return true;
    switch (info->GetSpellSpecific())
    {
        case SPELL_SPECIFIC_ASPECT:
        case SPELL_SPECIFIC_AURA:
        case SPELL_SPECIFIC_PRESENCE:
        case SPELL_SPECIFIC_TRACKER:
        case SPELL_SPECIFIC_FOOD:
        case SPELL_SPECIFIC_DRINK:
        case SPELL_SPECIFIC_FOOD_AND_DRINK:
            return true;
        default:
            break;
    }
    if (HasAnyAura(info, { SPELL_AURA_BIND_SIGHT, SPELL_AURA_FAR_SIGHT, SPELL_AURA_MOD_POSSESS,
            SPELL_AURA_MOD_POSSESS_PET, SPELL_AURA_TRACK_CREATURES, SPELL_AURA_TRACK_RESOURCES,
            SPELL_AURA_TRACK_STEALTHED, SPELL_AURA_MOD_STEALTH_DETECT, SPELL_AURA_MOD_SHAPESHIFT,
            SPELL_AURA_MOD_INVISIBILITY_DETECT, SPELL_AURA_MOD_STEALTH, SPELL_AURA_MOD_INVISIBILITY,
            SPELL_AURA_FEIGN_DEATH, SPELL_AURA_MOD_INCREASE_SPEED, SPELL_AURA_MOD_SPEED_ALWAYS,
            SPELL_AURA_MOD_INCREASE_MOUNTED_SPEED, SPELL_AURA_MOUNTED, SPELL_AURA_FLY, SPELL_AURA_WATER_BREATHING,
            SPELL_AURA_WATER_WALK, SPELL_AURA_FEATHER_FALL, SPELL_AURA_HOVER, SPELL_AURA_MOD_THREAT,
            SPELL_AURA_MOD_TOTAL_THREAT, SPELL_AURA_MANA_SHIELD, SPELL_AURA_TRANSFORM, SPELL_AURA_CONTROL_VEHICLE,
            SPELL_AURA_COMPREHEND_LANGUAGE }))
        return true;
    if (info->IsPositive() && HasAnyAura(info, { SPELL_AURA_MOD_STUN, SPELL_AURA_MOD_PACIFY,
            SPELL_AURA_MOD_PACIFY_SILENCE }))
        return true;
    if (info->HasEffect(SPELL_EFFECT_SUMMON) && !SummonsAShortHelper(info))
        return true;
    return HasAnyEffect(info, { SPELL_EFFECT_LEAP, SPELL_EFFECT_LEAP_BACK, SPELL_EFFECT_CHARGE,
        SPELL_EFFECT_CHARGE_DEST, SPELL_EFFECT_JUMP, SPELL_EFFECT_JUMP_DEST, SPELL_EFFECT_TELEPORT_UNITS,
        SPELL_EFFECT_TELEPORT_UNITS_FACE_CASTER, SPELL_EFFECT_TAMECREATURE, SPELL_EFFECT_RESURRECT_PET,
        SPELL_EFFECT_SUMMON_PET, SPELL_EFFECT_DISMISS_PET, SPELL_EFFECT_FEED_PET, SPELL_EFFECT_CREATE_ITEM,
        SPELL_EFFECT_CREATE_ITEM_2, SPELL_EFFECT_CREATE_RANDOM_ITEM, SPELL_EFFECT_ENCHANT_ITEM,
        SPELL_EFFECT_ENCHANT_ITEM_TEMPORARY, SPELL_EFFECT_ENCHANT_ITEM_PRISMATIC, SPELL_EFFECT_ENCHANT_HELD_ITEM,
        SPELL_EFFECT_RESURRECT, SPELL_EFFECT_RESURRECT_NEW, SPELL_EFFECT_SELF_RESURRECT,
        SPELL_EFFECT_SUMMON_OBJECT_WILD, SPELL_EFFECT_SUMMON_PLAYER, SPELL_EFFECT_OPEN_LOCK, SPELL_EFFECT_SKINNING,
        SPELL_EFFECT_PICKPOCKET, SPELL_EFFECT_DISTRACT, SPELL_EFFECT_ADD_FARSIGHT, SPELL_EFFECT_REDIRECT_THREAT,
        SPELL_EFFECT_SANCTUARY, SPELL_EFFECT_STUCK, SPELL_EFFECT_INSTAKILL, SPELL_EFFECT_TRADE_SKILL,
        SPELL_EFFECT_PROSPECTING, SPELL_EFFECT_MILLING, SPELL_EFFECT_DISENCHANT });
}

enum class Use { Never, Interrupt, Taunt, Heal, Shield, Cleanse, Purge, Defend, Attack, Energize, Cooldown, Buff };

bool Heals(SpellInfo const* info)
{
    return HasAnyEffect(info, { SPELL_EFFECT_HEAL, SPELL_EFFECT_HEAL_PCT, SPELL_EFFECT_HEAL_MAX_HEALTH }) ||
        info->HasAura(SPELL_AURA_PERIODIC_HEAL);
}

bool GivesManaBack(SpellInfo const* info, uint8 depth = 0);

Use UseOf(SpellInfo const* info)
{
    if (NeverAlone(info))
        return Use::Never;
    if (info->HasEffect(SPELL_EFFECT_INTERRUPT_CAST) || info->HasAura(SPELL_AURA_MOD_SILENCE))
        return Use::Interrupt;
    if (info->HasEffect(SPELL_EFFECT_ATTACK_ME) || info->HasAura(SPELL_AURA_MOD_TAUNT))
        return Use::Taunt;
    if (info->HasEffect(SPELL_EFFECT_DISPEL))
        return info->IsPositive() ? Use::Cleanse : Use::Purge;
    if (!info->IsPositive())
        return (HurtsOrHinders(info) || GivesManaBack(info)) && !BreaksOnDamageOrSendsAway(info) ? Use::Attack :
            Use::Never;
    if (Heals(info))
        return Use::Heal;
    if (info->HasAura(SPELL_AURA_SCHOOL_ABSORB))
        return Use::Shield;
    if (HasAnyAura(info, { SPELL_AURA_SCHOOL_IMMUNITY, SPELL_AURA_DAMAGE_IMMUNITY, SPELL_AURA_MOD_PARRY_PERCENT,
            SPELL_AURA_MOD_DODGE_PERCENT, SPELL_AURA_MOD_DAMAGE_PERCENT_TAKEN, SPELL_AURA_SPLIT_DAMAGE_PCT }))
        return Use::Defend;
    if (info->HasEffect(SPELL_EFFECT_ENERGIZE))
        return Use::Energize;
    if (info->HasEffect(SPELL_EFFECT_SUMMON))
        return Use::Cooldown;
    if (info->HasEffect(SPELL_EFFECT_APPLY_AURA) && info->GetMaxDuration() >= LastingBuff)
        return LongestRecovery(info) >= BigCooldown ? Use::Cooldown : Use::Buff;
    return Use::Never;
}

enum class Helps { Self, Pet, Chosen, Around };

Helps WhomItHelps(SpellInfo const* info)
{
    for (SpellEffectInfo const& effect : info->GetEffects())
    {
        if (!effect.IsEffect())
            continue;
        switch (effect.TargetA.GetTarget())
        {
            case TARGET_UNIT_PET:
                return Helps::Pet;
            case TARGET_UNIT_CASTER_AREA_PARTY:
            case TARGET_UNIT_CASTER_AREA_RAID:
            case TARGET_UNIT_SRC_AREA_ALLY:
            case TARGET_UNIT_DEST_AREA_ALLY:
                return Helps::Around;
            default:
                break;
        }
    }
    constexpr uint32 FriendlyUnit = TARGET_FLAG_UNIT | TARGET_FLAG_UNIT_ALLY | TARGET_FLAG_UNIT_PARTY |
        TARGET_FLAG_UNIT_RAID;
    return (info->GetExplicitTargetMask() & FriendlyUnit) ? Helps::Chosen : Helps::Self;
}

std::vector<Unit*> Friends(Player* player)
{
    std::vector<Unit*> friends = { player };
    if (Pet* pet = player->GetPet())
        friends.push_back(pet);
    if (Group* group = player->GetGroup())
        for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
        {
            Player* member = ref->GetSource();
            if (member && member != player && member->IsInMap(player))
            {
                friends.push_back(member);
                if (Pet* pet = member->GetPet())
                    friends.push_back(pet);
            }
        }
    std::erase_if(friends, [](Unit* unit) { return !unit->IsInWorld() || !unit->IsAlive(); });
    return friends;
}

bool InAFight(Player* player)
{
    std::vector<Unit*> const friends = Friends(player);
    return std::any_of(friends.begin(), friends.end(), [](Unit* unit) { return unit->IsInCombat(); });
}

bool Reaches(Player* player, SpellInfo const* info, Unit* target)
{
    if (target == player)
        return true;
    float const range = info->GetMaxRange(info->IsPositive(), player);
    return (range <= 0.0f || player->IsWithinDistInMap(target, range)) && player->IsWithinLOSInMap(target);
}

std::vector<Unit*> Helped(Player* player, SpellInfo const* info, std::vector<Unit*> const& friends)
{
    switch (WhomItHelps(info))
    {
        case Helps::Self:
            return { player };
        case Helps::Pet:
            if (Pet* pet = player->GetPet(); pet && pet->IsAlive())
                return { pet };
            return {};
        default:
            break;
    }
    std::vector<Unit*> helped;
    for (Unit* unit : friends)
        if (Reaches(player, info, unit))
            helped.push_back(unit);
    return helped;
}

uint32 ExpectedHealing(Player* player, SpellInfo const* info, Unit* target)
{
    uint64 total = 0;
    for (SpellEffectInfo const& effect : info->GetEffects())
    {
        uint32 const base = uint32(std::max(effect.CalcValue(player), 0));
        if (effect.Effect == SPELL_EFFECT_HEAL)
            total += player->SpellHealingBonusDone(target, info, base, HEAL, effect.EffectIndex);
        else if (effect.Effect == SPELL_EFFECT_HEAL_PCT)
            total += target->CountPctFromMaxHealth(int32(base));
        else if (effect.Effect == SPELL_EFFECT_HEAL_MAX_HEALTH)
            total += target->GetMaxHealth();
        else if (effect.IsAura(SPELL_AURA_PERIODIC_HEAL) && effect.Amplitude)
            total += uint64(player->SpellHealingBonusDone(target, info, base, DOT, effect.EffectIndex)) *
                uint32(std::max(info->GetMaxDuration(), 0)) / effect.Amplitude;
    }
    return uint32(std::min<uint64>(total, UINT32_MAX));
}

bool BeingHealedByAnother(Player* player, Unit* target)
{
    Group* group = player->GetGroup();
    if (!group)
        return false;
    for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
    {
        Player* member = ref->GetSource();
        if (!member || member == player || !member->IsInMap(player))
            continue;
        Spell const* casting = member->GetCurrentSpell(CURRENT_GENERIC_SPELL);
        if (casting && casting->m_targets.GetUnitTargetGUID() == target->GetGUID() && Heals(casting->GetSpellInfo()))
            return true;
    }
    return false;
}

float HealthPct(Unit const* unit)
{
    return unit->GetHealthPct();
}

uint32 Missing(Unit const* unit)
{
    return unit->GetMaxHealth() - unit->GetHealth();
}

bool HasDispellable(Unit* target, uint32 dispelMask, bool harmful)
{
    for (auto const& [auraId, application] : target->GetAppliedAuras())
    {
        Aura const* aura = application->GetBase();
        if (!aura->IsPassive() && application->IsPositive() != harmful &&
            (aura->GetSpellInfo()->GetDispelMask() & dispelMask))
            return true;
    }
    return false;
}

uint32 DispelMaskOf(SpellInfo const* info)
{
    uint32 mask = 0;
    for (SpellEffectInfo const& effect : info->GetEffects())
        if (effect.Effect == SPELL_EFFECT_DISPEL)
            mask |= SpellInfo::GetDispelMask(DispelType(effect.MiscValue));
    return mask;
}

bool AlreadyGiven(Unit* target, SpellInfo const* info, ObjectGuid caster)
{
    if (target->GetAura(info->Id, caster))
        return true;
    for (auto const& [auraId, application] : target->GetAppliedAuras())
    {
        SpellInfo const* held = application->GetBase()->GetSpellInfo();
        if (held->Id == info->Id || held->GetFirstRankSpell() == info->GetFirstRankSpell())
            return true;
        bool const sameCaster = application->GetBase()->GetCasterGUID() == caster;
        if (info->IsAuraExclusiveBySpecificWith(held) ||
            (sameCaster && info->IsAuraExclusiveBySpecificPerCasterWith(held)))
            return true;
        switch (sSpellMgr->CheckSpellGroupStackRules(info, held))
        {
            case SPELL_GROUP_STACK_RULE_EXCLUSIVE:
            case SPELL_GROUP_STACK_RULE_EXCLUSIVE_SAME_EFFECT:
            case SPELL_GROUP_STACK_RULE_EXCLUSIVE_HIGHEST:
                return true;
            case SPELL_GROUP_STACK_RULE_EXCLUSIVE_FROM_SAME_CASTER:
                if (sameCaster)
                    return true;
                break;
            default:
                break;
        }
    }
    return false;
}

bool Tanking(Player const* player)
{
    switch (player->GetShapeshiftForm())
    {
        case FORM_DEFENSIVESTANCE:
        case FORM_BEAR:
        case FORM_DIREBEAR:
            return true;
        default:
            break;
    }
    return player->GetTotalAuraModifier(SPELL_AURA_MOD_THREAT) > 0;
}

bool OursOrUs(Player const* player, Unit const* unit)
{
    return unit == player || unit->GetCharmerOrOwnerGUID() == player->GetGUID();
}

bool FoeOnAFriend(Player* player, Unit* foe)
{
    Unit* victim = foe->GetVictim();
    if (!victim || OursOrUs(player, victim))
        return false;
    std::vector<Unit*> const friends = Friends(player);
    return std::find(friends.begin(), friends.end(), victim) != friends.end();
}

bool Casting(Unit const* foe)
{
    return foe->IsNonMeleeSpellCast(false, false, true);
}

bool WorthTheBigCooldown(Unit const* foe)
{
    if (!foe)
        return false;
    if (foe->IsPlayer())
        return true;
    Creature const* creature = foe->ToCreature();
    return creature && (creature->isElite() || creature->isWorldBoss());
}

constexpr float ManaKeptForTheMain = 0.35f;
constexpr float LowPowerPct = 30.0f;

bool GivesManaBack(SpellInfo const* info, uint8 depth)
{
    for (SpellEffectInfo const& effect : info->GetEffects())
    {
        if (!effect.IsEffect())
            continue;
        bool const manaEffect = effect.MiscValue == POWER_MANA &&
            (effect.Effect == SPELL_EFFECT_POWER_DRAIN || (effect.Effect == SPELL_EFFECT_ENERGIZE &&
                effect.TargetA.GetTarget() == TARGET_UNIT_CASTER));
        bool const manaAura = effect.IsAura(SPELL_AURA_PERIODIC_MANA_LEECH) ||
            (effect.IsAura(SPELL_AURA_PERIODIC_ENERGIZE) && effect.MiscValue == POWER_MANA &&
                effect.TargetA.GetTarget() == TARGET_UNIT_CASTER);
        if (manaEffect || manaAura)
            return true;
        if (depth == 0 && effect.TriggerSpell)
            if (SpellInfo const* triggered = sSpellMgr->GetSpellInfo(effect.TriggerSpell);
                triggered && GivesManaBack(triggered, depth + 1))
                return true;
    }
    return false;
}

constexpr float LowManaPct = 90.0f;

bool LowOnMana(Player const* player)
{
    return player->GetMaxPower(POWER_MANA) && player->GetPowerPct(POWER_MANA) < LowManaPct;
}

bool LeavesManaForTheMain(Player const* player, SpellInfo const* info, bool emergency)
{
    if (info->PowerType != POWER_MANA || emergency || GivesManaBack(info))
        return true;
    int32 const cost = info->CalcPowerCost(player, info->GetSchoolMask());
    return int64(player->GetPower(POWER_MANA)) - cost >=
        int64(player->GetMaxPower(POWER_MANA) * ManaKeptForTheMain);
}

bool RecentlyRefused(Player const* player, uint32 spellId)
{
    std::lock_guard<std::mutex> lock(twinMutex);
    auto const& refused = brains[player->GetGUID().GetCounter()].RefusedUntil;
    auto const itr = refused.find(spellId);
    return itr != refused.end() && itr->second > GameTime::GetGameTimeMS().count();
}

struct Pick
{
    uint32 SpellId = 0;
    Unit* Target = nullptr;
    int32 Tier = 0;
    float HealthPct = 100.0f;
    uint32 Weight = 0;
};

enum Tier : int32
{
    TierBuff = 1,
    TierCooldown,
    TierAttack,
    TierEnergize,
    TierHelp,
    TierTaunt,
    TierEmergency,
    TierInterrupt
};

void Consider(Pick& best, Pick const& pick)
{
    if (pick.Tier > best.Tier)
    {
        best = pick;
        return;
    }
    if (pick.Tier < best.Tier)
        return;
    bool const better = pick.Tier == TierHelp || pick.Tier == TierEmergency ?
        Subclass::BetterHeal(pick.HealthPct, pick.Weight, best.HealthPct, best.Weight) : pick.Weight > best.Weight;
    if (better)
        best = pick;
}

bool ReachesFoe(Player* player, SpellInfo const* info, Unit* foe)
{
    if (!IsShot(info))
        return Reaches(player, info, foe);
    if (OtherHeadShoots(player) && player->IsWithinMeleeRange(foe) && !GivesManaBack(info))
        return false;
    return BrotherReaches(player, info, foe);
}

std::string_view ConsiderHeal(Player* player, SpellInfo const* info, std::vector<Unit*> const& friends, Pick& best)
{
    std::vector<Unit*> const helped = Helped(player, info, friends);
    if (helped.empty())
        return "nobody to heal in reach";
    bool const around = WhomItHelps(info) == Helps::Around;
    std::size_t worth = 0;
    Pick pick;
    for (Unit* unit : helped)
    {
        if (BeingHealedByAnother(player, unit) || (info->HasAura(SPELL_AURA_PERIODIC_HEAL) &&
                unit->GetAura(info->Id, player->GetGUID())))
            continue;
        uint32 const expected = ExpectedHealing(player, info, unit);
        if (!Subclass::HealWorthCasting(expected, Missing(unit), HealthPct(unit)))
            continue;
        ++worth;
        Pick const candidate = { info->Id, around ? player : unit,
            HealthPct(unit) < Subclass::CriticalHealthPct ? TierEmergency : TierHelp, HealthPct(unit), expected };
        if (!pick.SpellId || Subclass::BetterHeal(candidate.HealthPct, candidate.Weight, pick.HealthPct,
                pick.Weight))
            pick = candidate;
    }
    if (!pick.SpellId || (around && worth < std::min<std::size_t>(2, helped.size())))
        return "nobody hurt enough";
    Consider(best, pick);
    return {};
}

std::string_view ConsiderShield(Player* player, SpellInfo const* info, std::vector<Unit*> const& friends, Pick& best)
{
    bool considered = false;
    for (Unit* unit : Helped(player, info, friends))
        if (!unit->getAttackers().empty() && !AlreadyGiven(unit, info, player->GetGUID()) &&
            HealthPct(unit) < Subclass::AlmostFullHealthPct)
        {
            Consider(best, { info->Id, unit, TierHelp, HealthPct(unit), 0 });
            considered = true;
        }
    return considered ? std::string_view() : "nobody hurt and attacked";
}

std::string_view ConsiderCleanse(Player* player, SpellInfo const* info, std::vector<Unit*> const& friends,
    Pick& best)
{
    uint32 const mask = DispelMaskOf(info);
    bool considered = false;
    for (Unit* unit : Helped(player, info, friends))
        if (HasDispellable(unit, mask, true))
        {
            Consider(best, { info->Id, unit, TierHelp, HealthPct(unit), 0 });
            considered = true;
        }
    return considered ? std::string_view() : "nothing to dispel";
}

constexpr std::size_t AreaFoes = 3;
constexpr uint32 MarkWeight = std::numeric_limits<uint32>::max();

std::size_t FoesAround(Player* player, Unit* foe, SpellInfo const* info)
{
    float const radius = std::max(info->Effects[EFFECT_0].CalcRadius(player), 5.0f);
    std::list<Unit*> foes;
    Acore::AnyUnfriendlyUnitInObjectRangeCheck check(foe, player, radius);
    Acore::UnitListSearcher<Acore::AnyUnfriendlyUnitInObjectRangeCheck> searcher(foe, foes, check);
    Cell::VisitObjects(foe, searcher, radius);
    return foes.size();
}

bool MarksTheFoe(SpellInfo const* info)
{
    return HasAnyAura(info, { SPELL_AURA_MOD_STALKED, SPELL_AURA_RANGED_ATTACK_POWER_ATTACKER_BONUS });
}

std::string_view ConsiderSpell(Player* player, SpellInfo const* info, Unit* foe, std::vector<Unit*> const& friends,
    Pick& best)
{
    uint32 const recovery = LongestRecovery(info);
    switch (UseOf(info))
    {
        case Use::Interrupt:
            if (!foe || !Casting(foe))
                return "interrupt: target not casting";
            if (!ReachesFoe(player, info, foe))
                return "interrupt: out of reach";
            Consider(best, { info->Id, foe, TierInterrupt, 100.0f, recovery });
            return {};
        case Use::Taunt:
            if (!foe || !Subclass::TauntNeeded(Tanking(player), foe->GetVictim() == player,
                    FoeOnAFriend(player, foe)))
                return "taunt: not needed";
            if (!ReachesFoe(player, info, foe))
                return "taunt: out of reach";
            Consider(best, { info->Id, foe, TierTaunt, 100.0f, recovery });
            return {};
        case Use::Heal:
            return ConsiderHeal(player, info, friends, best);
        case Use::Shield:
            return ConsiderShield(player, info, friends, best);
        case Use::Cleanse:
            return ConsiderCleanse(player, info, friends, best);
        case Use::Purge:
            if (!foe || !HasDispellable(foe, DispelMaskOf(info), false))
                return "purge: nothing to remove";
            if (!ReachesFoe(player, info, foe))
                return "purge: out of reach";
            Consider(best, { info->Id, foe, TierAttack, 100.0f, recovery });
            return {};
        case Use::Defend:
            if (HealthPct(player) >= Subclass::LowHealthPct || player->getAttackers().empty())
                return "defend: not in danger";
            if (AlreadyGiven(player, info, player->GetGUID()))
                return "defend: already up";
            Consider(best, { info->Id, player, TierEmergency, HealthPct(player), recovery });
            return {};
        case Use::Attack:
            if (!foe)
                return "attack: no target";
            if (!ReachesFoe(player, info, foe))
                return "attack: out of reach";
            if (info->HasEffect(SPELL_EFFECT_APPLY_AURA) && AlreadyGiven(foe, info, player->GetGUID()))
                return "attack: already on the target";
            if (RainsOnAnArea(info) && FoesAround(player, foe, info) < AreaFoes)
                return "area: fewer foes than worth it";
            if (info->IsChanneled() && player->HasUnitState(UNIT_STATE_CASTING))
                return "channel: the main head is casting";
            Consider(best, { info->Id, foe, GivesManaBack(info) && LowOnMana(player) ? TierEnergize : TierAttack,
                100.0f, MarksTheFoe(info) ? MarkWeight : recovery });
            return {};
        case Use::Energize:
            if (info->PowerType == POWER_MANA || player->GetPowerPct(player->getPowerType()) >= LowPowerPct)
                return "energize: power not low";
            Consider(best, { info->Id, player, TierEnergize, 100.0f, recovery });
            return {};
        case Use::Cooldown:
            if (!WorthTheBigCooldown(foe))
                return "big cooldown: saved for elites";
            if (AlreadyGiven(player, info, player->GetGUID()))
                return "big cooldown: already up";
            Consider(best, { info->Id, player, TierCooldown, 100.0f, recovery });
            return {};
        case Use::Buff:
        {
            Unit* const buffed = WhomItHelps(info) == Helps::Pet ? static_cast<Unit*>(player->GetPet()) : player;
            if (!buffed || AlreadyGiven(buffed, info, player->GetGUID()))
                return "buff: already up";
            Consider(best, { info->Id, player, TierBuff, 100.0f, recovery });
            return {};
        }
        case Use::Never:
            break;
    }
    return "never on her own";
}

std::string_view Verdict(Player* player, SpellInfo const* info, Unit* foe, std::vector<Unit*> const& friends,
    bool emergency, Pick& best)
{
    if (player->HasSpellCooldown(info->Id))
        return "on cooldown";
    if (!Affordable(player, info))
        return "can't afford";
    if (RecentlyRefused(player, info->Id))
        return "refused a moment ago";
    if (!LeavesManaForTheMain(player, info, emergency))
        return "keeps mana for you";
    return ConsiderSpell(player, info, foe, friends, best);
}

bool SomeoneCritical(std::vector<Unit*> const& friends)
{
    return std::any_of(friends.begin(), friends.end(),
        [](Unit* unit) { return HealthPct(unit) < Subclass::CriticalHealthPct; });
}

std::pair<uint32, Unit*> PickSpell(Player* player)
{
    Unit* foe = TwinTarget(player);
    std::vector<Unit*> const friends = Friends(player);
    bool const emergency = SomeoneCritical(friends);
    Pick best;
    for (uint32 spellId : KnownBorrowedActives(player))
        if (SpellInfo const* info = sSpellMgr->GetSpellInfo(spellId))
            Verdict(player, info, foe, friends, emergency, best);
    return { best.SpellId, best.Target };
}

std::vector<std::string> ExplainBrain(Player* player)
{
    uint32 const easyAura = HasTwin(player) ? CowboyAura : HalfBrainAura;
    Unit* foe = TwinTarget(player);
    std::vector<Unit*> const friends = Friends(player);
    bool const emergency = SomeoneCritical(friends);
    std::vector<std::string> lines;
    lines.push_back(Acore::StringFormat("Let loose: {}. Awake: {}. In a fight: {}. Target: {}. Mana: {:.0f}%.",
        player->HasAura(easyAura) ? "yes" : "no", OtherHalfAwake(player) ? "yes" : "no",
        InAFight(player) ? "yes" : "no", foe ? foe->GetName() : "none", player->GetPowerPct(POWER_MANA)));
    Pick best;
    for (uint32 spellId : KnownBorrowedActives(player))
        if (SpellInfo const* info = sSpellMgr->GetSpellInfo(spellId))
        {
            std::string_view const verdict = Verdict(player, info, foe, friends, emergency, best);
            lines.push_back(Acore::StringFormat("{} ({}): {}", info->SpellName[0], spellId,
                verdict.empty() ? std::string_view("could cast") : verdict));
        }
    SpellInfo const* chosen = best.SpellId ? sSpellMgr->GetSpellInfo(best.SpellId) : nullptr;
    lines.push_back(Acore::StringFormat("She would cast: {}.", chosen ? chosen->SpellName[0] : "nothing"));
    return lines;
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
    if (player->GetCurrentSpell(CURRENT_CHANNELED_SPELL))
        return;
    uint32 const easyAura = HasTwin(player) ? CowboyAura : HalfBrainAura;
    bool const bot = BotPlaysOtherHalf(player);
    if ((!bot && !player->HasAura(easyAura)) || !InAFight(player) || !player->IsAlive() ||
        player->HasStealthAura() || !OtherHalfActs(player) || (!bot && HasTwin(player) && !BrotherOut(player)))
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

class mod_subclass_hand_me_downs : public PlayerScript
{
public:
    mod_subclass_hand_me_downs() : PlayerScript("mod_subclass_hand_me_downs", { PLAYERHOOK_ON_BEFORE_LOOT_MONEY }) { }

    void OnPlayerBeforeLootMoney(Player* player, Loot* loot) override
    {
        if (!loot || !loot->gold || !player->HasSpell(HandMeDownsSpell))
            return;
        ObjectGuid const source = player->GetLootGUID();
        bool const treasure = source.IsGameObject() || source.IsItem();
        uint32 const percent = treasure ? HandMeDownsTreasurePercent : HandMeDownsPercent;
        loot->gold += std::max<uint32>(1, loot->gold * percent / 100);
    }
};

bool SwingsAWeapon(SpellInfo const* info)
{
    return info->DmgClass == SPELL_DAMAGE_CLASS_MELEE && (info->HasEffect(SPELL_EFFECT_WEAPON_DAMAGE) ||
        info->HasEffect(SPELL_EFFECT_WEAPON_DAMAGE_NOSCHOOL) || info->HasEffect(SPELL_EFFECT_NORMALIZED_WEAPON_DMG) ||
        info->HasEffect(SPELL_EFFECT_WEAPON_PERCENT_DAMAGE));
}

void HideOtherHeadsArm(Player* player, SpellInfo const* info)
{
    ShowCastAsCopy(player, info->Id, IsBorrowedSpell(player, info->Id) && ShowsAsCopy(player, info));
}

class mod_subclass_twin_shots : public PlayerScript
{
public:
    mod_subclass_twin_shots() : PlayerScript("mod_subclass_twin_shots", { PLAYERHOOK_ON_SPELL_CAST }) { }

    void OnPlayerSpellCast(Player* player, Spell* spell, bool /*skipCheck*/) override
    {
        if (!spell || !HasSecondHead(player))
            return;
        SpellInfo const* info = spell->GetSpellInfo();
        HideOtherHeadsArm(player, info);
        if (!OtherHalfAwake(player))
            return;
        if (!IsShot(info))
        {
            if (SwingsAWeapon(info) && IsBorrowedSpell(player, info->Id) && !OtherHeadShownSpell(info->Id))
                player->HandleEmoteCommand(EMOTE_ONESHOT_SPECIALATTACK1H);
            return;
        }
        if (!OtherHeadShoots(player))
            return;
        NoteOtherHeadShot(player);
        Unit* target = spell->m_targets.GetUnitTarget();
        Item* gun = OwnBow(player);
        if (!gun)
            return;
        NoteHands(player, "shoots");
        if (!IsBow(gun))
        {
            if (CastsOwnSpell(player))
                return;
            if (player->GetSheath() != SHEATH_STATE_MELEE)
            {
                NoteHands(player, "draws mace and gun to shoot");
                player->SetSheath(SHEATH_STATE_MELEE);
                HoldTheGun(player, AimsFromAfar(player));
            }
            player->HandleEmoteCommand(EMOTE_ONESHOT_ATTACK_OFF_PIERCE);
        }
        else if (!target || !player->IsWithinMeleeRange(target))
            player->HandleEmoteCommand(RangedEmote(gun->GetTemplate()));
    }
};

void AddSC_mod_subclass_twin()
{
    new mod_subclass_hand_me_downs();
    new mod_subclass_twin_shots();
    RegisterSpellScript(spell_subclass_second_self);
    RegisterSpellScript(spell_subclass_easy);
    new mod_subclass_twin_packets();
    new mod_subclass_twin_creatures();
    new mod_subclass_twin_player();
}

ChatCommandTable const& SubclassTwinCommandTable()
{
    return TwinCommands::Table();
}

bool HandleSubclassGun(ChatHandler* handler)
{
    Player* player = handler->GetSession()->GetPlayer();
    auto shown = [player](uint8 slot) { return player->GetUInt32Value(PLAYER_VISIBLE_ITEM_1_ENTRYID + slot * 2); };
    Item* gun = OwnBow(player);
    Unit* target = AimedAt(player);
    handler->PSendSysMessage("Sheath {}; shown main hand {}, off hand {}, ranged {}; ranged item {}.",
        uint32(player->GetSheath()), shown(EQUIPMENT_SLOT_MAINHAND), shown(EQUIPMENT_SLOT_OFFHAND),
        shown(EQUIPMENT_SLOT_RANGED), gun ? gun->GetEntry() : 0);
    handler->PSendSysMessage("Other head shoots {}; target {}; in melee reach {}; moving {}; casting {}.",
        OtherHeadShoots(player), target ? target->GetName() : "none",
        target ? player->IsWithinMeleeRange(target) : false, player->isMoving(),
        player->IsNonMeleeSpellCast(false, true, true));
    AttackIntent const intent = IntentOf(player);
    uint32 const now = GameTime::GetGameTimeMS().count();
    handler->PSendSysMessage("Attack asked {}; server stopped it {}; attacking state {}.",
        intent.AskedAt ? Acore::StringFormat("{} ms ago", now - intent.AskedAt) : std::string("never"),
        intent.StoppedAt ? Acore::StringFormat("{} ms ago", now - intent.StoppedAt) : std::string("never"),
        player->HasUnitState(UNIT_STATE_MELEE_ATTACKING));
    return true;
}

bool HandleSubclassBrain(ChatHandler* handler)
{
    for (std::string const& line : ExplainBrain(handler->GetSession()->GetPlayer()))
        handler->SendSysMessage(line);
    return true;
}
