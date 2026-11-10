/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license.
 */

#include "AscensionSpecialization.h"
#include "ObjectMgr.h"
#include "Opcodes.h"
#include "Player.h"
#include "PlayerScript.h"
#include "ServerScript.h"
#include "SubclassRules.h"
#include "Timer.h"
#include "WorldPacket.h"
#include "WorldSession.h"
#include <map>
#include <mutex>
#include <tuple>
#include <unordered_map>

uint32 TwoPlayersPartnerOf(uint32 guid);
bool TwoPlayersAskStopOrGo(uint32 guid);
bool TwoPlayersIsLeftHead(uint32 guid);
Player* TwoPlayersSeatedPartner(Player const* head);
Subclass::SpecializationRoles RolesOfSpecialization(uint16 specialization);

namespace
{
constexpr uint32 PoolTickMs = 500;
constexpr uint32 QuestRewardWindowMs = 100;

using RollKey = std::tuple<uint32, uint64, uint32>;

std::mutex& shareMutex = *new std::mutex;
std::unordered_map<uint32, uint32>& questGoldPending = *new std::unordered_map<uint32, uint32>;
std::map<RollKey, uint32>& rollItems = *new std::map<RollKey, uint32>;
std::unordered_map<uint32, uint32>& sincePool = *new std::unordered_map<uint32, uint32>;

bool RangedHead(Player const* player)
{
    uint32 const guid = player->GetGUID().GetCounter();
    return TwoPlayersPartnerOf(guid) && !TwoPlayersIsLeftHead(guid);
}

std::string HeadSpecRefusal(Player* player, uint32 /*active*/, uint32 requested)
{
    uint32 const guid = player->GetGUID().GetCounter();
    if (!TwoPlayersPartnerOf(guid))
        return "";
    Subclass::SpecializationRoles const roles = RolesOfSpecialization(uint16(requested));
    if (!Subclass::HasAnyRole(roles))
        return "";
    if (TwoPlayersIsLeftHead(guid))
        return Subclass::FitsLeftHead(roles) ? "" :
            "The Melee Head takes melee or tank specializations only, while you share a body.";
    return Subclass::FitsRightHead(roles) ? "" :
        "The Ranged Head takes caster, ranged or healer specializations only, while you share a body.";
}

void SharePool(Player* body, Player* rider)
{
    if (body->getPowerType() != rider->getPowerType() || !body->IsAlive() || !rider->IsAlive())
        return;
    Powers const power = body->getPowerType();
    auto const [first, second] = Subclass::SharedPool(body->GetPower(power), body->GetMaxPower(power),
        rider->GetPower(power), rider->GetMaxPower(power));
    if (body->GetPower(power) != first)
        body->SetPower(power, first);
    if (rider->GetPower(power) != second)
        rider->SetPower(power, second);
}

class mod_subclass_two_players_share_player : public PlayerScript
{
public:
    mod_subclass_two_players_share_player() : PlayerScript("mod_subclass_two_players_share_player",
        { PLAYERHOOK_ON_QUEST_COMPUTE_EXP, PLAYERHOOK_ON_MONEY_CHANGED, PLAYERHOOK_ON_PLAYER_COMPLETE_QUEST,
          PLAYERHOOK_ON_UPDATE, PLAYERHOOK_ON_LOGOUT })
    {
    }

    void OnPlayerQuestComputeXP(Player* player, Quest const* /*quest*/, uint32& /*xpValue*/) override
    {
        if (!TwoPlayersPartnerOf(player->GetGUID().GetCounter()))
            return;
        std::lock_guard<std::mutex> lock(shareMutex);
        questGoldPending[player->GetGUID().GetCounter()] = getMSTime();
    }

    void OnPlayerMoneyChanged(Player* player, int32& amount) override
    {
        uint32 const guid = player->GetGUID().GetCounter();
        {
            std::lock_guard<std::mutex> lock(shareMutex);
            auto const found = questGoldPending.find(guid);
            if (amount <= 0 || found == questGoldPending.end())
                return;
            bool const fresh = getMSTimeDiff(found->second, getMSTime()) <= QuestRewardWindowMs;
            questGoldPending.erase(found);
            if (!fresh)
                return;
        }
        amount = Subclass::QuestGoldShare(amount, TwoPlayersIsLeftHead(guid));
    }

