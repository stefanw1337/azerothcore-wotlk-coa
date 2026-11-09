/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license.
 */

#include "AscensionSpecialization.h"
#include "CharacterCache.h"
#include "Chat.h"
#include "ChatCommand.h"
#include "CommandScript.h"
#include "DatabaseEnv.h"
#include "GameTime.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "PlayerScript.h"
#include "SubclassRules.h"
#include "WorldPacket.h"
#include "WorldScript.h"
#include "WorldSession.h"
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

using namespace Acore::ChatCommands;

Subclass::SpecializationRoles RolesOfSpecialization(uint16 specialization);

namespace
{
constexpr char const* PairAddonPrefix = "SUBCLASS";
constexpr uint64 InviteMilliseconds = 120000;

struct PairInvite
{
    uint32 Left = 0;
    uint64 Until = 0;
};

std::mutex& pairMutex = *new std::mutex;
std::unordered_map<uint32, uint32>& partners = *new std::unordered_map<uint32, uint32>;
std::unordered_map<uint32, bool>& leftHeads = *new std::unordered_map<uint32, bool>;
std::unordered_map<uint32, PairInvite>& invites = *new std::unordered_map<uint32, PairInvite>;

void SendPairAddon(Player* player, std::string const& message)
{
    WorldPacket packet;
    ChatHandler::BuildChatPacket(packet, CHAT_MSG_WHISPER, LANG_ADDON, player, player,
        std::string(PairAddonPrefix) + "\t" + message);
    player->SendDirectMessage(&packet);
}

void Tell(Player* player, std::string const& text)
{
    ChatHandler(player->GetSession()).SendSysMessage(text);
}

uint32 PartnerOf(uint32 guid)
{
    std::lock_guard<std::mutex> lock(pairMutex);
    auto const found = partners.find(guid);
    return found == partners.end() ? 0 : found->second;
}

bool IsLeftHead(uint32 guid)
{
    std::lock_guard<std::mutex> lock(pairMutex);
    auto const found = leftHeads.find(guid);
    return found != leftHeads.end() && found->second;
}

std::string NameOf(uint32 guid)
{
    std::string name;
    if (!sCharacterCache->GetCharacterNameByGuid(ObjectGuid::Create<HighGuid::Player>(guid), name))
        return "someone";
    return name;
}

Subclass::PairHead HeadOf(Player const* player)
{
    return { Subclass::IsTwoPlayersBody(player->getRace(), player->getGender()),
        PartnerOf(player->GetGUID().GetCounter()) != 0, player->GetLevel(),
        RolesOfSpecialization(uint16(GetAscensionActiveSpecialization(player))) };
}

std::string RefusalText(Subclass::PairRefusal refusal, Player const* right)
{
    switch (refusal)
    {
        case Subclass::PairRefusal::LeftNotTwoPlayers:
            return "Only a Two-Players Two-Headed Ogre can invite a Right Head.";
        case Subclass::PairRefusal::RightNotTwoPlayers:
            return right->GetName() + " is not a Two-Players Two-Headed Ogre.";
        case Subclass::PairRefusal::LeftPaired:
            return "You already have a Right Head.";
        case Subclass::PairRefusal::RightPaired:
            return right->GetName() + " is already part of a pair.";
        case Subclass::PairRefusal::LevelGap:
            return "The two heads must be within " + std::to_string(Subclass::MaxPairLevelGap) + " levels.";
        case Subclass::PairRefusal::LeftSpecialization:
            return "The Left Head needs a melee or tank specialization.";
        case Subclass::PairRefusal::RightSpecialization:
            return right->GetName() + " needs a caster or ranged specialization to be the Right Head.";
        case Subclass::PairRefusal::None:
            break;
    }
    return "";
}

std::string StateOf(Player const* player)
{
    uint32 const guid = player->GetGUID().GetCounter();
    uint32 const partner = PartnerOf(guid);
    if (!partner)
        return "PAIRSTATE NONE";
    return std::string("PAIRSTATE ") + (IsLeftHead(guid) ? "LEFT " : "RIGHT ") + NameOf(partner);
}

void Invite(Player* left, Player* right)
{
    if (left == right)
    {
        Tell(left, "You can't pair with yourself.");
        return;
    }
    Subclass::PairRefusal const refusal = Subclass::CheckPair(HeadOf(left), HeadOf(right));
    if (refusal != Subclass::PairRefusal::None)
    {
        Tell(left, RefusalText(refusal, right));
        return;
    }
    {
        std::lock_guard<std::mutex> lock(pairMutex);
        invites[right->GetGUID().GetCounter()] = { left->GetGUID().GetCounter(),
            uint64(GameTime::GetGameTimeMS().count()) + InviteMilliseconds };
    }
    Tell(left, "You invited " + right->GetName() + " to be your Right Head.");
    Tell(right, left->GetName() + " invites you to be the Right Head of their body. Type .pair accept or "
        ".pair decline.");
    SendPairAddon(right, "PAIRINVITE " + left->GetName());
}

void Accept(Player* right)
{
    uint32 const rightGuid = right->GetGUID().GetCounter();
    PairInvite invite;
    {
        std::lock_guard<std::mutex> lock(pairMutex);
        auto const found = invites.find(rightGuid);
        if (found != invites.end())
        {
            invite = found->second;
            invites.erase(found);
        }
    }
    if (!invite.Left || invite.Until < uint64(GameTime::GetGameTimeMS().count()))
    {
        Tell(right, "You have no pair invitation waiting.");
        return;
    }
    Player* left = ObjectAccessor::FindPlayerByLowGUID(invite.Left);
    if (!left)
    {
        Tell(right, "The head that invited you is no longer online.");
        return;
    }
    Subclass::PairRefusal const refusal = Subclass::CheckPair(HeadOf(left), HeadOf(right));
    if (refusal != Subclass::PairRefusal::None)
    {
        Tell(right, RefusalText(refusal, right));
        return;
    }
    uint32 const leftGuid = left->GetGUID().GetCounter();
    CharacterDatabase.DirectExecute(
        "INSERT INTO `mod_subclass_two_players` (`left_guid`, `right_guid`) VALUES ({}, {})", leftGuid, rightGuid);
    {
        std::lock_guard<std::mutex> lock(pairMutex);
        partners[leftGuid] = rightGuid;
        partners[rightGuid] = leftGuid;
        leftHeads[leftGuid] = true;
        leftHeads[rightGuid] = false;
    }
    Tell(left, right->GetName() + " is now your Right Head.");
    Tell(right, "You are now " + left->GetName() + "'s Right Head.");
    SendPairAddon(left, StateOf(left));
    SendPairAddon(right, StateOf(right));
}

void Decline(Player* right)
{
    PairInvite invite;
    {
        std::lock_guard<std::mutex> lock(pairMutex);
        auto const found = invites.find(right->GetGUID().GetCounter());
        if (found == invites.end())
        {
            Tell(right, "You have no pair invitation waiting.");
            return;
        }
        invite = found->second;
        invites.erase(found);
    }
    Tell(right, "You declined the invitation.");
    if (Player* left = ObjectAccessor::FindPlayerByLowGUID(invite.Left))
        Tell(left, right->GetName() + " declined to be your Right Head.");
}

void Status(Player* player)
{
    uint32 const guid = player->GetGUID().GetCounter();
    uint32 const partner = PartnerOf(guid);
    if (!partner)
        Tell(player, "You are not paired with another head.");
    else if (IsLeftHead(guid))
        Tell(player, "You are the Left Head. Your Right Head is " + NameOf(partner) + ".");
    else
        Tell(player, "You are the Right Head. Your Left Head is " + NameOf(partner) + ".");
    SendPairAddon(player, StateOf(player));
}

void Forget(uint32 guid)
{
    uint32 partner = 0;
    {
        std::lock_guard<std::mutex> lock(pairMutex);
        auto const found = partners.find(guid);
        if (found == partners.end())
            return;
        partner = found->second;
        partners.erase(found);
        partners.erase(partner);
        leftHeads.erase(guid);
        leftHeads.erase(partner);
    }
    CharacterDatabase.DirectExecute(
        "DELETE FROM `mod_subclass_two_players` WHERE `left_guid` = {} OR `right_guid` = {}", guid, guid);
    if (Player* other = ObjectAccessor::FindPlayerByLowGUID(partner))
    {
        Tell(other, "The other head of your body is gone. The seat is free for a new partner.");
        SendPairAddon(other, StateOf(other));
    }
}

void LoadPairs()
{
    CharacterDatabase.DirectExecute("CREATE TABLE IF NOT EXISTS `mod_subclass_two_players` ("
        "`left_guid` INT UNSIGNED NOT NULL, `right_guid` INT UNSIGNED NOT NULL, "
        "`paired_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP, PRIMARY KEY (`left_guid`), "
        "UNIQUE KEY `right_guid` (`right_guid`)) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci");
    std::unordered_map<uint32, uint32> loaded;
    std::unordered_map<uint32, bool> heads;
    QueryResult result = CharacterDatabase.Query("SELECT `left_guid`, `right_guid` FROM `mod_subclass_two_players`");
    if (result)
        do
        {
            Field* fields = result->Fetch();
            uint32 const left = fields[0].Get<uint32>();
            uint32 const right = fields[1].Get<uint32>();
            loaded[left] = right;
            loaded[right] = left;
            heads[left] = true;
            heads[right] = false;
        } while (result->NextRow());
    std::lock_guard<std::mutex> lock(pairMutex);
    partners = std::move(loaded);
    leftHeads = std::move(heads);
}

Player* NamedPlayer(ChatHandler* handler, std::string_view name)
{
    std::string wanted(name);
    if (!normalizePlayerName(wanted))
        return nullptr;
    Player* target = ObjectAccessor::FindPlayerByName(wanted, false);
    if (!target)
        handler->SendSysMessage(wanted + " is not online.");
    return target;
}

class mod_subclass_pairs_commands : public CommandScript
{
public:
    mod_subclass_pairs_commands() : CommandScript("mod_subclass_pairs_commands") { }

