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
#include "Item.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
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
    uint32 Inviter = 0;
    bool InviterIsLeft = true;
    uint64 Until = 0;
};

std::mutex& pairMutex = *new std::mutex;
std::unordered_map<uint32, uint32>& partners = *new std::unordered_map<uint32, uint32>;
std::unordered_map<uint32, bool>& leftHeads = *new std::unordered_map<uint32, bool>;
std::unordered_map<uint32, PairInvite>& invites = *new std::unordered_map<uint32, PairInvite>;
std::unordered_map<uint32, std::string>& standInBodies = *new std::unordered_map<uint32, std::string>;

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

std::string StandInBodyOf(uint32 guid)
{
    std::lock_guard<std::mutex> lock(pairMutex);
    auto const found = standInBodies.find(guid);
    return found == standInBodies.end() ? "" : found->second;
}

std::string NameOf(uint32 guid)
{
    std::string name;
    if (!sCharacterCache->GetCharacterNameByGuid(ObjectGuid::Create<HighGuid::Player>(guid), name))
        return "someone";
    return name;
}

bool IsTwoPlayers(Player const* player)
{
    return Subclass::IsTwoPlayersBody(player->getRace(true), player->GetByteValue(PLAYER_BYTES_3, 0));
}

Subclass::PairHead HeadOf(Player const* player)
{
    return { IsTwoPlayers(player),
        PartnerOf(player->GetGUID().GetCounter()) != 0, player->GetLevel(),
        RolesOfSpecialization(uint16(GetAscensionActiveSpecialization(player))) };
}

