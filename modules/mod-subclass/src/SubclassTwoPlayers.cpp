/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license.
 */

#include "AllSpellScript.h"
#include "AscensionSpecialization.h"
#include "Chat.h"
#include "Config.h"
#include "GameTime.h"
#include "Group.h"
#include "GroupMgr.h"
#include "GroupSharedSlots.h"
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
#include "WorldSession.h"
#include <algorithm>
#include <array>
#include <initializer_list>
#include <map>
#include <mutex>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <vector>

uint32 TwoPlayersPartnerOf(uint32 guid);
bool TwoPlayersIsLeftHead(uint32 guid);
bool TwoPlayersBotsAvailable();
bool TwoPlayersCallBot(Player* master, uint32 headGuid);
bool TwoPlayersHeadIsBot(Player const* head);
std::string TwoPlayersHalt(Player* rider, bool halt);
void SendSubclassMessage(Player* player, std::string const& message);
void TwoPlayersKeepBotRiderStill(Player* rider);
Player* TwoPlayersSeatedPartner(Player const* head);
void TwoPlayersShareBuffs(Player* first, Player* second);
void TwoPlayersDropSharedBuffs(Player* holder);
void TwoPlayersSteerBotBody(Player* body, bool roam);
void TwoPlayersKeepBotBodyRoaming(Player* body);
void KeepTwoPlayersBrother(Player* player);