    ChatCommandTable GetCommands() const override
    {
        static ChatCommandTable pairCommands =
        {
            { "invite", HandleInvite, SEC_PLAYER, Console::No },
            { "accept", HandleAccept, SEC_PLAYER, Console::No },
            { "decline", HandleDecline, SEC_PLAYER, Console::No },
            { "status", HandleStatus, SEC_PLAYER, Console::No }
        };
        static ChatCommandTable commands = { { "pair", pairCommands } };
        return commands;
    }

    static bool HandleInvite(ChatHandler* handler, Optional<std::string> name)
    {
        Player* player = handler->GetSession()->GetPlayer();
        Player* target = name ? NamedPlayer(handler, *name) : handler->getSelectedPlayer();
        if (!target)
        {
            if (!name)
                handler->SendSysMessage("Target a player or name one: .pair invite Name");
            return true;
        }
        Invite(player, target);
        return true;
    }

    static bool HandleAccept(ChatHandler* handler)
    {
        Accept(handler->GetSession()->GetPlayer());
        return true;
    }

    static bool HandleDecline(ChatHandler* handler)
    {
        Decline(handler->GetSession()->GetPlayer());
        return true;
    }

    static bool HandleStatus(ChatHandler* handler)
    {
        Status(handler->GetSession()->GetPlayer());
        return true;
    }
};

class mod_subclass_pairs_player : public PlayerScript
{
public:
    mod_subclass_pairs_player() : PlayerScript("mod_subclass_pairs_player", { PLAYERHOOK_ON_LOGIN,
        PLAYERHOOK_ON_DELETE })
    {
    }