std::string RefusalText(Subclass::PairRefusal refusal, Player const* left, Player const* right)
{
    switch (refusal)
    {
        case Subclass::PairRefusal::LeftNotTwoPlayers:
            return left->GetName() + " is not a Two-Players Two-Headed Ogre.";
        case Subclass::PairRefusal::RightNotTwoPlayers:
            return right->GetName() + " is not a Two-Players Two-Headed Ogre.";
        case Subclass::PairRefusal::LeftPaired:
            return left->GetName() + " is already part of a pair.";
        case Subclass::PairRefusal::RightPaired:
            return right->GetName() + " is already part of a pair.";
        case Subclass::PairRefusal::LevelGap:
            return "The two heads must be within " + std::to_string(Subclass::MaxPairLevelGap) + " levels.";
        case Subclass::PairRefusal::LeftSpecialization:
            return left->GetName() + " needs a melee or tank specialization to be the Left Head.";
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
    {
        std::string const formerLeft = StandInBodyOf(guid);
        if (!formerLeft.empty())
            return "PAIRSTATE ECHO " + formerLeft;
        Subclass::SpecializationRoles const roles = HeadOf(player).Roles;
        return std::string("PAIRSTATE NONE ") + (Subclass::FitsLeftHead(roles) ? "LEFT"
            : Subclass::FitsRightHead(roles) ? "RIGHT" : "ANY");
    }
    return std::string("PAIRSTATE ") + (IsLeftHead(guid) ? "LEFT " : "RIGHT ") + NameOf(partner);
}

void Invite(Player* inviter, Player* invitee, bool inviterIsLeft)
{
    if (inviter == invitee)
    {
        Tell(inviter, "You can't pair with yourself.");
        return;
    }
    Player* left = inviterIsLeft ? inviter : invitee;
    Player* right = inviterIsLeft ? invitee : inviter;
    Subclass::PairRefusal const refusal = Subclass::CheckPair(HeadOf(left), HeadOf(right));
    if (refusal != Subclass::PairRefusal::None)
    {
        Tell(inviter, RefusalText(refusal, left, right));
        return;
    }
    {
        std::lock_guard<std::mutex> lock(pairMutex);
        invites[invitee->GetGUID().GetCounter()] = { inviter->GetGUID().GetCounter(), inviterIsLeft,
            uint64(GameTime::GetGameTimeMS().count()) + InviteMilliseconds };
    }
    std::string const seat = inviterIsLeft ? "Right Head" : "Left Head";
    Tell(inviter, "You invited " + invitee->GetName() + " to be your " + seat + ".");
    Tell(invitee, inviter->GetName() + " invites you to be the " + seat + " of their body. Type .pair accept or "
        ".pair decline.");
    SendPairAddon(invitee, (inviterIsLeft ? "PAIRINVITE " : "PAIRINVITELEFT ") + inviter->GetName());
}

void ForgetStandInBody(uint32 right)
{
    {
        std::lock_guard<std::mutex> lock(pairMutex);
        if (!standInBodies.erase(right))
            return;
    }
    CharacterDatabase.DirectExecute("DELETE FROM `mod_subclass_two_players_echo` WHERE `right_guid` = {}", right);
    CharacterDatabase.DirectExecute("DELETE FROM `mod_subclass_two_players_stand_in` WHERE `right_guid` = {}", right);
}

std::string EchoSlotList()
{
    std::string slots;
    for (std::uint8_t slot = 0; slot < Subclass::EquipmentSlotCount; ++slot)
        if (Subclass::IsEchoSlot(slot))
            slots += (slots.empty() ? "" : ",") + std::to_string(slot);
    return slots;
}

void TakeEcho(uint32 left, uint32 right)
{
    std::string formerLeft = NameOf(left);
    std::string escaped = formerLeft;
    CharacterDatabase.EscapeString(escaped);
    CharacterDatabase.DirectExecute("DELETE FROM `mod_subclass_two_players_echo` WHERE `right_guid` = {}", right);
    CharacterDatabase.DirectExecute("REPLACE INTO `mod_subclass_two_players_stand_in` "
        "(`right_guid`, `former_left_guid`, `former_left_name`) VALUES ({}, {}, '{}')", right, left, escaped);
    CharacterDatabase.DirectExecute("INSERT INTO `mod_subclass_two_players_echo` (`right_guid`, `slot`, `item_entry`, "
        "`enchantments`, `random_property_id`) SELECT {}, ci.`slot`, ii.`itemEntry`, ii.`enchantments`, "
        "ii.`randomPropertyId` FROM `character_inventory` ci JOIN `item_instance` ii ON ii.`guid` = ci.`item` "
        "WHERE ci.`guid` = {} AND ci.`bag` = 0 AND ci.`slot` IN ({})", right, left, EchoSlotList());
    std::lock_guard<std::mutex> lock(pairMutex);
    standInBodies[right] = formerLeft;
}

void Accept(Player* invitee)
{
    uint32 const inviteeGuid = invitee->GetGUID().GetCounter();
    PairInvite invite;
    {
        std::lock_guard<std::mutex> lock(pairMutex);
        auto const found = invites.find(inviteeGuid);
        if (found != invites.end())
        {
            invite = found->second;
            invites.erase(found);
        }
    }
    if (!invite.Inviter || invite.Until < uint64(GameTime::GetGameTimeMS().count()))
    {
        Tell(invitee, "You have no pair invitation waiting.");
        return;
    }
    Player* inviter = ObjectAccessor::FindPlayerByLowGUID(invite.Inviter);
    if (!inviter)
    {
        Tell(invitee, "The head that invited you is no longer online.");
        return;
    }
    Player* left = invite.InviterIsLeft ? inviter : invitee;
    Player* right = invite.InviterIsLeft ? invitee : inviter;
    Subclass::PairRefusal const refusal = Subclass::CheckPair(HeadOf(left), HeadOf(right));
    if (refusal != Subclass::PairRefusal::None)
    {
        Tell(invitee, RefusalText(refusal, left, right));
        return;
    }
    uint32 const leftGuid = left->GetGUID().GetCounter();
    uint32 const rightGuid = right->GetGUID().GetCounter();
    CharacterDatabase.DirectExecute(
        "INSERT INTO `mod_subclass_two_players` (`left_guid`, `right_guid`) VALUES ({}, {})", leftGuid, rightGuid);
    {
        std::lock_guard<std::mutex> lock(pairMutex);
        partners[leftGuid] = rightGuid;
        partners[rightGuid] = leftGuid;
        leftHeads[leftGuid] = true;
        leftHeads[rightGuid] = false;
    }
    bool const hadStandInBody = !StandInBodyOf(rightGuid).empty();
    ForgetStandInBody(rightGuid);
    Tell(left, right->GetName() + " is now your Right Head.");
    Tell(right, "You are now " + left->GetName() + "'s Right Head.");
    if (hadStandInBody)
        Tell(right, "The stand-in body and its echo gear are gone. You ride on " + left->GetName() + " now.");
    SendPairAddon(left, StateOf(left));
    SendPairAddon(right, StateOf(right));
}

void Decline(Player* invitee)
{
    PairInvite invite;
    {
        std::lock_guard<std::mutex> lock(pairMutex);
        auto const found = invites.find(invitee->GetGUID().GetCounter());
        if (found == invites.end())
        {
            Tell(invitee, "You have no pair invitation waiting.");
            return;
        }
        invite = found->second;
        invites.erase(found);
    }
    Tell(invitee, "You declined the invitation.");
    if (Player* inviter = ObjectAccessor::FindPlayerByLowGUID(invite.Inviter))
        Tell(inviter, invitee->GetName() + " declined to be your " + (invite.InviterIsLeft ? "Right" : "Left")
            + " Head.");
}

void Status(Player* player)
{
    uint32 const guid = player->GetGUID().GetCounter();
    uint32 const partner = PartnerOf(guid);
    std::string const formerLeft = StandInBodyOf(guid);
    if (!partner && !formerLeft.empty())
        Tell(player, "You ride a stand-in body wearing the echo of " + formerLeft + "'s gear. Type .pair echo to see "
            "it, or find a new Left Head.");
    else if (!partner)
        Tell(player, "You are not paired with another head.");
    else if (IsLeftHead(guid))
        Tell(player, "You are the Left Head. Your Right Head is " + NameOf(partner) + ".");
    else
        Tell(player, "You are the Right Head. Your Left Head is " + NameOf(partner) + ".");
    SendPairAddon(player, StateOf(player));
}

void ShowEcho(Player* player)
{
    std::string const formerLeft = StandInBodyOf(player->GetGUID().GetCounter());
    if (formerLeft.empty())
    {
        Tell(player, "You have no echo gear.");
        return;
    }
    Tell(player, "Echo of " + formerLeft + "'s gear:");
    QueryResult result = CharacterDatabase.Query("SELECT `item_entry` FROM `mod_subclass_two_players_echo` "
        "WHERE `right_guid` = {} ORDER BY `slot`", player->GetGUID().GetCounter());
    if (!result)
    {
        Tell(player, "  nothing; the body was bare.");
        return;
    }
    do
    {
        ItemTemplate const* item = sObjectMgr->GetItemTemplate(result->Fetch()[0].Get<uint32>());
        Tell(player, "  " + (item ? item->Name1 : std::string("unknown item")));
    } while (result->NextRow());
}

void Forget(uint32 guid)
{
    uint32 partner = 0;
    bool wasLeft = false;
    {
        std::lock_guard<std::mutex> lock(pairMutex);
        auto const found = partners.find(guid);
        if (found == partners.end())
            return;
        partner = found->second;
        wasLeft = leftHeads[guid];
        partners.erase(found);
        partners.erase(partner);
        leftHeads.erase(guid);
        leftHeads.erase(partner);
    }
    CharacterDatabase.DirectExecute(
        "DELETE FROM `mod_subclass_two_players` WHERE `left_guid` = {} OR `right_guid` = {}", guid, guid);
    if (Player* other = ObjectAccessor::FindPlayerByLowGUID(partner))
    {
        if (wasLeft)
            Tell(other, "Your Left Head is gone. The echo of their gear is kept for your stand-in body.");
        else
            Tell(other, "The other head of your body is gone. The seat is free for a new partner.");
        SendPairAddon(other, StateOf(other));
    }
}

void Delete(uint32 guid)
{
    uint32 const partner = PartnerOf(guid);
    if (partner && IsLeftHead(guid))
        TakeEcho(guid, partner);
    ForgetStandInBody(guid);
    Forget(guid);
}

void LoadPairs()
{
    CharacterDatabase.DirectExecute("CREATE TABLE IF NOT EXISTS `mod_subclass_two_players` ("
        "`left_guid` INT UNSIGNED NOT NULL, `right_guid` INT UNSIGNED NOT NULL, "
        "`paired_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP, PRIMARY KEY (`left_guid`), "
        "UNIQUE KEY `right_guid` (`right_guid`)) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci");
    CharacterDatabase.DirectExecute("CREATE TABLE IF NOT EXISTS `mod_subclass_two_players_stand_in` ("
        "`right_guid` INT UNSIGNED NOT NULL, `former_left_guid` INT UNSIGNED NOT NULL, "
        "`former_left_name` VARCHAR(12) NOT NULL, `since` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP, "
        "PRIMARY KEY (`right_guid`)) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci");
    CharacterDatabase.DirectExecute("CREATE TABLE IF NOT EXISTS `mod_subclass_two_players_echo` ("
        "`right_guid` INT UNSIGNED NOT NULL, `slot` TINYINT UNSIGNED NOT NULL, `item_entry` INT UNSIGNED NOT NULL, "
        "`enchantments` TEXT NOT NULL, `random_property_id` SMALLINT NOT NULL DEFAULT 0, "
        "PRIMARY KEY (`right_guid`, `slot`)) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci");
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
    std::unordered_map<uint32, std::string> bodies;
    QueryResult standIns = CharacterDatabase.Query(
        "SELECT `right_guid`, `former_left_name` FROM `mod_subclass_two_players_stand_in`");
    if (standIns)
        do
        {
            Field* fields = standIns->Fetch();
            bodies[fields[0].Get<uint32>()] = fields[1].Get<std::string>();
        } while (standIns->NextRow());
    std::lock_guard<std::mutex> lock(pairMutex);
    partners = std::move(loaded);
    leftHeads = std::move(heads);
    standInBodies = std::move(bodies);
}

void FitHoldingBody(Player* player)
{
    if (!IsTwoPlayers(player))
        return;
    Item const* offHand = player->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_OFFHAND);
    bool const holding = offHand && offHand->GetTemplate()->InventoryType == Subclass::HeldInOffHand;
    uint32 const body = player->GetNativeDisplayId();
    uint32 const wanted = Subclass::BodyDisplayFor(body, holding);
    if (wanted == body)
        return;
    bool const showingBody = player->GetDisplayId() == body;
    player->SetNativeDisplayId(wanted);
    if (showingBody)
        player->SetDisplayId(wanted);
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
            { "inviteleft", HandleInviteLeft, SEC_PLAYER, Console::No },
            { "echo", HandleEcho, SEC_PLAYER, Console::No },
            { "accept", HandleAccept, SEC_PLAYER, Console::No },
            { "decline", HandleDecline, SEC_PLAYER, Console::No },
            { "status", HandleStatus, SEC_PLAYER, Console::No }
        };
        static ChatCommandTable commands = { { "pair", pairCommands } };
        return commands;
    }

    static bool InviteFrom(ChatHandler* handler, Optional<std::string> const& name, bool asLeft)
    {
        Player* player = handler->GetSession()->GetPlayer();
        Player* target = name ? NamedPlayer(handler, *name) : handler->getSelectedPlayer();
        if (!target)
        {
            if (!name)
                handler->SendSysMessage(asLeft ? "Target a player or name one: .pair invite Name"
                    : "Target a player or name one: .pair inviteleft Name");
            return true;
        }
        Invite(player, target, asLeft);
        return true;
    }

    static bool HandleInvite(ChatHandler* handler, Optional<std::string> name)
    {
        return InviteFrom(handler, name, true);
    }

    static bool HandleInviteLeft(ChatHandler* handler, Optional<std::string> name)
    {
        return InviteFrom(handler, name, false);
    }

    static bool HandleEcho(ChatHandler* handler)
    {
        ShowEcho(handler->GetSession()->GetPlayer());
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
        PLAYERHOOK_ON_DELETE, PLAYERHOOK_ON_AFTER_SET_VISIBLE_ITEM_SLOT })
    {
    }

    void OnPlayerAfterSetVisibleItemSlot(Player* player, uint8 slot, Item* /*item*/) override
    {
        if (slot == EQUIPMENT_SLOT_OFFHAND)
            FitHoldingBody(player);
    }

    void OnPlayerLogin(Player* player) override
    {
        FitHoldingBody(player);
        SendPairAddon(player, StateOf(player));
        if (!PartnerOf(player->GetGUID().GetCounter()) && !StandInBodyOf(player->GetGUID().GetCounter()).empty())
            Status(player);
    }

    void OnPlayerDelete(ObjectGuid guid, uint32 /*accountId*/) override
    {
        Delete(guid.GetCounter());
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
    if (words.size() == 2 && (words[0] == "PAIR" || words[0] == "PAIRLEFT"))
    {
        std::string name(words[1]);
        if (!normalizePlayerName(name))
            return;
        if (Player* target = ObjectAccessor::FindPlayerByName(name, false))
            Invite(player, target, words[0] == "PAIR");
        else
            Tell(player, name + " is not online.");
    }
    else if (words.size() == 1 && words[0] == "PAIRACCEPT")
        Accept(player);
    else if (words.size() == 1 && words[0] == "PAIRDECLINE")
        Decline(player);
    else if (words.size() == 1 && words[0] == "PAIRSTATUS")
        Status(player);
    else if (words.size() == 1 && words[0] == "PAIRECHO")
        ShowEcho(player);
}

void AddSC_mod_subclass_pairs()
{
    new mod_subclass_pairs_commands();
    new mod_subclass_pairs_player();
    new mod_subclass_pairs_world();
}
