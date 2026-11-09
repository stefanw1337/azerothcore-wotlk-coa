/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license.
 */

#include "AllSpellScript.h"
#include "Chat.h"
#include "Group.h"
#include "GroupMgr.h"
#include "ObjectAccessor.h"
#include "Opcodes.h"
#include "Player.h"
#include "PlayerScript.h"
#include "Spell.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "SubclassRules.h"
#include "UnitScript.h"
#include "Vehicle.h"
#include "WorldPacket.h"
#include <algorithm>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

uint32 TwoPlayersPartnerOf(uint32 guid);
bool TwoPlayersIsLeftHead(uint32 guid);

namespace
{
constexpr uint32 KeepTogetherMs = 1000;
constexpr float SeatReach = 40.0f;
constexpr int8 RightHeadSeat = 0;

std::mutex& bodyMutex = *new std::mutex;
std::unordered_map<uint32, std::vector<uint32>>& lent = *new std::unordered_map<uint32, std::vector<uint32>>;
std::unordered_map<uint32, uint32>& sinceCheck = *new std::unordered_map<uint32, uint32>;
std::unordered_set<uint32>& waiting = *new std::unordered_set<uint32>;

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
    shape.Passive = info->IsPassive();
    shape.EquippedClass = info->EquippedItemClass;
    shape.EquippedSubClassMask = uint32(info->EquippedItemSubClassMask);
    return Subclass::BorrowOf(shape);
}

bool HasBodyKit(Player const* body)
{
    Vehicle const* kit = body->GetVehicleKit();
    return kit && kit->GetVehicleInfo()->m_ID == Subclass::TwoPlayersVehicle;
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
    if (body->GetVehicleKit() || !body->CreateVehicleKit(Subclass::TwoPlayersVehicle, 0))
        return false;
    body->GetVehicleKit()->Reset();
    body->RemoveNpcFlag(UNIT_NPC_FLAG_SPELLCLICK | UNIT_NPC_FLAG_PLAYER_VEHICLE);
    SendVehicleData(body, Subclass::TwoPlayersVehicle);
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

void Lend(Player* to, Player const* from, Subclass::Borrow kind)
{
    std::vector<uint32> added;
    for (auto const& [spellId, spell] : from->GetSpellMap())
    {
        if (spell->State == PLAYERSPELL_REMOVED || !spell->Active || to->HasSpell(spellId))
            continue;
        if (BorrowOfSpell(spellId) != kind)
            continue;
        if (to->addSpell(spellId, SPEC_MASK_ALL, true, true))
            added.push_back(spellId);
    }
    std::lock_guard<std::mutex> lock(bodyMutex);
    auto& mine = lent[to->GetGUID().GetCounter()];
    mine.insert(mine.end(), added.begin(), added.end());
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
    Lend(left, right, Subclass::Borrow::Movement);
    Lend(right, left, Subclass::Borrow::RangedAttack);
    Tell(left, right->GetName() + " sits on your shoulders now. Their movement spells are yours to use.");
    Tell(right, "You ride on " + left->GetName() + "'s body now. Their ranged attacks are yours to use.");
}

bool Ready(Player const* player)
{
    return !player->IsInFlight() && !player->IsBeingTeleported();
}

bool Connected(uint32 guid)
{
    return ObjectAccessor::FindConnectedPlayer(ObjectGuid::Create<HighGuid::Player>(guid)) != nullptr;
}

bool MarkWaiting(uint32 guid, bool wait)
{
    std::lock_guard<std::mutex> lock(bodyMutex);
    return wait ? waiting.insert(guid).second : waiting.erase(guid) != 0;
}

void WaitForBody(Player* rider, bool wait)
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
    if (!wait)
        Tell(rider, "Your Melee Head is back.");
}

void KeepTogether(Player* player)
{
    uint32 const guid = player->GetGUID().GetCounter();
    uint32 const partnerGuid = TwoPlayersPartnerOf(guid);
    if (!partnerGuid)
        return;
    bool const left = TwoPlayersIsLeftHead(guid);
    if (!left)
        WaitForBody(player, !Connected(partnerGuid));
    Player* partner = Online(partnerGuid);
    if (!partner)
    {
        if (HasLent(player))
            Reclaim(player);
        return;
    }
    Player* body = left ? player : partner;
    Player* rider = left ? partner : player;
    if (left && SameMap(body, rider) && Ready(body) && Ready(rider))
        GroupUp(body, rider);
    if (Seated(body, rider) || !Ready(body) || !Ready(rider) || body->GetVehicle())
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

class mod_subclass_two_players_player : public PlayerScript
{
public:
    mod_subclass_two_players_player() : PlayerScript("mod_subclass_two_players_player", { PLAYERHOOK_ON_UPDATE,
        PLAYERHOOK_ON_PLAYER_JUST_DIED, PLAYERHOOK_ON_LOGOUT, PLAYERHOOK_CAN_GROUP_ACCEPT })
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
        if (!partnerGuid || TwoPlayersIsLeftHead(guid))
            return true;
        Player* left = Online(partnerGuid);
        if (!left || left->GetGroup() == group)
            return true;
        Tell(player, "Your Melee Head handles the group: ask them to invite or join, and you come along.");
        return false;
    }

    void OnPlayerLogout(Player* player) override
    {
        Reclaim(player);
        DropBodyKit(player);
        std::lock_guard<std::mutex> lock(bodyMutex);
        sinceCheck.erase(player->GetGUID().GetCounter());
        waiting.erase(player->GetGUID().GetCounter());
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
        SpellInfo const* info = spell->GetSpellInfo();
        Subclass::Borrow const kind = BorrowOfSpell(info->Id);
        if (kind == Subclass::Borrow::None)
            return true;
        bool const pressedByLeft = TwoPlayersIsLeftHead(guid);
        Player* other = Online(partnerGuid);
        Player* left = pressedByLeft ? presser : other;
        Player* right = pressedByLeft ? other : presser;
        if (!Seated(left, right))
            return true;
        if (!Subclass::BodyCasts(kind, pressedByLeft))
        {
            Tell(presser, kind == Subclass::Borrow::Movement ? "Only the Melee Head moves the body." :
                "The Ranged Head uses the ranged attacks.");
            return false;
        }
        if (!LentTo(presser, info->Id))
            return true;
        Player* owner = pressedByLeft ? right : left;
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
        SpellCastTargets aimed = *targets;
        left->CastSpell(aimed, info, nullptr, TriggerCastFlags(TRIGGERED_IGNORE_POWER_AND_REAGENT_COST |
            TRIGGERED_IGNORE_CASTER_MOUNTED_OR_ON_VEHICLE | TRIGGERED_COUNTS_AS_CAST));
        return false;
    }
};
}

void TwoPlayersSeparate(uint32 first, uint32 second)
{
    for (uint32 guid : { first, second })
        if (Player* player = ObjectAccessor::FindPlayerByLowGUID(guid))
        {
            if (player->GetVehicle() && player->GetVehicleBase() &&
                player->GetVehicleBase()->GetGUID().GetCounter() == (guid == first ? second : first))
                player->ExitVehicle();
            Reclaim(player);
            DropBodyKit(player);
            if (MarkWaiting(guid, false))
                player->SetControlled(false, UNIT_STATE_ROOT);
        }
}

void AddSC_mod_subclass_two_players()
{
    new mod_subclass_two_players_player();
    new mod_subclass_two_players_unit();
    new mod_subclass_two_players_spells();
}