    void OnPlayerCompleteQuest(Player* player, Quest const* /*quest*/) override
    {
        std::lock_guard<std::mutex> lock(shareMutex);
        questGoldPending.erase(player->GetGUID().GetCounter());
    }

    void OnPlayerUpdate(Player* player, uint32 diff) override
    {
        uint32 const guid = player->GetGUID().GetCounter();
        {
            std::lock_guard<std::mutex> lock(shareMutex);
            uint32& since = sincePool[guid];
            since += diff;
            if (since < PoolTickMs)
                return;
            since = 0;
        }
        if (!TwoPlayersPartnerOf(guid) || !TwoPlayersIsLeftHead(guid))
            return;
        if (Player* rider = TwoPlayersSeatedPartner(player))
            SharePool(player, rider);
    }

    void OnPlayerLogout(Player* player) override
    {
        uint32 const guid = player->GetGUID().GetCounter();
        std::lock_guard<std::mutex> lock(shareMutex);
        questGoldPending.erase(guid);
        sincePool.erase(guid);
        for (auto it = rollItems.begin(); it != rollItems.end();)
            it = std::get<0>(it->first) == guid ? rollItems.erase(it) : std::next(it);
    }
};

class mod_subclass_two_players_share_server : public ServerScript
{
public:
    mod_subclass_two_players_share_server() : ServerScript("mod_subclass_two_players_share_server",
        { SERVERHOOK_CAN_PACKET_SEND, SERVERHOOK_CAN_PACKET_RECEIVE })
    {
    }

    using ServerScript::CanPacketReceive;

    bool CanPacketSend(WorldSession* session, WorldPacket const& packet) override
    {
        if (packet.GetOpcode() != SMSG_LOOT_START_ROLL || !session || !session->GetPlayer() ||
            !RangedHead(session->GetPlayer()))
            return true;
        WorldPacket copy(packet);
        copy.rpos(0);
        uint64 rollGuid = 0;
        uint32 mapId = 0;
        uint32 slot = 0;
        uint32 itemId = 0;
        copy >> rollGuid >> mapId >> slot >> itemId;
        ItemTemplate const* proto = sObjectMgr->GetItemTemplate(itemId);
        if (!proto || Subclass::RangedHeadMayNeed(proto->Class))
            return true;
        {
            std::lock_guard<std::mutex> lock(shareMutex);
            rollItems[{ session->GetPlayer()->GetGUID().GetCounter(), rollGuid, slot }] = itemId;
        }
        std::size_t const maskAt = packet.size() - 1;
        uint8 const mask = packet.read<uint8>(maskAt);
        if (!(mask & ROLL_FLAG_TYPE_NEED))
            return true;
        copy.put<uint8>(maskAt, mask & ~ROLL_FLAG_TYPE_NEED);
        session->SendPacket(&copy);
        return false;
    }

    bool CanPacketReceive(WorldSession* session, WorldPacket& packet) override
    {
        if (packet.GetOpcode() == CMSG_REQUEST_VEHICLE_EXIT && session && session->GetPlayer())
            return !TwoPlayersAskStopOrGo(session->GetPlayer()->GetGUID().GetCounter());
        if (packet.GetOpcode() != CMSG_LOOT_ROLL || !session || !session->GetPlayer() ||
            !RangedHead(session->GetPlayer()) || packet.size() < 13)
            return true;
        uint64 const rollGuid = packet.read<uint64>(0);
        uint32 const slot = packet.read<uint32>(8);
        if (packet.read<uint8>(12) != ROLL_NEED)
            return true;
        std::lock_guard<std::mutex> lock(shareMutex);
        if (rollItems.count({ session->GetPlayer()->GetGUID().GetCounter(), rollGuid, slot }))
            packet.put<uint8>(12, ROLL_GREED);
        return true;
    }
};
}

void AddSC_mod_subclass_two_players_share()
{
    AddAscensionSpecializationSwitchGuard(HeadSpecRefusal);
    new mod_subclass_two_players_share_player();
    new mod_subclass_two_players_share_server();
}
