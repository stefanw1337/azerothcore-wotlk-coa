/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license.
 */

#include "AscensionCoATalentData.h"
#include "AscensionSpecialization.h"
#include "Chat.h"
#include "CommandScript.h"
#include "Config.h"
#include "DatabaseEnv.h"
#include "Log.h"
#include "Player.h"
#include "PlayerScript.h"
#include "SubclassRules.h"
#include "WorldScript.h"
#include <algorithm>
#include <mutex>
#include <unordered_map>

using namespace Acore::ChatCommands;

namespace
{
std::mutex subclassMutex;
Subclass::Settings settings;
std::vector<Subclass::Pair> pairs;
std::unordered_map<uint32, Subclass::Choice> choices;

Subclass::Character CharacterOf(Player const* player)
{
    return { player->getRace(), player->getGender(), player->getClass(), player->GetLevel() };
}

bool IsSpecializationOf(uint8 classId, uint16 specId)
{
    return specId && std::any_of(AscensionCompatData::CoASpecializations.begin(),
        AscensionCompatData::CoASpecializations.end(), [classId, specId](auto const& specialization)
        {
            return specialization.ClassId == classId && specialization.SpecId == specId;
        });
}

void LoadPairs()
{
    std::vector<Subclass::Pair> loaded;
    if (QueryResult result = WorldDatabase.Query(
            "SELECT `race`, `gender_mask`, `main_class`, `sub_class`, `sub_spec` FROM `mod_subclass_pair` "
            "WHERE `enabled` = 1"))
    {
        do
        {
            Field* fields = result->Fetch();
            loaded.push_back({ fields[0].Get<uint8>(), fields[1].Get<uint8>(), fields[2].Get<uint8>(),
                fields[3].Get<uint8>(), fields[4].Get<uint16>() });
        } while (result->NextRow());
    }

    std::lock_guard<std::mutex> lock(subclassMutex);
    pairs = std::move(loaded);
    LOG_INFO("module", "mod-subclass: loaded {} allowed subclass pair(s)", pairs.size());
}

Subclass::Choice ChoiceOf(Player const* player)
{
    uint32 const guid = player->GetGUID().GetCounter();
    {
        std::lock_guard<std::mutex> lock(subclassMutex);
        if (auto itr = choices.find(guid); itr != choices.end())
            return itr->second;
    }

    Subclass::Choice choice;
    if (QueryResult result = CharacterDatabase.Query(
            "SELECT `sub_class`, `sub_spec` FROM `mod_subclass_character` WHERE `guid` = {}", guid))
    {
        Field* fields = result->Fetch();
        choice = { fields[0].Get<uint8>(), fields[1].Get<uint16>() };
    }

    std::lock_guard<std::mutex> lock(subclassMutex);
    return choices.try_emplace(guid, choice).first->second;
}

std::vector<Subclass::Tree> OpenTreesOf(Player const* player)
{
    if (!IsAscensionCustomClassId(player->getClass()))
        return {};
    Subclass::Choice const choice = ChoiceOf(player);
    std::lock_guard<std::mutex> lock(subclassMutex);
    return Subclass::OpenTrees(settings, pairs, CharacterOf(player), choice);
}

uint32 ForgetBorrowedTrees(Player* player, Subclass::Choice const& choice)
{
    uint32 removed = 0;
    std::vector<Subclass::Tree> const trees = Subclass::BorrowedTrees(choice);
    for (AscensionCompatData::CoATalentEntry const& entry : AscensionCompatData::CoATalentEntries)
    {
        Subclass::Tree const tree = { uint8(entry.ClassId), uint16(entry.SpecId) };
        if (std::find(trees.begin(), trees.end(), tree) == trees.end())
            continue;
        for (uint32 spellId : entry.SpellIds)
            if (spellId && player->HasSpell(spellId))
            {
                player->removeSpell(spellId, SPEC_MASK_ALL, false);
                ++removed;
            }
    }
    return removed;
}

void StoreChoice(Player const* player, Subclass::Choice const& choice)
{
    uint32 const guid = player->GetGUID().GetCounter();
    if (choice.Empty())
        CharacterDatabase.Execute("DELETE FROM `mod_subclass_character` WHERE `guid` = {}", guid);
    else
        CharacterDatabase.Execute(
            "REPLACE INTO `mod_subclass_character` (`guid`, `sub_class`, `sub_spec`, `chosen_at`) "
            "VALUES ({}, {}, {}, UNIX_TIMESTAMP())", guid, uint32(choice.SubClass), choice.SubSpec);

    std::lock_guard<std::mutex> lock(subclassMutex);
    choices[guid] = choice;
}

uint32 ApplyChoice(Player* player, Subclass::Choice const& previous, Subclass::Choice const& wanted)
{
    uint32 const removed = previous == wanted ? 0 : ForgetBorrowedTrees(player, previous);
    StoreChoice(player, wanted);
    if (uint32 const active = GetAscensionActiveSpecialization(player))
        SwitchAscensionSpecialization(player, active);
    return removed;
}

std::string Describe(Subclass::Choice const& choice)
{
    if (choice.Empty())
        return "none";
    return Acore::StringFormat("class {} (class tree and specialization {})", uint32(choice.SubClass),
        choice.SubSpec);
}
}

