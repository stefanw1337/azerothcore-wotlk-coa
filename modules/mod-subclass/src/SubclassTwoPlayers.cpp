/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license.
 */

#include "AllSpellScript.h"
#include "Chat.h"
#include "Config.h"
#include "GameTime.h"
#include "Group.h"
#include "GroupMgr.h"
#include "Item.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Opcodes.h"
#include "Player.h"
#include "PlayerScript.h"
#include "Spell.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "SubclassRules.h"
#include "ThreatManager.h"
#include "UnitScript.h"
#include "UpdateData.h"
#include "Vehicle.h"
#include "WorldPacket.h"
#include <algorithm>
#include <array>
#include <initializer_list>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

uint32 TwoPlayersPartnerOf(uint32 guid);
bool TwoPlayersIsLeftHead(uint32 guid);
bool TwoPlayersBotsAvailable();
bool TwoPlayersCallBot(Player* master, uint32 headGuid);
bool TwoPlayersHeadIsBot(Player const* head);
void TwoPlayersSteerBotBody(Player* body, bool roam);

namespace
{
constexpr uint32 KeepTogetherMs = 1000;
constexpr float SeatReach = 40.0f;
constexpr int8 RightHeadSeat = 0;
constexpr uint32 BotRecallMs = 30000;
constexpr uint32 BodyThreatKey = 0x7FF09600;

struct Absence
{
    uint32 Since = 0;
    uint32 Called = 0;
};

std::mutex& bodyMutex = *new std::mutex;
std::unordered_map<uint32, std::vector<uint32>>& lent = *new std::unordered_map<uint32, std::vector<uint32>>;
std::unordered_map<uint32, uint32>& sinceCheck = *new std::unordered_map<uint32, uint32>;
std::unordered_set<uint32>& waiting = *new std::unordered_set<uint32>;
std::unordered_set<uint32>& halted = *new std::unordered_set<uint32>;
std::unordered_set<uint32>& greeted = *new std::unordered_set<uint32>;
std::unordered_map<uint32, Absence>& absences = *new std::unordered_map<uint32, Absence>;

struct WornCopy
{
    Item* Copy = nullptr;
    ObjectGuid Source;
    uint32 Look = 0;
};

using WornCopies = std::array<WornCopy, EQUIPMENT_SLOT_END>;
std::mutex& copyMutex = *new std::mutex;
std::unordered_map<uint32, WornCopies>& wornCopies = *new std::unordered_map<uint32, WornCopies>;
std::unordered_set<uint32>& redirected = *new std::unordered_set<uint32>;

Player* Online(uint32 guid)
{
    Player* player = guid ? ObjectAccessor::FindPlayerByLowGUID(guid) : nullptr;
    return player && player->IsInWorld() && !player->IsBeingTeleported() ? player : nullptr;
}

void Tell(Player* player, std::string const& text)
{
    ChatHandler(player->GetSession()).SendSysMessage(text);
}

Subclass::Borrow BorrowOfSpell(uint32 spellId)
{
    SpellInfo const* info = sSpellMgr->GetSpellInfo(spellId);
    if (!info)
        return Subclass::Borrow::None;
    Subclass::SpellShape shape;
    for (uint8 i = 0; i < 3; ++i)
    {
        shape.Effects[i] = info->Effects[i].Effect;
        shape.Auras[i] = info->Effects[i].ApplyAuraName;
        shape.TargetsA[i] = info->Effects[i].TargetA.GetTarget();
    }
    for (uint8 i = 0; i < 3; ++i)
        shape.AuraValues[i] = info->Effects[i].MiscValue;
    shape.Passive = info->IsPassive();
    shape.EquippedClass = info->EquippedItemClass;
    shape.EquippedSubClassMask = uint32(info->EquippedItemSubClassMask);
    shape.DamageClass = info->DmgClass;
    shape.MaxRange = std::max(info->GetMaxRange(false), info->GetMaxRange(true));
    shape.CastTime = info->CastTimeEntry ? info->CastTimeEntry->CastTime : 0;
    shape.Channeled = info->IsChanneled();
    shape.Duration = info->GetMaxDuration();
    shape.Positive = info->IsPositive();
    return Subclass::BorrowOf(shape);
}

bool HasBodyKit(Player const* body)
{
    Vehicle const* kit = body->GetVehicleKit();
    return kit && kit->GetVehicleInfo()->m_ID == Subclass::BodyVehicleFor(body->getRace(true));
}

void SendVehicleData(Player* body, uint32 vehicleId)
{
    WorldPacket data(SMSG_PLAYER_VEHICLE_DATA, body->GetPackGUID().size() + 4);
    data << body->GetPackGUID();
    data << uint32(vehicleId);
    body->SendMessageToSet(&data, true);
}

bool GiveBodyKit(Player* body)
{
    if (HasBodyKit(body))
        return true;
    uint32 const vehicle = Subclass::BodyVehicleFor(body->getRace(true));
    if (body->GetVehicleKit() || !body->CreateVehicleKit(vehicle, 0))
        return false;
    body->GetVehicleKit()->Reset();
    body->RemoveNpcFlag(UNIT_NPC_FLAG_SPELLCLICK | UNIT_NPC_FLAG_PLAYER_VEHICLE);
    SendVehicleData(body, vehicle);
    return true;
}

void DropBodyKit(Player* body)
{
    if (!HasBodyKit(body))
        return;
    SendVehicleData(body, 0);
    body->RemoveVehicleKit();
}

bool Seated(Player const* left, Player const* right)
{
    return left && right && right->GetVehicleBase() == left;
}

bool LentTo(Player const* player, uint32 spellId)
{
    std::lock_guard<std::mutex> lock(bodyMutex);
    auto const found = lent.find(player->GetGUID().GetCounter());
    return found != lent.end() && std::find(found->second.begin(), found->second.end(), spellId) != found->second.end();
}

void Lend(Player* to, Player const* from, std::initializer_list<Subclass::Borrow> kinds)
{
    std::vector<uint32> current;
    {
        std::lock_guard<std::mutex> lock(bodyMutex);
        auto const found = lent.find(to->GetGUID().GetCounter());
        if (found != lent.end())
            current = found->second;
    }
    std::unordered_set<uint32> const had(current.begin(), current.end());
    std::unordered_set<uint32> wanted;
    for (auto const& [spellId, spell] : from->GetSpellMap())
    {
        if (spell->State == PLAYERSPELL_REMOVED || !spell->Active)
            continue;
        if (std::find(kinds.begin(), kinds.end(), BorrowOfSpell(spellId)) == kinds.end())
            continue;
        if (!had.count(spellId) && to->HasSpell(spellId))
            continue;
        wanted.insert(spellId);
    }
    for (uint32 spellId : current)
        if (!wanted.count(spellId))
            to->removeSpell(spellId, SPEC_MASK_ALL, true);
    std::vector<uint32> kept;
    for (uint32 spellId : wanted)
        if (to->HasSpell(spellId) || to->addSpell(spellId, SPEC_MASK_ALL, true, true))
            kept.push_back(spellId);
    std::lock_guard<std::mutex> lock(bodyMutex);
    lent[to->GetGUID().GetCounter()] = std::move(kept);
}

void LendBothWays(Player* left, Player* right)
{
    Lend(left, right, { Subclass::Borrow::Movement, Subclass::Borrow::Melee });
    Lend(right, left, { Subclass::Borrow::Ranged });
}

void Reclaim(Player* player)
{
    std::vector<uint32> spells;
    {
        std::lock_guard<std::mutex> lock(bodyMutex);
        auto const found = lent.find(player->GetGUID().GetCounter());
        if (found == lent.end())
            return;
        spells = std::move(found->second);
        lent.erase(found);
    }
    for (uint32 spellId : spells)
        player->removeSpell(spellId, SPEC_MASK_ALL, true);
}

bool HasLent(Player const* player)
{
    std::lock_guard<std::mutex> lock(bodyMutex);
    return lent.count(player->GetGUID().GetCounter()) != 0;
}

void GroupUp(Player* left, Player* right)
{
    Group* group = left->GetGroup();
    if (right->GetGroup() && right->GetGroup() == group)
        return;
    if (right->GetGroup())
        right->RemoveFromGroup();
    if (!group)
    {
        group = new Group();
        if (!group->Create(left))
        {
            delete group;
            return;
        }
        sGroupMgr->AddGroup(group);
    }
    if (!group->IsFull())
        group->AddMember(right);
}

bool SameMap(Player const* first, Player const* second)
{
    return first->GetMapId() == second->GetMapId() && first->GetInstanceId() == second->GetInstanceId();
}

bool Greet(uint32 bodyGuid, bool together)
{
    std::lock_guard<std::mutex> lock(bodyMutex);
    return together ? greeted.insert(bodyGuid).second : greeted.erase(bodyGuid) != 0;
}

void Seat(Player* left, Player* right)
{
    if (Seated(left, right) || !GiveBodyKit(left))
        return;
    if (right->GetVehicle())
        right->ExitVehicle();
    right->EnterVehicle(left, RightHeadSeat);
    if (!Seated(left, right))
        return;
    Reclaim(left);
    Reclaim(right);
    LendBothWays(left, right);
    if (!Greet(left->GetGUID().GetCounter(), true))
        return;
    bool const piggyback = Subclass::BodyVehicleFor(left->getRace(true)) == Subclass::TwinsTwoPlayersVehicle;
    Tell(left, right->GetName() + (piggyback ? " rides on your back now." : " is your other head now.") +
        " Their movement and melee abilities are yours to use.");
    Tell(right, (piggyback ? "You ride on " + left->GetName() + "'s back now." :
        "You are " + left->GetName() + "'s other head now.") +
        " Their ranged spells, casts and buffs are yours to use.");
}

bool Ready(Player const* player)
{
    return !player->IsInFlight() && !player->IsBeingTeleported();
}

Player* Connected(uint32 guid)
{
    return ObjectAccessor::FindConnectedPlayer(ObjectGuid::Create<HighGuid::Player>(guid));
}

bool Halted(uint32 riderGuid)
{
    std::lock_guard<std::mutex> lock(bodyMutex);
    return halted.count(riderGuid) != 0;
}

bool BotsTakeOver()
{
    return TwoPlayersBotsAvailable() && sConfigMgr->GetOption<bool>("Subclass.TwoPlayers.Bots", true);
}

bool TimeToCallBot(uint32 guid, bool missing)
{
    uint32 const now = std::max<uint32>(getMSTime(), 1);
    uint32 const delay = sConfigMgr->GetOption<uint32>("Subclass.TwoPlayers.BotSeconds", 10) * IN_MILLISECONDS;
    std::lock_guard<std::mutex> lock(bodyMutex);
    if (!missing)
    {
        absences.erase(guid);
        return false;
    }
    Absence& absence = absences[guid];
    if (!absence.Since)
        absence.Since = now;
    if (getMSTimeDiff(absence.Since, now) < delay)
        return false;
    if (absence.Called && getMSTimeDiff(absence.Called, now) < BotRecallMs)
        return false;
    absence.Called = now;
    return true;
}

void CallBotForMissingHead(Player* player, uint32 partnerGuid, bool missing, bool left)
{
    if (TwoPlayersHeadIsBot(player) || !BotsTakeOver())
        return;
    if (!TimeToCallBot(player->GetGUID().GetCounter(), missing) || !TwoPlayersCallBot(player, partnerGuid))
        return;
    Tell(player, left ? "A bot casts for your Ranged Head until they come back." :
        "A bot steers your Melee Head's body until they come back. Type /pair stop to hold it still and /pair go "
        "to let it roam again.");
}

bool MarkWaiting(uint32 guid, bool wait)
{
    std::lock_guard<std::mutex> lock(bodyMutex);
    return wait ? waiting.insert(guid).second : waiting.erase(guid) != 0;
}

void WaitForBody(Player* rider, bool wait, bool botBody)
{
    bool const changed = MarkWaiting(rider->GetGUID().GetCounter(), wait && rider->IsAlive());
    if (wait && rider->IsAlive())
    {
        rider->SetControlled(true, UNIT_STATE_ROOT);
        if (changed)
            Tell(rider, "Your Melee Head is offline. The body stands still here until they come back.");
        return;
    }
    if (!changed)
        return;
    rider->SetControlled(false, UNIT_STATE_ROOT);
    if (!wait && !botBody)
        Tell(rider, "Your Melee Head is back.");
}

void KeepTogether(Player* player)
{
    uint32 const guid = player->GetGUID().GetCounter();
    uint32 const partnerGuid = TwoPlayersPartnerOf(guid);
    if (!partnerGuid)
        return;
    bool const left = TwoPlayersIsLeftHead(guid);
    Player* const connected = Connected(partnerGuid);
    if (!left)
        WaitForBody(player, !connected, TwoPlayersHeadIsBot(connected));
    CallBotForMissingHead(player, partnerGuid, !connected, left);
    Player* partner = Online(partnerGuid);
    if (!partner)
    {
        Greet(left ? guid : partnerGuid, false);
        if (HasLent(player))
            Reclaim(player);
        return;
    }
    Player* body = left ? player : partner;
    Player* rider = left ? partner : player;
    if (!left && TwoPlayersHeadIsBot(body))
        TwoPlayersSteerBotBody(body, !Halted(guid));
    if (left && SameMap(body, rider) && Ready(body) && Ready(rider))
        GroupUp(body, rider);
    if (Seated(body, rider))
    {
        if (left)
            LendBothWays(body, rider);
        return;
    }
    if (!Ready(body) || !Ready(rider) || body->GetVehicle())
        return;
    bool const close = SameMap(body, rider) && rider->IsWithinDist(body, SeatReach);
    if (left)
    {
        if (!close)
            return;
        if (body->IsAlive() && rider->IsAlive())
            Seat(body, rider);
        return;
    }
    if (!body->IsAlive())
        return;
    if (!rider->IsAlive())
    {
        rider->ResurrectPlayer(body->GetHealthPct() / 100.0f);
        rider->SpawnCorpseBones();
        Tell(rider, "The body you share is back on its feet, and so are you.");
    }
    if (!close)
        rider->TeleportTo(body->GetMapId(), body->GetPositionX(), body->GetPositionY(), body->GetPositionZ(),
            body->GetOrientation());
}

void LetGoOfLentSpells(Player* player)
{
    if (!HasLent(player))
        return;
    uint32 const guid = player->GetGUID().GetCounter();
    Player* partner = Online(TwoPlayersPartnerOf(guid));
    bool const left = TwoPlayersIsLeftHead(guid);
    if (!(left ? Seated(player, partner) : Seated(partner, player)))
        Reclaim(player);
}

uint32 LookOf(Item const* item)
{
    uint32 look = item->GetEntry();
    for (uint8 slot = 0; slot < MAX_ENCHANTMENT_SLOT; ++slot)
        look = look * 31 + item->GetEnchantmentId(EnchantmentSlot(slot));
    return look * 31 + uint32(item->GetItemRandomPropertyId());
}

void DropCopy(Player* rider, uint8 slot, WornCopy& worn)
{
    if (!worn.Copy)
        return;
    rider->_ApplyItemBonuses(worn.Copy->GetTemplate(), slot, false);
    if (!rider->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
        rider->SetGuidValue(PLAYER_FIELD_INV_SLOT_HEAD + (slot * 2), ObjectGuid::Empty);
    if (rider->IsInWorld())
        worn.Copy->DestroyForPlayer(rider);
    delete worn.Copy;
    worn = {};
}

Item* CopyOf(Player* rider, Item const* source)
{
    Item* copy = new Item();
    if (!copy->Create(sObjectMgr->GetGenerator<HighGuid::Item>().Generate(), source->GetEntry(), rider))
    {
        delete copy;
        return nullptr;
    }
    for (uint16 field : { ITEM_FIELD_FLAGS, ITEM_FIELD_RANDOM_PROPERTIES_ID, ITEM_FIELD_PROPERTY_SEED,
        ITEM_FIELD_DURABILITY, ITEM_FIELD_MAXDURABILITY })
        copy->SetUInt32Value(field, source->GetUInt32Value(field));
    for (uint8 slot = 0; slot < MAX_ENCHANTMENT_SLOT; ++slot)
        copy->SetUInt32Value(ITEM_FIELD_ENCHANTMENT_1_1 + slot * MAX_ENCHANTMENT_OFFSET,
            source->GetEnchantmentId(EnchantmentSlot(slot)));
    return copy;
}

void ShowCopy(Player* rider, Item* copy)
{
    UpdateData data;
    copy->BuildCreateUpdateBlockForPlayer(&data, rider);
    WorldPacket packet;
    data.BuildPacket(packet);
    rider->SendDirectMessage(&packet);
}

void WearTheBodysGear(Player* rider, Player const* body)
{
    std::lock_guard<std::mutex> lock(copyMutex);
    uint32 const guid = rider->GetGUID().GetCounter();
    if (!body)
    {
        auto const found = wornCopies.find(guid);
        if (found == wornCopies.end())
            return;
        for (uint8 slot = EQUIPMENT_SLOT_START; slot < EQUIPMENT_SLOT_END; ++slot)
            DropCopy(rider, slot, found->second[slot]);
        wornCopies.erase(found);
        return;
    }
    WornCopies& copies = wornCopies[guid];
    for (uint8 slot = EQUIPMENT_SLOT_START; slot < EQUIPMENT_SLOT_END; ++slot)
    {
        Item const* worn = body->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
        WornCopy& copy = copies[slot];
        uint32 const look = worn ? LookOf(worn) : 0;
        if (!copy.Copy && !worn)
            continue;
        if (copy.Copy && worn && copy.Source == worn->GetGUID() && copy.Look == look)
            continue;
        DropCopy(rider, slot, copy);
        if (!worn || rider->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
            continue;
        Item* made = CopyOf(rider, worn);
        if (!made)
            continue;
        copy = { made, worn->GetGUID(), look };
        ShowCopy(rider, made);
        rider->SetGuidValue(PLAYER_FIELD_INV_SLOT_HEAD + (slot * 2), made->GetGUID());
        rider->_ApplyItemBonuses(made->GetTemplate(), slot, true);
    }
}

void LeaveOwnGearBehind(Player* rider)
{
    bool destroyed = false;
    for (uint8 slot = EQUIPMENT_SLOT_START; slot < EQUIPMENT_SLOT_END; ++slot)
        if (rider->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
        {
            rider->DestroyItem(INVENTORY_SLOT_BAG_0, slot, true);
            destroyed = true;
        }
    if (destroyed)
        Tell(rider, "Your own gear is gone: the body wears the gear for both of you.");
}

void RedirectThreatTo(Player* rider, Player const* body)
{
    uint32 const guid = rider->GetGUID().GetCounter();
    {
        std::lock_guard<std::mutex> lock(copyMutex);
        if (body ? !redirected.insert(guid).second : !redirected.erase(guid))
            return;
    }
    if (body)
        rider->GetThreatMgr().RegisterRedirectThreat(BodyThreatKey, body->GetGUID(), 100);
    else
        rider->GetThreatMgr().UnregisterRedirectThreat(BodyThreatKey);
}

void FollowTheBody(Player* rider)
{
    uint32 const guid = rider->GetGUID().GetCounter();
    uint32 const partnerGuid = TwoPlayersPartnerOf(guid);
    Player* body = partnerGuid && !TwoPlayersIsLeftHead(guid) ? Online(partnerGuid) : nullptr;
    bool const seated = Seated(body, rider);
    if (seated)
        LeaveOwnGearBehind(rider);
    RedirectThreatTo(rider, seated ? body : nullptr);
    WearTheBodysGear(rider, seated ? body : nullptr);
}

bool WearsNoGearOfItsOwn(Player const* player)
{
    uint32 const guid = player->GetGUID().GetCounter();
    return TwoPlayersPartnerOf(guid) && !TwoPlayersIsLeftHead(guid);
}

bool RoomForBothHeads(Player* body, Group* group)
{
    if (!group || group == body->GetGroup())
        return true;
    uint32 const capacity = group->isRaidGroup() ? MAXRAIDSIZE : MAXGROUPSIZE;
    if (group->GetMembersCount() + 2 <= capacity)
        return true;
    std::string const refusal = "The two-headed ogre needs two free spots in the party, one for each head.";
    Tell(body, refusal);
    if (Player* leader = ObjectAccessor::FindConnectedPlayer(group->GetLeaderGUID()))
        Tell(leader, refusal);
    return false;
}

class mod_subclass_two_players_player : public PlayerScript
{
public:
    mod_subclass_two_players_player() : PlayerScript("mod_subclass_two_players_player", { PLAYERHOOK_ON_UPDATE,
        PLAYERHOOK_ON_PLAYER_JUST_DIED, PLAYERHOOK_ON_LOGOUT, PLAYERHOOK_CAN_GROUP_ACCEPT, PLAYERHOOK_CAN_EQUIP_ITEM,
        PLAYERHOOK_CAN_UNEQUIP_ITEM })
    {
    }

    void OnPlayerUpdate(Player* player, uint32 diff) override
    {
        uint32 const guid = player->GetGUID().GetCounter();
        {
            std::lock_guard<std::mutex> lock(bodyMutex);
            uint32& since = sinceCheck[guid];
            since += diff;
            if (since < KeepTogetherMs)
                return;
            since = 0;
        }
        KeepTogether(player);
        LetGoOfLentSpells(player);
        FollowTheBody(player);
    }

    bool OnPlayerCanEquipItem(Player* player, uint8 /*slot*/, uint16& /*dest*/, Item* item, bool /*swap*/,
        bool notLoading) override
    {
        return !notLoading || item->GetTemplate()->InventoryType == INVTYPE_BAG || !WearsNoGearOfItsOwn(player);
    }

    bool OnPlayerCanUnequipItem(Player* player, uint16 pos, bool /*swap*/) override
    {
        return (pos >> 8) != INVENTORY_SLOT_BAG_0 || (pos & 255) >= EQUIPMENT_SLOT_END ||
            !WearsNoGearOfItsOwn(player);
    }

    void OnPlayerJustDied(Player* player) override
    {
        uint32 const guid = player->GetGUID().GetCounter();
        if (!TwoPlayersIsLeftHead(guid))
            return;
        Player* right = Online(TwoPlayersPartnerOf(guid));
        if (!Seated(player, right) || !right->IsAlive())
            return;
        right->ExitVehicle();
        Unit::Kill(right, right, false);
        Tell(right, "The body you share has fallen, and you with it.");
    }

    bool OnPlayerCanGroupAccept(Player* player, Group* group) override
    {
        uint32 const guid = player->GetGUID().GetCounter();
        uint32 const partnerGuid = TwoPlayersPartnerOf(guid);
        if (!partnerGuid)
            return true;
        if (TwoPlayersIsLeftHead(guid))
            return RoomForBothHeads(player, group);
        Player* left = Online(partnerGuid);
        if (!left || left->GetGroup() == group)
            return true;
        Tell(player, "Your Melee Head handles the group: ask them to invite or join, and you come along.");
        return false;
    }

    void OnPlayerLogout(Player* player) override
    {
        RedirectThreatTo(player, nullptr);
        WearTheBodysGear(player, nullptr);
        Reclaim(player);
        DropBodyKit(player);
        std::lock_guard<std::mutex> lock(bodyMutex);
        sinceCheck.erase(player->GetGUID().GetCounter());
        waiting.erase(player->GetGUID().GetCounter());
        halted.erase(player->GetGUID().GetCounter());
        absences.erase(player->GetGUID().GetCounter());
    }
};

class mod_subclass_two_players_unit : public UnitScript
{
public:
    mod_subclass_two_players_unit() : UnitScript("mod_subclass_two_players_unit", true, { UNITHOOK_ON_DAMAGE }) { }

    void OnDamage(Unit* /*attacker*/, Unit* victim, uint32& damage) override
    {
        Player* right = victim ? victim->ToPlayer() : nullptr;
        if (!right || !right->GetVehicle())
            return;
        uint32 const guid = right->GetGUID().GetCounter();
        if (TwoPlayersIsLeftHead(guid))
            return;
        Player* left = Online(TwoPlayersPartnerOf(guid));
        if (Seated(left, right))
            damage = 0;
    }
};

char const* RefusalFor(Subclass::Borrow kind)
{
    switch (kind)
    {
        case Subclass::Borrow::Movement:
            return "Only the Melee Head moves the body.";
        case Subclass::Borrow::Melee:
            return "The Melee Head uses the melee abilities.";
        default:
            return "The Ranged Head uses the ranged spells, casts and buffs.";
    }
}

class mod_subclass_two_players_spells : public AllSpellScript
{
public:
    mod_subclass_two_players_spells() : AllSpellScript("mod_subclass_two_players_spells", { ALLSPELLHOOK_CAN_PREPARE })
    {
    }

    bool CanPrepare(Spell* spell, SpellCastTargets const* targets, AuraEffect const* /*triggeredByAura*/) override
    {
        if (spell->IsTriggered() || !targets)
            return true;
        Player* presser = spell->GetCaster() ? spell->GetCaster()->ToPlayer() : nullptr;
        if (!presser)
            return true;
        uint32 const guid = presser->GetGUID().GetCounter();
        uint32 const partnerGuid = TwoPlayersPartnerOf(guid);
        if (!partnerGuid)
            return true;
        bool const pressedByLeft = TwoPlayersIsLeftHead(guid);
        Player* other = Online(partnerGuid);
        Player* left = pressedByLeft ? presser : other;
        Player* right = pressedByLeft ? other : presser;
        if (!Seated(left, right))
            return true;
        SpellInfo const* info = spell->GetSpellInfo();
        Subclass::Borrow const kind = BorrowOfSpell(info->Id);
        if (kind != Subclass::Borrow::None && !Subclass::BodyCasts(kind, pressedByLeft))
        {
            Tell(presser, RefusalFor(kind));
            return false;
        }
        if (!pressedByLeft)
            spell->AddTriggeredCastFlag(TRIGGERED_IGNORE_EQUIPPED_ITEM_REQUIREMENT);
        if (!LentTo(presser, info->Id))
            return true;
        Player* owner = other;
        Powers const ownerPower = Powers(info->PowerType);
        Powers const payerPower = presser->getPowerType();
        int32 const ownerCost = std::max(0, info->CalcPowerCost(owner, info->GetSchoolMask()));
        uint32 const share = Subclass::ShareOfOwnPower(uint32(ownerCost), owner->GetMaxPower(ownerPower),
            presser->GetMaxPower(payerPower));
        if (presser->GetPower(payerPower) < share)
        {
            Spell::SendCastResult(presser, info, 0, SPELL_FAILED_NO_POWER);
            return false;
        }
        if (share)
            presser->ModifyPower(payerPower, -int32(share));
        spell->AddTriggeredCastFlag(TriggerCastFlags(TRIGGERED_IGNORE_POWER_AND_REAGENT_COST |
            TRIGGERED_IGNORE_CASTER_MOUNTED_OR_ON_VEHICLE | TRIGGERED_COUNTS_AS_CAST |
            TRIGGERED_IGNORE_EQUIPPED_ITEM_REQUIREMENT));
        return true;
    }
};
}

Player* TwoPlayersSeatedPartner(Player const* head)
{
    uint32 const guid = head->GetGUID().GetCounter();
    Player* partner = Online(TwoPlayersPartnerOf(guid));
    if (!partner)
        return nullptr;
    bool const left = TwoPlayersIsLeftHead(guid);
    return (left ? Seated(head, partner) : Seated(partner, head)) ? partner : nullptr;
}

void TwoPlayersSeparate(uint32 first, uint32 second)
{
    for (uint32 guid : { first, second })
        if (Player* player = ObjectAccessor::FindPlayerByLowGUID(guid))
        {
            if (player->GetVehicle() && player->GetVehicleBase() &&
                player->GetVehicleBase()->GetGUID().GetCounter() == (guid == first ? second : first))
                player->ExitVehicle();
            RedirectThreatTo(player, nullptr);
            WearTheBodysGear(player, nullptr);
            Reclaim(player);
            DropBodyKit(player);
            if (MarkWaiting(guid, false))
                player->SetControlled(false, UNIT_STATE_ROOT);
        }
}

std::string TwoPlayersHalt(Player* rider, bool halt)
{
    uint32 const guid = rider->GetGUID().GetCounter();
    uint32 const partnerGuid = TwoPlayersPartnerOf(guid);
    if (!partnerGuid || TwoPlayersIsLeftHead(guid))
        return "Only a Ranged Head can stop or start a bot that steers the body.";
    Player* body = Online(partnerGuid);
    if (!TwoPlayersHeadIsBot(body))
        return "Your Melee Head steers the body, not a bot.";
    {
        std::lock_guard<std::mutex> lock(bodyMutex);
        if (halt)
            halted.insert(guid);
        else
            halted.erase(guid);
    }
    TwoPlayersSteerBotBody(body, !halt);
    return halt ? "The body stops and waits here." : "The body roams, fights and quests again.";
}

void AddSC_mod_subclass_two_players()
{
    new mod_subclass_two_players_player();
    new mod_subclass_two_players_unit();
    new mod_subclass_two_players_spells();
}