    void OnPlayerLogin(Player* player) override
    {
        SendPairAddon(player, StateOf(player));
    }

    void OnPlayerDelete(ObjectGuid guid, uint32 /*accountId*/) override
    {
        Forget(guid.GetCounter());
    }
};

class mod_subclass_pairs_world : public WorldScript
{
public:
    mod_subclass_pairs_world() : WorldScript("mod_subclass_pairs_world", { WORLDHOOK_ON_STARTUP }) { }

    void OnStartup() override
    {
        LoadPairs();
    }
};
}

void HandlePairMessage(Player* player, std::vector<std::string_view> const& words)
{
    if (words.size() == 2 && words[0] == "PAIR")
    {
        std::string name(words[1]);
        if (!normalizePlayerName(name))
            return;
        if (Player* target = ObjectAccessor::FindPlayerByName(name, false))
            Invite(player, target);
        else
            Tell(player, name + " is not online.");
    }
    else if (words.size() == 1 && words[0] == "PAIRACCEPT")
        Accept(player);
    else if (words.size() == 1 && words[0] == "PAIRDECLINE")
        Decline(player);
    else if (words.size() == 1 && words[0] == "PAIRSTATUS")
        Status(player);
}

void AddSC_mod_subclass_pairs()
{
    new mod_subclass_pairs_commands();
    new mod_subclass_pairs_player();
    new mod_subclass_pairs_world();
}