class mod_subclass_world : public WorldScript
{
public:
    mod_subclass_world() : WorldScript("mod_subclass_world", { WORLDHOOK_ON_AFTER_CONFIG_LOAD, WORLDHOOK_ON_STARTUP })
    {
    }

    void OnAfterConfigLoad(bool /*reload*/) override
    {
        Subclass::Settings loaded;
        loaded.Enabled = sConfigMgr->GetOption<bool>("Subclass.Enable", true);
        loaded.MinLevel = uint8(std::min<uint32>(sConfigMgr->GetOption<uint32>("Subclass.MinLevel", 20), 255));
        loaded.BorrowedPointsPercent = sConfigMgr->GetOption<uint32>("Subclass.BorrowedPointsPercent", 100);
        std::lock_guard<std::mutex> lock(subclassMutex);
        settings = loaded;
    }

    void OnStartup() override
    {
        LoadPairs();
    }
};

class mod_subclass_player : public PlayerScript
{
public:
    mod_subclass_player() : PlayerScript("mod_subclass_player",
        { PLAYERHOOK_ON_LOGIN, PLAYERHOOK_ON_LOGOUT, PLAYERHOOK_ON_DELETE })
    {
    }

    void OnPlayerLogin(Player* player) override
    {
        Subclass::Choice const choice = ChoiceOf(player);
        if (choice.Empty())
            return;

        Subclass::Character const character = CharacterOf(player);
        Subclass::Choice kept;
        if (IsAscensionCustomClassId(character.Class) && IsSpecializationOf(choice.SubClass, choice.SubSpec))
        {
            std::lock_guard<std::mutex> lock(subclassMutex);
            kept = Subclass::Trimmed(pairs, character, choice);
        }
        if (kept == choice)
            return;

        uint32 const removed = ApplyChoice(player, choice, kept);
        ChatHandler(player->GetSession()).PSendSysMessage(
            "Your character no longer qualifies for its subclass; {} talent spell(s) were removed.", removed);
        LOG_INFO("module", "mod-subclass: {} no longer qualifies ({}), removed {} spell(s)", player->GetName(),
            Describe(choice), removed);
    }

    void OnPlayerLogout(Player* player) override
    {
        std::lock_guard<std::mutex> lock(subclassMutex);
        choices.erase(player->GetGUID().GetCounter());
    }

    void OnPlayerDelete(ObjectGuid guid, uint32 /*accountId*/) override
    {
        CharacterDatabase.Execute("DELETE FROM `mod_subclass_character` WHERE `guid` = {}", guid.GetCounter());
        std::lock_guard<std::mutex> lock(subclassMutex);
        choices.erase(guid.GetCounter());
    }
};

class mod_subclass_commands : public CommandScript
{
public:
    mod_subclass_commands() : CommandScript("mod_subclass_commands") { }

    ChatCommandTable GetCommands() const override
    {
        static ChatCommandTable subclassCommands =
        {
            { "info", HandleInfo, SEC_GAMEMASTER, Console::No },
            { "specs", HandleSpecs, SEC_GAMEMASTER, Console::Yes },
            { "set", HandleSet, SEC_GAMEMASTER, Console::No },
            { "clear", HandleClear, SEC_GAMEMASTER, Console::No },
            { "spec", HandleSpec, SEC_GAMEMASTER, Console::No },
            { "learn", HandleLearn, SEC_GAMEMASTER, Console::No },
            { "reload", HandleReload, SEC_ADMINISTRATOR, Console::Yes }
        };
        static ChatCommandTable commands = { { "subclass", subclassCommands } };
        return commands;
    }

    static Player* Target(ChatHandler* handler)
    {
        Player* target = handler->getSelectedPlayerOrSelf();
        if (!target)
            handler->SendErrorMessage("Select a player.");
        return target;
    }