namespace
{
constexpr uint32 KeepTogetherMs = 1000;
constexpr float SeatReach = 40.0f;
constexpr uint32 LentBuffRecastMs = 60000;
constexpr float LentHealBelowPct = 60.0f;
constexpr std::size_t LentBarChunk = 200;
constexpr uint32 OneBodyRepeatMs = 30000;
constexpr int8 RightHeadSeat = 0;
constexpr uint32 BotRecallMs = 30000;
constexpr uint32 BodyThreatKey = 0x7FF09600;
constexpr uint32 HiddenArrivalMs = 15000;

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
std::unordered_set<uint32>& exitRequests = *new std::unordered_set<uint32>;
std::unordered_set<uint32>& steered = *new std::unordered_set<uint32>;
std::map<uint32, std::set<uint32>>& toldGroups = *new std::map<uint32, std::set<uint32>>;
std::unordered_map<uint32, uint32>& groupToldAt = *new std::unordered_map<uint32, uint32>;
std::map<std::pair<uint32, uint32>, uint32>& lentBuffs = *new std::map<std::pair<uint32, uint32>, uint32>;
std::unordered_map<uint32, std::vector<uint32>>& handedOver = *new std::unordered_map<uint32, std::vector<uint32>>;
std::unordered_map<uint32, std::vector<uint32>>& toldLent = *new std::unordered_map<uint32, std::vector<uint32>>;
std::unordered_map<uint32, std::vector<uint32>>& toldHanded = *new std::unordered_map<uint32, std::vector<uint32>>;
std::unordered_map<uint32, Absence>& absences = *new std::unordered_map<uint32, Absence>;
std::unordered_map<uint32, uint32>& hiddenArrivals = *new std::unordered_map<uint32, uint32>;

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

bool ClassAbilityOf(Player const* owner, uint32 spellId)
{
    static std::mutex abilityMutex;
    static std::unordered_map<uint8, std::unordered_set<uint32>> byClass;
    std::lock_guard<std::mutex> lock(abilityMutex);
    auto [found, fresh] = byClass.try_emplace(owner->getClass());
    if (fresh)
        for (AscensionClassAbility const& ability : GetAscensionClassAbilities(owner->getClass()))
        {
            found->second.insert(ability.SpellId);
            found->second.insert(ability.FirstSpellId);
        }
    return found->second.count(spellId) != 0;
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

bool RidesHidden(Player const* rider)
{
    return Subclass::BodyVehicleFor(rider->getRace(true)) == Subclass::TwoPlayersVehicle;
}

bool LoggingOut(Player const* player)
{
    return player && player->GetSession() && player->GetSession()->isLogingOut();
}

void HideRiderApart(Player* rider)
{
    uint32 const guid = rider->GetGUID().GetCounter();
    uint32 const partnerGuid = TwoPlayersPartnerOf(guid);
    if (!partnerGuid || TwoPlayersIsLeftHead(guid) || !RidesHidden(rider))
        return;
    Player* body = ObjectAccessor::FindPlayerByLowGUID(partnerGuid);
    bool const visible = Seated(body, rider) && !LoggingOut(rider) && !LoggingOut(body);
    if (rider->IsVisible() != visible)
        rider->SetVisible(visible);
}

bool LentTo(Player const* player, uint32 spellId)
{
    std::lock_guard<std::mutex> lock(bodyMutex);
    auto const found = lent.find(player->GetGUID().GetCounter());
    return found != lent.end() && std::find(found->second.begin(), found->second.end(), spellId) != found->second.end();
}

std::string SpellNames(std::vector<uint32> const& spells)
{
    std::string names;
    for (uint32 spellId : spells)
        if (SpellInfo const* info = sSpellMgr->GetSpellInfo(spellId))
            names += (names.empty() ? "" : ", ") + std::string(info->SpellName[0]);
    return names;
}

void TellOnce(Player* player, std::unordered_map<uint32, std::vector<uint32>>& told, std::vector<uint32> spells,
    std::string const& lead)
{
    std::sort(spells.begin(), spells.end());
    {
        std::lock_guard<std::mutex> lock(bodyMutex);
        std::vector<uint32>& last = told[player->GetGUID().GetCounter()];
        if (last == spells)
            return;
        last = spells;
    }
    if (!spells.empty())
        Tell(player, lead + SpellNames(spells) + ".");
}

void ForgetTold(uint32 guid)
{
    std::lock_guard<std::mutex> lock(bodyMutex);
    toldLent.erase(guid);
    toldHanded.erase(guid);
}

void ShowLentOnBar(Player* to, std::vector<uint32> spells)
{
    std::sort(spells.begin(), spells.end());
    std::string line;
    for (uint32 spellId : spells)
    {
        std::string const item = std::to_string(spellId);
        if (!line.empty() && line.size() + item.size() + 1 > LentBarChunk)
        {
            SendSubclassMessage(to, "LENTSPELLS " + line);
            line.clear();
        }
        line += (line.empty() ? "" : ",") + item;
    }
    if (!line.empty())
        SendSubclassMessage(to, "LENTSPELLS " + line);
    SendSubclassMessage(to, "LENTDONE");
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
        if (spell->State == PLAYERSPELL_REMOVED || !spell->Active || !ClassAbilityOf(from, spellId))
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
    {
        bool const known = to->HasSpell(spellId);
        if (!known && !to->addSpell(spellId, SPEC_MASK_ALL, true, true))
            continue;
        if (!known)
            to->SendLearnPacket(spellId, true);
        kept.push_back(spellId);
    }
    TellOnce(to, toldLent, kept, "From " + from->GetName() + " you now use: ");
    if (std::unordered_set<uint32>(kept.begin(), kept.end()) != had)
        ShowLentOnBar(to, kept);
    std::lock_guard<std::mutex> lock(bodyMutex);
    lent[to->GetGUID().GetCounter()] = std::move(kept);
}

std::vector<uint32> HandedOver(Player const* owner)
{
    std::lock_guard<std::mutex> lock(bodyMutex);
    auto const found = handedOver.find(owner->GetGUID().GetCounter());
    return found == handedOver.end() ? std::vector<uint32>() : found->second;
}

void GiveBack(Player* owner, std::vector<uint32> const& spells)
{
    bool returned = false;
    for (uint32 spellId : spells)
    {
        if (!owner->HasSpell(spellId))
            continue;
        owner->SendLearnPacket(spellId, true);
        returned = true;
    }
    if (returned)
        owner->SendActionButtons(1);
}

void HandOver(Player* owner, Player const* holder, std::initializer_list<Subclass::Borrow> kinds)
{
    std::vector<uint32> const current = HandedOver(owner);
    std::unordered_set<uint32> const had(current.begin(), current.end());
    std::vector<uint32> wanted;
    for (auto const& [spellId, spell] : owner->GetSpellMap())
    {
        if (spell->State == PLAYERSPELL_REMOVED || !spell->Active || LentTo(owner, spellId) ||
            !ClassAbilityOf(owner, spellId))
            continue;
        if (std::find(kinds.begin(), kinds.end(), BorrowOfSpell(spellId)) != kinds.end())
            wanted.push_back(spellId);
    }
    std::unordered_set<uint32> const keep(wanted.begin(), wanted.end());
    std::vector<uint32> back;
    for (uint32 spellId : current)
        if (!keep.count(spellId))
            back.push_back(spellId);
    GiveBack(owner, back);
    for (uint32 spellId : wanted)
        if (!had.count(spellId))
            owner->SendLearnPacket(spellId, false);
    TellOnce(owner, toldHanded, wanted, holder->GetName() + " uses these now: ");
    std::lock_guard<std::mutex> lock(bodyMutex);
    handedOver[owner->GetGUID().GetCounter()] = std::move(wanted);
}

void LendBothWays(Player* left, Player* right)
{
    Lend(left, right, { Subclass::Borrow::Movement, Subclass::Borrow::Melee });
    Lend(right, left, { Subclass::Borrow::Ranged });
    HandOver(left, right, { Subclass::Borrow::Ranged });
    HandOver(right, left, { Subclass::Borrow::Movement, Subclass::Borrow::Melee });
}

void Reclaim(Player* player)
{
    std::vector<uint32> spells;
    std::vector<uint32> handed;
    {
        std::lock_guard<std::mutex> lock(bodyMutex);
        uint32 const guid = player->GetGUID().GetCounter();
        if (auto const found = lent.find(guid); found != lent.end())
        {
            spells = std::move(found->second);
            lent.erase(found);
        }
        if (auto const found = handedOver.find(guid); found != handedOver.end())
        {
            handed = std::move(found->second);
            handedOver.erase(found);
        }
    }
    for (uint32 spellId : spells)
        player->removeSpell(spellId, SPEC_MASK_ALL, true);
    if (!spells.empty())
        ShowLentOnBar(player, {});
    GiveBack(player, handed);
    TwoPlayersDropSharedBuffs(player);
}

bool HasLent(Player const* player)
{
    std::lock_guard<std::mutex> lock(bodyMutex);
    uint32 const guid = player->GetGUID().GetCounter();
    return lent.count(guid) != 0 || handedOver.count(guid) != 0;
}

void ShowOneBodyToGroup(Player* body, Player* rider)
{
    Group* group = body->GetGroup();
    if (!group)
        return;
    std::string const line = "TWOPLAYERS " + body->GetName() + " " + rider->GetName();
    for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
    {
        Player* member = ref->GetSource();
        if (!member || !member->IsInWorld())
            continue;
        {
            std::lock_guard<std::mutex> lock(bodyMutex);
            uint32& last = groupToldAt[body->GetGUID().GetCounter()];
            if (getMSTimeDiff(last, getMSTime()) > OneBodyRepeatMs)
            {
                toldGroups.erase(body->GetGUID().GetCounter());
                last = getMSTime();
            }
            if (!toldGroups[body->GetGUID().GetCounter()].insert(member->GetGUID().GetCounter()).second)
                continue;
        }
        SendSubclassMessage(member, line);
    }
}

void ShowTwoAgain(Player* body)
{
    std::set<uint32> told;
    {
        std::lock_guard<std::mutex> lock(bodyMutex);
        auto const found = toldGroups.find(body->GetGUID().GetCounter());
        if (found == toldGroups.end())
            return;
        told = std::move(found->second);
        toldGroups.erase(found);
    }
    for (uint32 guid : told)
        if (Player* member = Online(guid))
            SendSubclassMessage(member, "TWOPLAYERSOFF " + body->GetName());
}

void GroupUp(Player* left, Player* right)
{
    bool const playerLeads = TwoPlayersHeadIsBot(left) && !TwoPlayersHeadIsBot(right);
    Player* leader = playerLeads ? right : left;
    Player* joiner = playerLeads ? left : right;
    Group* group = leader->GetGroup();
    if (joiner->GetGroup() && joiner->GetGroup() == group)
        return;
    if (joiner->GetGroup())
        joiner->RemoveFromGroup();
    if (!group)
    {
        group = new Group();
        if (!group->Create(leader))
        {
            delete group;
            return;
        }
        sGroupMgr->AddGroup(group);
    }
    if (GroupHasRoomFor(group, joiner->GetGUID()))
        group->AddMember(joiner);
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

void HideArrival(Player* rider)
{
    {
        std::lock_guard<std::mutex> lock(bodyMutex);
        hiddenArrivals[rider->GetGUID().GetCounter()] = getMSTime();
    }
    rider->SetVisible(false);
}

void RevealArrival(Player* rider, bool onlyWhenOverdue)
{
    {
        std::lock_guard<std::mutex> lock(bodyMutex);
        auto const found = hiddenArrivals.find(rider->GetGUID().GetCounter());
        if (found == hiddenArrivals.end())
            return;
        if (onlyWhenOverdue && getMSTimeDiff(found->second, getMSTime()) < HiddenArrivalMs)
            return;
        hiddenArrivals.erase(found);
    }
    if (!rider->IsVisible())
        rider->SetVisible(true);
}

void Seat(Player* left, Player* right)
{
    if (Seated(left, right) || !GiveBodyKit(left))
        return;
    if (right->GetVehicle())
        right->ExitVehicle();
    if ((TwoPlayersHeadIsBot(right) || RidesHidden(right)) && !right->IsVisible())
        right->SetVisible(true);
    RevealArrival(right, false);
    right->EnterVehicle(left, RightHeadSeat);
    if (!Seated(left, right))
        return;
    Reclaim(left);
    Reclaim(right);
    LendBothWays(left, right);
    TwoPlayersShareBuffs(left, right);
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

void KeepTheTwoPlayersBody(Player* player)
{
    uint8 const race = player->getRace(true);
    uint8 const gender = race == Subclass::ThinHumanRace ? GENDER_FEMALE : GENDER_MALE;
    if ((race != Subclass::TwoHeadedOgreRace && race != Subclass::ThinHumanRace) ||
        player->GetByteValue(PLAYER_BYTES_3, 0) == gender)
        return;
    player->SetByteValue(UNIT_FIELD_BYTES_0, 2, gender);
    player->SetByteValue(PLAYER_BYTES_3, 0, gender);
    player->InitDisplayIds();
    LOG_INFO("module", "mod-subclass: Two-Players head {} gets its Two-Players body back", player->GetName());
}

Unit* FoeOfTheBody(Player* body)
{
    Unit* foe = body->GetVictim();
    if (!foe && body->GetTarget())
        foe = ObjectAccessor::GetUnit(*body, body->GetTarget());
    return foe && foe->IsAlive() && body->IsValidAttackTarget(foe) ? foe : nullptr;
}

bool LeavesAnAura(SpellInfo const* info)
{
    for (SpellEffectInfo const& effect : info->Effects)
        if (effect.IsAura())
            return true;
    return false;
}

bool BuffedRecently(Player const* bot, SpellInfo const* info, uint32 now)
{
    uint32 const keep = info->GetMaxDuration() > 0 ? uint32(info->GetMaxDuration()) : LentBuffRecastMs;
    std::lock_guard<std::mutex> lock(bodyMutex);
    uint32& last = lentBuffs[{ bot->GetGUID().GetCounter(), info->Id }];
    if (last && getMSTimeDiff(last, now) < keep)
        return true;
    last = now;
    return false;
}

void UseLentAbilities(Player* bot, Player* body)
{
    if (!TwoPlayersHeadIsBot(bot) || !bot->IsAlive() || bot->IsNonMeleeSpellCast(false))
        return;
    std::vector<uint32> spells;
    {
        std::lock_guard<std::mutex> lock(bodyMutex);
        auto const found = lent.find(bot->GetGUID().GetCounter());
        if (found == lent.end())
            return;
        spells = found->second;
    }
    Unit* foe = body->IsInCombat() ? FoeOfTheBody(body) : nullptr;
    uint32 const now = getMSTime();
    for (uint32 spellId : spells)
    {
        SpellInfo const* info = sSpellMgr->GetSpellInfo(spellId);
        if (!info || info->IsPassive() || bot->HasSpellCooldown(spellId) ||
            bot->GetGlobalCooldownMgr().HasGlobalCooldown(info))
            continue;
        Unit* target = nullptr;
        if (!info->IsPositive())
        {
            bool const upClose = BorrowOfSpell(spellId) == Subclass::Borrow::Melee;
            if (foe && (!upClose || body->IsWithinMeleeRange(foe)))
                target = foe;
        }
        else if (LeavesAnAura(info))
        {
            if (!bot->HasAura(spellId) && !BuffedRecently(bot, info, now))
                target = bot;
        }
        else if (body->GetHealthPct() < LentHealBelowPct)
            target = body;
        if (target && bot->CastSpell(target, spellId, false) == SPELL_CAST_OK)
            return;
    }
}

bool Steered(uint32 riderGuid, bool botBody)
{
    std::lock_guard<std::mutex> lock(bodyMutex);
    return botBody ? steered.insert(riderGuid).second : (steered.erase(riderGuid), false);
}

bool TakeExitRequest(uint32 guid)
{
    std::lock_guard<std::mutex> lock(bodyMutex);
    return exitRequests.erase(guid) != 0;
}

void KeepTogether(Player* player)
{
    uint32 const guid = player->GetGUID().GetCounter();
    uint32 const partnerGuid = TwoPlayersPartnerOf(guid);
    if (!partnerGuid)
        return;
    KeepTheTwoPlayersBody(player);
    bool const left = TwoPlayersIsLeftHead(guid);
    bool const leaving = player->GetSession() && player->GetSession()->isLogingOut();
    if (!left && TakeExitRequest(guid))
        Tell(player, TwoPlayersHalt(player, !Halted(guid)));
    Player* const connected = Connected(partnerGuid);
    if (!left)
        WaitForBody(player, !connected, TwoPlayersHeadIsBot(connected));
    if (!left && connected && TwoPlayersHeadIsBot(player))
        TwoPlayersKeepBotRiderStill(player);
    if (!leaving)
        CallBotForMissingHead(player, partnerGuid, !connected, left);
    Player* partner = Online(partnerGuid);
    if (!left)
        RevealArrival(player, partner != nullptr);
    if (!partner)
    {
        Greet(left ? guid : partnerGuid, false);
        ForgetTold(guid);
        if (left)
            ShowTwoAgain(player);
        Steered(guid, false);
        if (HasLent(player))
            Reclaim(player);
        return;
    }
    Player* body = left ? player : partner;
    Player* rider = left ? partner : player;
    if (!left && leaving && TwoPlayersHeadIsBot(body))
    {
        Steered(guid, false);
        TwoPlayersSteerBotBody(body, false);
    }
    else if (!left && Steered(guid, TwoPlayersHeadIsBot(body)))
        TwoPlayersSteerBotBody(body, !Halted(guid));
    else if (!left && TwoPlayersHeadIsBot(body) && !Halted(guid))
        TwoPlayersKeepBotBodyRoaming(body);
    if (left && SameMap(body, rider) && Ready(body) && Ready(rider))
        GroupUp(body, rider);
    if (Seated(body, rider))
    {
        if (left)
        {
            LendBothWays(body, rider);
            UseLentAbilities(body, body);
            ShowOneBodyToGroup(body, rider);
        }
        else
            UseLentAbilities(rider, body);
        return;
    }
    if (left)
        ShowTwoAgain(body);
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
    rider->ApplyEnchantment(worn.Copy, false);
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
        made->SetSlot(slot);
        rider->ApplyEnchantment(made, true);
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

bool SharesTheBodysGear(Player const* rider)
{
    return RidesHidden(rider);
}

void FollowTheBody(Player* rider)
{
    uint32 const guid = rider->GetGUID().GetCounter();
    uint32 const partnerGuid = TwoPlayersPartnerOf(guid);
    Player* body = partnerGuid && !TwoPlayersIsLeftHead(guid) ? Online(partnerGuid) : nullptr;
    bool const seated = Seated(body, rider);
    bool const shared = seated && SharesTheBodysGear(rider);
    if (shared)
        LeaveOwnGearBehind(rider);
    RedirectThreatTo(rider, seated ? body : nullptr);
    WearTheBodysGear(rider, shared ? body : nullptr);
}

bool WearsNoGearOfItsOwn(Player const* player)
{
    uint32 const guid = player->GetGUID().GetCounter();
    return TwoPlayersPartnerOf(guid) && !TwoPlayersIsLeftHead(guid) && SharesTheBodysGear(player);
}

class mod_subclass_two_players_player : public PlayerScript
{
public:
    mod_subclass_two_players_player() : PlayerScript("mod_subclass_two_players_player", { PLAYERHOOK_ON_UPDATE,
        PLAYERHOOK_ON_PLAYER_JUST_DIED, PLAYERHOOK_ON_LOGOUT, PLAYERHOOK_CAN_GROUP_ACCEPT, PLAYERHOOK_CAN_EQUIP_ITEM,
        PLAYERHOOK_CAN_UNEQUIP_ITEM, PLAYERHOOK_ON_LOAD_FROM_DB, PLAYERHOOK_ON_BEFORE_LOGOUT })
    {
    }

    void OnPlayerLoadFromDB(Player* player) override
    {
        uint32 const guid = player->GetGUID().GetCounter();
        uint32 const partnerGuid = TwoPlayersPartnerOf(guid);
        if (partnerGuid && !TwoPlayersIsLeftHead(guid) && Online(partnerGuid))
            HideArrival(player);
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
        KeepTwoPlayersBrother(player);
        HideRiderApart(player);
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
            return true;
        Player* left = Online(partnerGuid);
        if (!left || left->GetGroup() == group)
            return true;
        Tell(player, "Your Melee Head handles the group: ask them to invite or join, and you come along.");
        return false;
    }

    void OnPlayerBeforeLogout(Player* player) override
    {
        HideSeatedHeads(player);
    }

    static void HideSeatedHeads(Player* player)
    {
        Player* seated = TwoPlayersSeatedPartner(player);
        for (Player* head : { player, seated })
            if (head && head->GetVehicle() && (TwoPlayersHeadIsBot(head) || RidesHidden(head)))
                head->SetVisible(false);
    }

    void OnPlayerLogout(Player* player) override
    {
        HideSeatedHeads(player);
        ShowTwoAgain(player);
        RedirectThreatTo(player, nullptr);
        WearTheBodysGear(player, nullptr);
        Reclaim(player);
        DropBodyKit(player);
        std::lock_guard<std::mutex> lock(bodyMutex);
        sinceCheck.erase(player->GetGUID().GetCounter());
        waiting.erase(player->GetGUID().GetCounter());
        halted.erase(player->GetGUID().GetCounter());
        absences.erase(player->GetGUID().GetCounter());
        toldLent.erase(player->GetGUID().GetCounter());
        steered.erase(player->GetGUID().GetCounter());
        groupToldAt.erase(player->GetGUID().GetCounter());
        toldHanded.erase(player->GetGUID().GetCounter());
        hiddenArrivals.erase(player->GetGUID().GetCounter());
    }
};

Player* VisibleRiderOf(Unit* unit)
{
    Player* body = unit ? unit->ToPlayer() : nullptr;
    if (!body || Subclass::BodyVehicleFor(body->getRace(true)) != Subclass::TwinsTwoPlayersVehicle)
        return nullptr;
    uint32 const guid = body->GetGUID().GetCounter();
    if (!TwoPlayersIsLeftHead(guid))
        return nullptr;
    Player* rider = Online(TwoPlayersPartnerOf(guid));
    return Seated(body, rider) ? rider : nullptr;
}

bool HealsManyAtOnce(SpellInfo const* info)
{
    if (info->IsAffectingArea())
        return true;
    for (SpellEffectInfo const& effect : info->Effects)
        if (effect.ChainTarget > 1)
            return true;
    return false;
}

Player* BodyOfSeatedRider(Unit* unit)
{
    Player* rider = unit ? unit->ToPlayer() : nullptr;
    if (!rider || !rider->GetVehicle())
        return nullptr;
    uint32 const guid = rider->GetGUID().GetCounter();
    if (TwoPlayersIsLeftHead(guid))
        return nullptr;
    Player* body = Online(TwoPlayersPartnerOf(guid));
    return Seated(body, rider) ? body : nullptr;
}

void DismountForRider(Unit* rider, SpellInfo const* info)
{
    Player* body = BodyOfSeatedRider(rider);
    if (!body || !body->IsMounted() || info->HasAttribute(SPELL_ATTR0_ALLOW_WHILE_MOUNTED) ||
        (info->IsPositive() && !rider->IsInCombat()))
        return;
    body->Dismount();
    body->RemoveAurasByType(SPELL_AURA_MOUNTED);
}

class mod_subclass_two_players_unit : public UnitScript
{
public:
    mod_subclass_two_players_unit() : UnitScript("mod_subclass_two_players_unit", true,
        { UNITHOOK_ON_DAMAGE, UNITHOOK_MODIFY_MELEE_DAMAGE, UNITHOOK_MODIFY_HEAL_RECEIVED }) { }

    void ModifyHealReceived(Unit* healer, Unit* receiver, uint32& heal, SpellInfo const* info) override
    {
        if (!healer || !heal || !info || HealsManyAtOnce(info))
            return;
        Player* body = BodyOfSeatedRider(receiver);
        if (!body || !body->IsAlive())
            return;
        HealInfo bodyHeal(healer, body, heal, info, info->GetSchoolMask());
        heal = 0;
        healer->HealBySpell(bodyHeal);
    }

    void ModifyMeleeDamage(Unit* /*target*/, Unit* attacker, uint32& /*damage*/) override
    {
        Player* rider = VisibleRiderOf(attacker);
        if (!rider)
            return;
        bool const armed = attacker->ToPlayer()->GetWeaponForAttack(BASE_ATTACK, true) != nullptr;
        rider->HandleEmoteCommand(armed ? EMOTE_ONESHOT_ATTACK1H : EMOTE_ONESHOT_ATTACK_UNARMED);
    }

    void OnDamage(Unit* /*attacker*/, Unit* victim, uint32& damage) override
    {
        if (BodyOfSeatedRider(victim))
            damage = 0;
    }
};

uint32 LentShare(Player const* presser, Player const* owner, SpellInfo const* info)
{
    Powers const ownerPower = Powers(info->PowerType);
    int32 const ownerCost = std::max(0, info->CalcPowerCost(owner, info->GetSchoolMask()));
    return Subclass::ShareOfOwnPower(uint32(ownerCost), owner->GetMaxPower(ownerPower),
        presser->GetMaxPower(presser->getPowerType()));
}

void PayLentShare(Unit* caster, SpellInfo const* info)
{
    Player* presser = caster ? caster->ToPlayer() : nullptr;
    if (!presser || !LentTo(presser, info->Id))
        return;
    Player* owner = TwoPlayersSeatedPartner(presser);
    if (!owner)
        return;
    if (uint32 const share = LentShare(presser, owner, info))
        presser->ModifyPower(presser->getPowerType(), -int32(share));
}

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
    mod_subclass_two_players_spells() : AllSpellScript("mod_subclass_two_players_spells",
        { ALLSPELLHOOK_CAN_PREPARE, ALLSPELLHOOK_ON_CAST })
    {
    }

    void OnSpellCast(Spell* spell, Unit* caster, SpellInfo const* info, bool /*skipCheck*/) override
    {
        if (spell->IsTriggered() || info->IsPassive())
            return;
        PayLentShare(caster, info);
        DismountForRider(caster, info);
        if (Player* rider = VisibleRiderOf(caster))
            rider->HandleEmoteCommand(info->IsPositive() ? EMOTE_ONESHOT_SPELL_CAST_OMNI : EMOTE_ONESHOT_SPELL_CAST);
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
        if (kind != Subclass::Borrow::None && !Subclass::BodyCasts(kind, pressedByLeft) &&
            ClassAbilityOf(presser, info->Id))
        {
            if (!TwoPlayersHeadIsBot(presser))
                Tell(presser, RefusalFor(kind));
            return false;
        }
        if (!pressedByLeft)
            spell->AddTriggeredCastFlag(TRIGGERED_IGNORE_EQUIPPED_ITEM_REQUIREMENT);
        if (!LentTo(presser, info->Id))
            return true;
        if (presser->GetPower(presser->getPowerType()) < LentShare(presser, other, info))
        {
            Spell::SendCastResult(presser, info, 0, SPELL_FAILED_NO_POWER);
            return false;
        }
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

bool TwoPlayersPressLent(Player* player, uint32 spellId)
{
    if (!LentTo(player, spellId))
        return false;
    SpellInfo const* info = sSpellMgr->GetSpellInfo(spellId);
    if (!info || !player->IsAlive() || !player->HasSpell(spellId))
        return true;
    Unit* selected = player->GetSelectedUnit();
    Unit* target = player;
    if (info->NeedsExplicitUnitTarget())
    {
        if (info->IsPositive())
            target = selected && !player->IsValidAttackTarget(selected) ? selected : player;
        else
            target = selected && player->IsValidAttackTarget(selected) ? selected : nullptr;
    }
    if (!target)
    {
        ChatHandler(player->GetSession()).SendNotification("That needs an enemy target.");
        return true;
    }
    player->CastSpell(target, spellId, false);
    return true;
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

bool TwoPlayersAskStopOrGo(uint32 guid)
{
    uint32 const partnerGuid = TwoPlayersPartnerOf(guid);
    if (!partnerGuid || TwoPlayersIsLeftHead(guid))
        return false;
    std::lock_guard<std::mutex> lock(bodyMutex);
    exitRequests.insert(guid);
    return true;
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

ObjectGuid BodyOfTwoPlayersRider(ObjectGuid rider)
{
    if (!rider.IsPlayer())
        return ObjectGuid::Empty;
    uint32 const guid = rider.GetCounter();
    uint32 const partner = TwoPlayersPartnerOf(guid);
    if (!partner || TwoPlayersIsLeftHead(guid))
        return ObjectGuid::Empty;
    return ObjectGuid::Create<HighGuid::Player>(partner);
}

void AddSC_mod_subclass_two_players()
{
    SetBodyOfRider(&BodyOfTwoPlayersRider);
    new mod_subclass_two_players_player();
    new mod_subclass_two_players_unit();
    new mod_subclass_two_players_spells();
}
