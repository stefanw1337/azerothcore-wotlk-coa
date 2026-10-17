/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license.
 */

#include "AllCreatureScript.h"
#include "AscensionCoATalentData.h"
#include "AscensionSpecialization.h"
#include "Chat.h"
#include "CommandScript.h"
#include "Config.h"
#include "DatabaseEnv.h"
#include "ItemScript.h"
#include "Log.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "PlayerScript.h"
#include "Random.h"
#include "ScriptedGossip.h"
#include "SubclassRules.h"
#include "UnitScript.h"
#include "WorldScript.h"
#include <algorithm>
#include <mutex>
#include <unordered_map>

using namespace Acore::ChatCommands;

namespace
{
constexpr uint32 PromptMenuId = 9530010;
constexpr uint32 PromptTextId = 9530010;
constexpr uint32 ClassTextId = 9530011;
constexpr uint32 SpecTextId = 9530012;
constexpr uint32 SenderPrompt = 9530010;
constexpr uint32 SenderTrainer = 9530011;
constexpr uint32 SenderClass = 9530012;
constexpr uint32 SenderSpec = 9530013;
constexpr uint32 ActionAccept = 1;
constexpr uint32 ActionLater = 2;
constexpr char const* BookScriptName = "item_subclass_book_of_origins";
constexpr char const* Head = "|cffc9a0dcYour second head:|r ";

struct NamedClass
{
    uint8 Id;
    char const* Name;
    char const* Color;
};

constexpr NamedClass Classes[] =
{
    { 23, "Necromancer", "|cff45db9c" },
    { 24, "Pyromancer", "|cffff6112" },
    { 25, "Cultist", "|cff9c45f2" },
    { 26, "Starcaller", "|cff8fffff" },
    { 27, "Sun Cleric", "|cffffb240" },
    { 28, "Tinker", "|cffd9d9d9" },
    { 32, "Runemaster", "|cff40c7eb" },
    { 31, "Primalist", "|cffe38c59" },
    { 30, "Reaper", "|cff0a876b" },
    { 29, "Venomancer", "|cff6ba600" },
    { 22, "Chronomancer", "|cffffed4a" },
    { 20, "Bloodmage", "|cffa30000" },
    { 18, "Guardian", "|cff9c9482" },
    { 16, "Stormbringer", "|cff007ded" },
    { 14, "Felsworn", "|cff75fa00" },
    { 12, "Barbarian", "|cff8a3303" },
    { 13, "Witch Doctor", "|cfff500ff" },
    { 15, "Witch Hunter", "|cff5433cf" },
    { 17, "Knight of Xoroth", "|cfffc0005" },
    { 19, "Templar", "|cfffffab2" },
    { 21, "Ranger", "|cffbff06b" }
};

struct NamedSpecialization
{
    uint16 Id;
    char const* Name;
};

constexpr NamedSpecialization SpecializationNames[] =
{
    { 1, "Headhunting" },
    { 2, "Brutality" },
    { 3, "Ancestry" },
    { 4, "Shadowhunting" },
    { 5, "Voodoo" },
    { 6, "Brewing" },
    { 7, "Infernal" },
    { 8, "Slayer" },
    { 9, "Tyrant" },
    { 10, "Boltslinger" },
    { 11, "Houndmaster" },
    { 12, "Inquisition" },
    { 13, "Wind" },
    { 14, "Maelstrom" },
    { 15, "Lightning" },
    { 16, "Hellfire" },
    { 17, "Defiance" },
    { 18, "War" },
    { 19, "Gladiator" },
    { 20, "Inspiration" },
    { 21, "Vanguard" },
    { 22, "Oathkeeper" },
    { 23, "Zealot" },
    { 24, "Crusader" },
    { 25, "Fleshweaver" },
    { 26, "Sanguine" },
    { 27, "Accursed" },
    { 28, "Archery" },
    { 29, "Farstrider" },
    { 30, "Brigand" },
    { 31, "Time" },
    { 32, "Infinite" },
    { 33, "Artificer" },
    { 34, "Death" },
    { 35, "Animation" },
    { 36, "Rime" },
    { 37, "Flameweaving" },
    { 38, "Incineration" },
    { 39, "Draconic" },
    { 40, "Heretic" },
    { 41, "Corruption" },
    { 42, "Godblade" },
    { 43, "Moon Priest" },
    { 44, "Sentinel" },
    { 45, "Warden" },
    { 46, "Piety" },
    { 47, "Valkyrie" },
    { 48, "Seraphim" },
    { 49, "Demolition" },
    { 50, "Mechanics" },
    { 51, "Invention" },
    { 52, "Fortitude" },
    { 53, "Stalking" },
    { 54, "Rot" },
    { 55, "Soul" },
    { 56, "Harvest" },
    { 57, "Domination" },
    { 58, "Grovekeeper" },
    { 59, "Wildwalker" },
    { 60, "Mountain King" },
    { 61, "Engravement" },
    { 62, "Glyphic" },
    { 63, "Riftblade" },
    { 95, "Geomancy" },
    { 96, "Dreadnought" },
    { 97, "Black Knight" },
    { 98, "Blessings" },
    { 99, "Eternal" },
    { 100, "Moon Guard" },
    { 101, "Vizier" },
};

std::string ClassName(uint8 classId)
{
    for (NamedClass const& named : Classes)
        if (named.Id == classId)
            return Acore::StringFormat("{}{}|r", named.Color, named.Name);
    return Acore::StringFormat("class {}", uint32(classId));
}

std::string SpecializationName(uint16 specId)
{
    for (NamedSpecialization const& named : SpecializationNames)
        if (named.Id == specId)
            return named.Name;
    return Acore::StringFormat("specialization {}", specId);
}

struct Record
{
    Subclass::Choice Choice;
    uint32 Insights = 0;
};

std::mutex subclassMutex;
Subclass::Settings settings;
std::vector<Subclass::Pair> pairs;
std::unordered_map<uint32, Record> records;

Subclass::Settings CurrentSettings()
{
    std::lock_guard<std::mutex> lock(subclassMutex);
    return settings;
}

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

uint8 ClassOfSpecialization(uint16 specId)
{
    for (auto const& specialization : AscensionCompatData::CoASpecializations)
        if (specialization.SpecId == specId)
            return specialization.ClassId;
    return 0;
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

Record RecordOf(Player const* player)
{
    uint32 const guid = player->GetGUID().GetCounter();
    {
        std::lock_guard<std::mutex> lock(subclassMutex);
        if (auto itr = records.find(guid); itr != records.end())
            return itr->second;
    }

    Record record;
    if (QueryResult result = CharacterDatabase.Query(
            "SELECT `sub_class`, `sub_spec`, `tokens` FROM `mod_subclass_character` WHERE `guid` = {}", guid))
    {
        Field* fields = result->Fetch();
        record.Choice = { fields[0].Get<uint8>(), fields[1].Get<uint16>() };
        record.Insights = fields[2].Get<uint32>();
    }

    std::lock_guard<std::mutex> lock(subclassMutex);
    return records.try_emplace(guid, record).first->second;
}

Subclass::Choice ChoiceOf(Player const* player)
{
    return RecordOf(player).Choice;
}

void StoreRecord(Player const* player, Record const& record)
{
    uint32 const guid = player->GetGUID().GetCounter();
    if (record.Choice.Empty() && !record.Insights)
        CharacterDatabase.Execute("DELETE FROM `mod_subclass_character` WHERE `guid` = {}", guid);
    else
        CharacterDatabase.Execute(
            "REPLACE INTO `mod_subclass_character` (`guid`, `sub_class`, `sub_spec`, `tokens`, `chosen_at`) "
            "VALUES ({}, {}, {}, {}, UNIX_TIMESTAMP())", guid, uint32(record.Choice.SubClass), record.Choice.SubSpec,
            std::min<uint32>(record.Insights, 255));

    std::lock_guard<std::mutex> lock(subclassMutex);
    records[guid] = record;
}

bool IsEligible(Player const* player)
{
    if (!IsAscensionCustomClassId(player->getClass()))
        return false;
    std::lock_guard<std::mutex> lock(subclassMutex);
    return Subclass::IsEligible(pairs, CharacterOf(player));
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

uint32 ApplyChoice(Player* player, Subclass::Choice const& wanted)
{
    Record record = RecordOf(player);
    uint32 const removed = record.Choice == wanted ? 0 : ForgetBorrowedTrees(player, record.Choice);
    record.Choice = wanted;
    StoreRecord(player, record);
    if (uint32 const active = GetAscensionActiveSpecialization(player))
        SwitchAscensionSpecialization(player, active);
    return removed;
}

void AddInsight(Player* player)
{
    Record record = RecordOf(player);
    ++record.Insights;
    StoreRecord(player, record);
}

std::string Describe(Subclass::Choice const& choice)
{
    if (choice.Empty())
        return "none";
    return Acore::StringFormat("{} (class tree and {})", ClassName(choice.SubClass),
        SpecializationName(choice.SubSpec));
}

bool OffersQuest(Player const* player)
{
    if (!IsAscensionCustomClassId(player->getClass()))
        return false;
    Record const record = RecordOf(player);
    Subclass::Settings const current = CurrentSettings();
    bool const untouched = player->GetQuestStatus(current.QuestId) == QUEST_STATUS_NONE;
    std::lock_guard<std::mutex> lock(subclassMutex);
    return Subclass::OffersQuest(current, pairs, CharacterOf(player), record.Choice, record.Insights, untouched);
}

void ShowPrompt(Player* player)
{
    ClearGossipMenuFor(player);
    AddGossipItemFor(player, GOSSIP_ICON_CHAT, "Fine. We go to Shadowfang Keep and get this book.", SenderPrompt,
        ActionAccept);
    AddGossipItemFor(player, GOSSIP_ICON_CHAT, "Not now. Stop talking in my ear.", SenderPrompt, ActionLater);
    player->PlayerTalkClass->GetGossipMenu().SetMenuId(PromptMenuId);
    SendGossipMenuFor(player, PromptTextId, player->GetGUID());
}

void SchedulePrompt(Player* player, Milliseconds delay)
{
    ObjectGuid const guid = player->GetGUID();
    player->m_Events.AddEventAtOffset([player, guid]()
    {
        if (player->IsInWorld() && player->GetGUID() == guid && OffersQuest(player))
            ShowPrompt(player);
    }, delay);
}

void AcceptQuest(Player* player)
{
    CloseGossipMenuFor(player);
    Quest const* quest = sObjectMgr->GetQuestTemplate(CurrentSettings().QuestId);
    if (!quest || !OffersQuest(player) || !player->CanTakeQuest(quest, true) || !player->CanAddQuest(quest, true))
        return;
    player->AddQuestAndCheckCompletion(quest, nullptr);
    ChatHandler(player->GetSession()).PSendSysMessage("{}Book. Shadowfang Keep. The mad mage has it. Go!", Head);
}

void ShowClasses(Player* player, Creature* trainer)
{
    ClearGossipMenuFor(player);
    Subclass::Character const character = CharacterOf(player);
    {
        std::lock_guard<std::mutex> lock(subclassMutex);
        for (NamedClass const& named : Classes)
            if (Subclass::AllowsAnySpecializationOf(pairs, character, named.Id))
                AddGossipItemFor(player, GOSSIP_ICON_TRAINER, ClassName(named.Id), SenderClass, named.Id);
    }
    SendGossipMenuFor(player, ClassTextId, trainer->GetGUID());
}

void ShowSpecializations(Player* player, Creature* trainer, uint8 classId)
{
    ClearGossipMenuFor(player);
    Subclass::Character const character = CharacterOf(player);
    {
        std::lock_guard<std::mutex> lock(subclassMutex);
        for (auto const& specialization : AscensionCompatData::CoASpecializations)
            if (specialization.ClassId == classId &&
                Subclass::AllowsSubclass(pairs, character, classId, specialization.SpecId))
            {
                std::string const text = Acore::StringFormat("{}: {}", ClassName(classId),
                    SpecializationName(specialization.SpecId));
                std::string const confirm = Acore::StringFormat(
                    "Spend one insight of the Book of Origins on {}? Talents from an earlier choice are removed.",
                    SpecializationName(specialization.SpecId));
                player->PlayerTalkClass->GetGossipMenu().AddMenuItem(-1, GOSSIP_ICON_TRAINER, text, SenderSpec,
                    specialization.SpecId, confirm, 0);
            }
    }
    SendGossipMenuFor(player, SpecTextId, trainer->GetGUID());
}

void ChooseAtTrainer(Player* player, uint16 specId)
{
    CloseGossipMenuFor(player);
    ChatHandler handler(player->GetSession());
    uint8 const classId = ClassOfSpecialization(specId);
    Record const record = RecordOf(player);
    Subclass::Choice const wanted = { classId, specId };
    bool allowed = false;
    {
        std::lock_guard<std::mutex> lock(subclassMutex);
        allowed = Subclass::AllowsSubclass(pairs, CharacterOf(player), classId, specId) &&
            player->GetLevel() >= settings.MinLevel;
    }
    if (!record.Insights || !allowed || !IsSpecializationOf(classId, specId))
    {
        handler.SendSysMessage("Your heads cannot agree on that.");
        return;
    }

    uint32 const removed = ApplyChoice(player, wanted);
    Record spent = RecordOf(player);
    spent.Insights = record.Insights - 1;
    StoreRecord(player, spent);
    handler.PSendSysMessage("{}Ha! Now we know the ways of the {}. {} talent spell(s) of the old way were forgotten. "
        "Log out and back in to see it in your talents.", Head, Describe(wanted), removed);
    LOG_INFO("module", "mod-subclass: {} chose {} at a trainer", player->GetName(), Describe(wanted));
}

void GrantBook(Player* player, uint32 bookItemId, Creature const* boss)
{
    if (!player->AddItem(bookItemId, 1))
        return;
    ChatHandler(player->GetSession()).PSendSysMessage(
        "{}Look! In the remains of {}, another Book of Origins! We can choose again.", Head, boss->GetName());
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
        loaded.QuestId = sConfigMgr->GetOption<uint32>("Subclass.QuestId", 9530010);
        loaded.BookItemId = sConfigMgr->GetOption<uint32>("Subclass.BookItemId", 9530010);
        loaded.BookDropChance = sConfigMgr->GetOption<float>("Subclass.BookDropChance", 5.0f);
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
    mod_subclass_player() : PlayerScript("mod_subclass_player", { PLAYERHOOK_ON_LOGIN, PLAYERHOOK_ON_LOGOUT,
        PLAYERHOOK_ON_DELETE, PLAYERHOOK_ON_LEVEL_CHANGED, PLAYERHOOK_ON_GOSSIP_SELECT })
    {
    }

    void OnPlayerLogin(Player* player) override
    {
        TrimChoice(player);
        SchedulePrompt(player, 5s);
    }

    void OnPlayerLevelChanged(Player* player, uint8 oldLevel) override
    {
        if (oldLevel < CurrentSettings().MinLevel)
            SchedulePrompt(player, 1s);
    }

    void OnPlayerGossipSelect(Player* player, uint32 menuId, uint32 sender, uint32 action) override
    {
        if (menuId != PromptMenuId || sender != SenderPrompt)
            return;
        if (action == ActionAccept)
            AcceptQuest(player);
        else
        {
            CloseGossipMenuFor(player);
            ChatHandler(player->GetSession()).PSendSysMessage("{}Hmph. Me ask again later.", Head);
        }
    }

    void OnPlayerLogout(Player* player) override
    {
        std::lock_guard<std::mutex> lock(subclassMutex);
        records.erase(player->GetGUID().GetCounter());
    }

    void OnPlayerDelete(ObjectGuid guid, uint32 /*accountId*/) override
    {
        CharacterDatabase.Execute("DELETE FROM `mod_subclass_character` WHERE `guid` = {}", guid.GetCounter());
        std::lock_guard<std::mutex> lock(subclassMutex);
        records.erase(guid.GetCounter());
    }

private:
    static void TrimChoice(Player* player)
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

        uint32 const removed = ApplyChoice(player, kept);
        ChatHandler(player->GetSession()).PSendSysMessage(
            "Your character no longer qualifies for its subclass; {} talent spell(s) were removed.", removed);
        LOG_INFO("module", "mod-subclass: {} no longer qualifies ({}), removed {} spell(s)", player->GetName(),
            Describe(choice), removed);
    }
};

class item_subclass_book_of_origins : public ItemScript
{
public:
    item_subclass_book_of_origins() : ItemScript(BookScriptName) { }

    bool OnUse(Player* player, Item* item, SpellCastTargets const& /*targets*/) override
    {
        ChatHandler handler(player->GetSession());
        if (!IsEligible(player))
        {
            handler.SendSysMessage("The pages are full of ogre scribbles you cannot make sense of.");
            return true;
        }

        uint32 const bookItemId = item->GetEntry();
        Quest const* quest = sObjectMgr->GetQuestTemplate(CurrentSettings().QuestId);
        QuestStatus const status = quest ? player->GetQuestStatus(quest->GetQuestId()) : QUEST_STATUS_NONE;
        if (status == QUEST_STATUS_COMPLETE && player->CanRewardQuest(quest, true))
            player->RewardQuest(quest, 0, player);
        else if (status == QUEST_STATUS_REWARDED)
            player->DestroyItemCount(bookItemId, 1, true);
        else
        {
            handler.PSendSysMessage("{}Not yet. First we do this properly.", Head);
            return true;
        }

        AddInsight(player);
        handler.PSendSysMessage("{}We read it together. Now we understand! Go to a class trainer, any of them, and "
            "we choose a second way.", Head);
        return true;
    }
};

class mod_subclass_trainers : public AllCreatureScript
{
public:
    mod_subclass_trainers() : AllCreatureScript("mod_subclass_trainers") { }

    bool CanCreatureGossipHello(Player* player, Creature* creature) override
    {
        if (!creature->HasNpcFlag(UNIT_NPC_FLAG_TRAINER_CLASS) || !IsEligible(player) || !RecordOf(player).Insights)
            return false;
        player->PrepareGossipMenu(creature, creature->GetGossipMenuId(), true);
        AddGossipItemFor(player, GOSSIP_ICON_TRAINER, "[Second head] We want to learn the way of another class.",
            SenderTrainer, 0);
        player->SendPreparedGossip(creature);
        return true;
    }

    bool CanCreatureGossipSelect(Player* player, Creature* creature, uint32 sender, uint32 action) override
    {
        switch (sender)
        {
            case SenderTrainer:
                ShowClasses(player, creature);
                return true;
            case SenderClass:
                ShowSpecializations(player, creature, uint8(action));
                return true;
            case SenderSpec:
                ChooseAtTrainer(player, uint16(action));
                return true;
            default:
                return false;
        }
    }
};

class mod_subclass_bosses : public UnitScript
{
public:
    mod_subclass_bosses() : UnitScript("mod_subclass_bosses", true, { UNITHOOK_ON_UNIT_DEATH }) { }

    void OnUnitDeath(Unit* unit, Unit* /*killer*/) override
    {
        Creature* boss = unit->ToCreature();
        if (!boss || !(boss->IsDungeonBoss() || boss->isWorldBoss()))
            return;
        Map* map = boss->GetMap();
        if (!map || !map->IsNonRaidDungeon())
            return;

        Subclass::Settings const current = CurrentSettings();
        for (auto const& reference : map->GetPlayers())
        {
            Player* player = reference.GetSource();
            if (!player || !player->IsInWorld() || !player->IsAtGroupRewardDistance(boss) ||
                !IsAscensionCustomClassId(player->getClass()))
                continue;
            bool finds = false;
            {
                std::lock_guard<std::mutex> lock(subclassMutex);
                finds = Subclass::FindsBooks(current, pairs, CharacterOf(player),
                    player->GetQuestRewardStatus(current.QuestId));
            }
            if (finds && roll_chance_f(current.BookDropChance))
                GrantBook(player, current.BookItemId, boss);
        }
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
            { "quest", HandleQuest, SEC_GAMEMASTER, Console::No },
            { "insight", HandleInsight, SEC_GAMEMASTER, Console::No },
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
        Record const record = RecordOf(target);
        bool const open = !OpenTreesOf(target).empty();
        Subclass::Settings const current = CurrentSettings();
        handler->PSendSysMessage("{}: race {}, gender {}, class {}, level {}, active specialization {}.",
            target->GetName(), uint32(character.Race), uint32(character.Gender), uint32(character.Class),
            uint32(character.Level), GetAscensionActiveSpecialization(target));
        handler->PSendSysMessage(
            "Eligible: {}. Choice: {}. Borrowed trees open: {}. Insights: {}. Quest {} status: {}.",
            IsEligible(target) ? "yes" : "no", Describe(record.Choice), open ? "yes" : "no", record.Insights,
            current.QuestId, uint32(target->GetQuestStatus(current.QuestId)));
        return true;
    }

    static bool HandleSpecs(ChatHandler* handler, uint8 classId)
    {
        std::string list;
        for (auto const& specialization : AscensionCompatData::CoASpecializations)
            if (specialization.ClassId == classId)
                list += Acore::StringFormat(" {}={}", specialization.SpecId, SpecializationName(specialization.SpecId));
        if (list.empty())
        {
            handler->SendErrorMessage("Class {} has no CoA specializations.", uint32(classId));
            return false;
        }
        handler->PSendSysMessage("{} specializations:{}", ClassName(classId), list);
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

        uint32 const removed = ApplyChoice(target, wanted);
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

    static bool HandleQuest(ChatHandler* handler)
    {
        Player* target = Target(handler);
        if (!target)
            return false;
        if (!OffersQuest(target))
        {
            handler->SendErrorMessage("{} is not offered the quest (not eligible, below the level, already chose, "
                "holds an insight, or has taken the quest).", target->GetName());
            return false;
        }
        ShowPrompt(target);
        return true;
    }

    static bool HandleInsight(ChatHandler* handler, uint8 count)
    {
        Player* target = Target(handler);
        if (!target)
            return false;
        Record record = RecordOf(target);
        record.Insights = count;
        StoreRecord(target, record);
        handler->PSendSysMessage("{} now holds {} insight(s).", target->GetName(), uint32(count));
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
    new item_subclass_book_of_origins();
    new mod_subclass_trainers();
    new mod_subclass_bosses();
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