    static bool HandleInfo(ChatHandler* handler)
    {
        Player* target = Target(handler);
        if (!target)
            return false;

        Subclass::Character const character = CharacterOf(target);
        Subclass::Choice const choice = ChoiceOf(target);
        bool eligible = false;
        {
            std::lock_guard<std::mutex> lock(subclassMutex);
            eligible = Subclass::IsEligible(pairs, character);
        }
        bool const open = !OpenTreesOf(target).empty();
        handler->PSendSysMessage("{}: race {}, gender {}, class {}, level {}, active specialization {}.",
            target->GetName(), uint32(character.Race), uint32(character.Gender), uint32(character.Class),
            uint32(character.Level), GetAscensionActiveSpecialization(target));
        handler->PSendSysMessage("Eligible: {}. Choice: {}. Borrowed trees open: {}.", eligible ? "yes" : "no",
            Describe(choice), open ? "yes" : "no");
        return true;
    }

    static bool HandleSpecs(ChatHandler* handler, uint8 classId)
    {
        std::string list;
        for (auto const& specialization : AscensionCompatData::CoASpecializations)
            if (specialization.ClassId == classId)
                list += Acore::StringFormat(" {}", specialization.SpecId);
        if (list.empty())
        {
            handler->SendErrorMessage("Class {} has no CoA specializations.", uint32(classId));
            return false;
        }
        handler->PSendSysMessage("Class {} specializations:{}", uint32(classId), list);
        return true;
    }

    static bool HandleSet(ChatHandler* handler, uint8 subClass, uint16 subSpec)
    {
        Player* target = Target(handler);
        if (!target)
            return false;

        Subclass::Character const character = CharacterOf(target);
        Subclass::Choice const wanted = { subClass, subSpec };
        if (!wanted.Empty())
        {
            if (!IsAscensionCustomClassId(character.Class))
            {
                handler->SendErrorMessage("Only CoA classes can have a subclass.");
                return false;
            }
            if (!IsSpecializationOf(subClass, subSpec))
            {
                handler->SendErrorMessage("Specialization {} does not belong to class {}.", subSpec,
                    uint32(subClass));
                return false;
            }
            std::lock_guard<std::mutex> lock(subclassMutex);
            if (!Subclass::AllowsSubclass(pairs, character, subClass, subSpec))
            {
                handler->SendErrorMessage("This character may not borrow that class and specialization.");
                return false;
            }
            if (character.Level < settings.MinLevel)
            {
                handler->SendErrorMessage("A subclass requires level {}.", uint32(settings.MinLevel));
                return false;
            }
        }

        uint32 const removed = ApplyChoice(target, ChoiceOf(target), wanted);
        handler->PSendSysMessage("{}: subclass set to {} ({} old borrowed talent spell(s) removed). Relog to "
            "refresh the talent window.", target->GetName(), Describe(wanted), removed);
        return true;
    }

    static bool HandleClear(ChatHandler* handler)
    {
        return HandleSet(handler, 0, 0);
    }

    static bool HandleSpec(ChatHandler* handler, uint16 specId)
    {
        Player* target = Target(handler);
        if (!target)
            return false;
        if (!SwitchAscensionSpecialization(target, specId))
        {
            handler->SendErrorMessage("{} cannot switch to specialization {}.", target->GetName(), specId);
            return false;
        }
        handler->PSendSysMessage("{} is now in specialization {}.", target->GetName(), specId);
        return true;
    }

    static bool HandleLearn(ChatHandler* handler, uint32 entryId, uint8 rank)
    {
        Player* target = Target(handler);
        if (!target)
            return false;
        if (!SetAscensionTalentRank(target, entryId, rank))
        {
            handler->SendErrorMessage("Talent entry {} rank {} cannot be set for {}.", entryId, uint32(rank),
                target->GetName());
            return false;
        }
        handler->PSendSysMessage("Set talent entry {} to rank {} for {}.", entryId, uint32(rank), target->GetName());
        return true;
    }

    static bool HandleReload(ChatHandler* handler)
    {
        LoadPairs();
        handler->SendSysMessage("Reloaded mod_subclass_pair.");
        return true;
    }
};

void AddSC_mod_subclass()
{
    new mod_subclass_world();
    new mod_subclass_player();
    new mod_subclass_commands();

    AddAscensionTalentTreeProvider([](Player const* player)
    {
        AscensionCoATalentState::TalentGrant grant;
        for (Subclass::Tree const& tree : OpenTreesOf(player))
            grant.ExtraTrees.push_back({ tree.ClassId, tree.SpecId });
        return grant;
    });
    AddAscensionTalentBudgetProvider([](Player const* player, uint32& classPoints, uint32& specializationPoints)
    {
        if (OpenTreesOf(player).empty())
            return;
        std::lock_guard<std::mutex> lock(subclassMutex);
        classPoints += Subclass::BonusPoints(settings, classPoints);
        specializationPoints += Subclass::BonusPoints(settings, specializationPoints);
    });
}
