/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license.
 */

#include "AllCreatureScript.h"
#include "AscensionCoATalentData.h"
#include "AscensionCustomClassData.h"
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
#include "SpellMgr.h"
#include "SubclassRules.h"
#include "StringConvert.h"
#include "Tokenize.h"
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
constexpr uint32 SenderSecondSpec = 9530014;
constexpr uint32 ActionAccept = 1;
constexpr uint32 ActionLater = 2;
constexpr char const* BookScriptName = "item_subclass_book_of_origins";
constexpr char const* Head = "|cffc9a0dcYour second head:|r ";
constexpr char const* AddonPrefix = "SUBCLASS";
constexpr std::size_t AddonChunk = 200;

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
    { 21, "Ranger", "|cffbff06b" },
    { 101, "Warrior", "|cffc79c6e" },
    { 102, "Paladin", "|cfff58cba" },
    { 103, "Hunter", "|cffabd473" },
    { 104, "Rogue", "|cfffff569" },
    { 105, "Priest", "|cffffffff" },
    { 106, "Death Knight", "|cffc41f3b" },
    { 107, "Shaman", "|cff0070de" },
    { 108, "Mage", "|cff69ccf0" },
    { 109, "Warlock", "|cff9482c9" },
    { 111, "Druid", "|cffff7d0a" }
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
    { 64, "Arms" },
    { 65, "Fury" },
    { 66, "Protection" },
    { 67, "Holy" },
    { 68, "Protection" },
    { 69, "Retribution" },
    { 70, "Beast Mastery" },
    { 71, "Marksmanship" },
    { 72, "Survival" },
    { 73, "Assassination" },
    { 74, "Combat" },
    { 75, "Subtlety" },
    { 76, "Discipline" },
    { 77, "Holy" },
    { 78, "Shadow" },
    { 79, "Blood" },
    { 80, "Frost" },
    { 81, "Unholy" },
    { 82, "Elemental" },
    { 83, "Enhancement" },
    { 84, "Restoration" },
    { 85, "Arcane" },
    { 86, "Fire" },
    { 87, "Frost" },
    { 88, "Affliction" },
    { 89, "Destruction" },
    { 90, "Demonology" },
    { 91, "Balance" },
    { 92, "Feral" },
    { 93, "Restoration" },
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

std::string PlainClassName(uint8 classId)
{
    for (NamedClass const& named : Classes)
        if (named.Id == classId)
            return named.Name;
    return Acore::StringFormat("Class {}", uint32(classId));
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

uint8 CatalogClassOf(uint8 classId)
{
    if (classId >= CLASS_WARRIOR && classId <= CLASS_DRUID && classId != CLASS_HERO)
        return AscensionCompatData::CoABorrowOnlyClassOf(classId);
    return classId;
}

std::vector<Subclass::Tree> const& CatalogSpecializations()
{
    static std::vector<Subclass::Tree> const specializations = []
    {
        std::vector<Subclass::Tree> found;
        for (AscensionCompatData::CoATalentEntry const& entry : AscensionCompatData::CoATalentEntries)
        {
            Subclass::Tree const tree = { entry.ClassId, entry.SpecId };
            if (entry.SpecId && std::find(found.begin(), found.end(), tree) == found.end())
                found.push_back(tree);
        }
        std::sort(found.begin(), found.end(), [](Subclass::Tree const& left, Subclass::Tree const& right)
        {
            return left.SpecId < right.SpecId;
        });
        return found;
    }();
    return specializations;
}

bool IsSpecializationOf(uint8 classId, uint16 specId)
{
    std::vector<Subclass::Tree> const& specializations = CatalogSpecializations();
    return specId && std::find(specializations.begin(), specializations.end(), Subclass::Tree{ classId, specId }) !=
        specializations.end();
}

uint8 ClassOfSpecialization(uint16 specId)
{
    for (Subclass::Tree const& specialization : CatalogSpecializations())
        if (specialization.SpecId == specId)
            return specialization.ClassId;
    return 0;
}

bool HasClassTree(uint8 classId)
{
    return std::any_of(AscensionCompatData::CoATalentEntries.begin(), AscensionCompatData::CoATalentEntries.end(),
        [classId](AscensionCompatData::CoATalentEntry const& entry)
        {
            return entry.ClassId == classId && !entry.SpecId;
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
                CatalogClassOf(fields[3].Get<uint8>()), fields[4].Get<uint16>() });
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
            "SELECT `sub_class`, `sub_spec`, `sub_spec2`, `tokens` FROM `mod_subclass_character` WHERE `guid` = {}",
            guid))
    {
        Field* fields = result->Fetch();
        record.Choice = { fields[0].Get<uint8>(), fields[1].Get<uint16>(), fields[2].Get<uint16>() };
        record.Insights = fields[3].Get<uint32>();
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
            "REPLACE INTO `mod_subclass_character` (`guid`, `sub_class`, `sub_spec`, `sub_spec2`, `tokens`, "
            "`chosen_at`) VALUES ({}, {}, {}, {}, {}, UNIX_TIMESTAMP())", guid, uint32(record.Choice.SubClass),
            record.Choice.SubSpec, record.Choice.SecondSpec,
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
    bool const hasClassTree = HasClassTree(choice.SubClass);
    std::lock_guard<std::mutex> lock(subclassMutex);
    return Subclass::OpenTrees(settings, pairs, CharacterOf(player), choice, hasClassTree);
}

uint32 ForgetBorrowedTrees(Player* player, Subclass::Choice const& choice)
{
    uint32 removed = 0;
    std::vector<Subclass::Tree> const trees = Subclass::BorrowedTrees(choice, HasClassTree(choice.SubClass));
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

AscensionCompatData::CoATalentEntry const* FindCatalogEntry(uint32 entryId)
{
    auto const& entries = AscensionCompatData::CoATalentEntries;
    auto itr = std::lower_bound(entries.begin(), entries.end(), entryId,
        [](AscensionCompatData::CoATalentEntry const& entry, uint32 id) { return entry.EntryId < id; });
    return itr != entries.end() && itr->EntryId == entryId ? &*itr : nullptr;
}

bool InTrees(std::vector<Subclass::Tree> const& trees, AscensionCompatData::CoATalentEntry const& entry)
{
    return std::find(trees.begin(), trees.end(), Subclass::Tree{ entry.ClassId, entry.SpecId }) != trees.end();
}

void SendAddon(Player* player, std::string const& message)
{
    WorldPacket packet;
    ChatHandler::BuildChatPacket(packet, CHAT_MSG_WHISPER, LANG_ADDON, player, player,
        std::string(AddonPrefix) + "\t" + message);
    player->SendDirectMessage(&packet);
}

void SendAddonList(Player* player, std::string const& head, std::vector<std::string> const& items)
{
    std::string line;
    for (std::string const& item : items)
    {
        if (!line.empty() && line.size() + item.size() + 1 > AddonChunk)
        {
            SendAddon(player, head + " " + line);
            line.clear();
        }
        line += (line.empty() ? "" : ",") + item;
    }
    if (!line.empty())
        SendAddon(player, head + " " + line);
}

void SendSubclassState(Player* player)
{
    if (!player || !player->IsInWorld())
        return;
    Record const record = RecordOf(player);
    std::vector<Subclass::Tree> const open = OpenTreesOf(player);
    SendAddon(player, Acore::StringFormat("RESET {} {} {}", IsEligible(player) ? 1 : 0, open.size(),
        record.Insights));
    for (std::size_t index = 0; index < open.size(); ++index)
    {
        Subclass::Tree const& tree = open[index];
        SendAddon(player, Acore::StringFormat("TREE {} {} {}", index + 1, tree.SpecId,
            tree.SpecId ? SpecializationName(tree.SpecId) : PlainClassName(tree.ClassId)));
        std::vector<std::string> ids;
        for (AscensionCompatData::CoATalentEntry const& entry : AscensionCompatData::CoATalentEntries)
            if (entry.ClassId == tree.ClassId && entry.SpecId == tree.SpecId && (entry.AECost || entry.TECost))
                ids.push_back(std::to_string(entry.EntryId));
        SendAddonList(player, Acore::StringFormat("IDS {}", index + 1), ids);
    }
    std::vector<std::string> ranks;
    for (AscensionCoATalentState::KnownEntry const& known : GetAscensionKnownTalentEntries(player))
        if (AscensionCompatData::CoATalentEntry const* entry = FindCatalogEntry(known.EntryId);
            known.Rank && entry && InTrees(open, *entry))
            ranks.push_back(Acore::StringFormat("{}:{}", known.EntryId, known.Rank));
    SendAddonList(player, "RANKS", ranks);
    uint32 classPoints = 0;
    uint32 specializationPoints = 0;
    GetAscensionTalentPointsLeft(player, classPoints, specializationPoints);
    SendAddon(player, Acore::StringFormat("POINTS {} {}", classPoints, specializationPoints));
    SendAddon(player, "DONE");
}

void LearnFromAddon(Player* player, uint32 entryId, uint32 rank)
{
    AscensionCompatData::CoATalentEntry const* entry = FindCatalogEntry(entryId);
    if (!entry || !InTrees(OpenTreesOf(player), *entry) || !(entry->AECost || entry->TECost) ||
        rank > entry->SpellCount)
    {
        SendAddon(player, "RESULT 0 That talent is not in your borrowed trees.");
        SendSubclassState(player);
        return;
    }
    std::vector<AscensionCoATalentState::KnownEntry> build = GetAscensionKnownTalentEntries(player);
    auto known = std::find_if(build.begin(), build.end(), [entryId](AscensionCoATalentState::KnownEntry const& item)
    {
        return item.EntryId == entryId;
    });
    if (known != build.end())
        known->Rank = rank;
    else
        build.push_back({ entryId, rank });
    std::string refusal;
    bool const applied = ApplyAscensionTalentBuild(player, build, refusal);
    SendAddon(player, applied ? std::string("RESULT 1") : "RESULT 0 " + refusal);
    SendSubclassState(player);
}

void HandleAddonMessage(Player* player, std::string_view message)
{
    if (message == "SYNC")
    {
        SendSubclassState(player);
        return;
    }
    std::vector<std::string_view> const words = Acore::Tokenize(message, ' ', false);
    if (words.size() == 3 && words[0] == "LEARN")
        if (Optional<uint32> entryId = Acore::StringTo<uint32>(words[1]))
            if (Optional<uint32> rank = Acore::StringTo<uint32>(words[2]))
                LearnFromAddon(player, *entryId, *rank);
}

bool OwnClassGrants(Player const* player, uint32 spellId)
{
    return std::any_of(AscensionCompatData::ClassSpells.begin(), AscensionCompatData::ClassSpells.end(),
        [player, spellId](AscensionCompatData::ClassSpell const& spell)
        {
            return spell.ClassId == player->getClass() && spell.SpellId == spellId;
        });
}

uint32 ForgetBorrowedBaseSpells(Player* player, uint8 subClass)
{
    uint32 removed = 0;
    for (AscensionCompatData::ClassSpell const& spell : AscensionCompatData::ClassSpells)
        if (spell.ClassId == subClass && player->HasSpell(spell.SpellId) && !OwnClassGrants(player, spell.SpellId))
        {
            player->removeSpell(spell.SpellId, SPEC_MASK_ALL, false);
            ++removed;
        }
    return removed;
}

uint32 SyncBorrowedBaseSpells(Player* player)
{
    if (OpenTreesOf(player).empty())
        return 0;
    uint8 const subClass = ChoiceOf(player).SubClass;
    uint32 learned = 0;
    for (AscensionCompatData::ClassSpell const& spell : AscensionCompatData::ClassSpells)
        if (spell.ClassId == subClass && spell.RequiredLevel <= player->GetLevel() &&
            !player->HasSpell(spell.SpellId) && sSpellMgr->GetSpellInfo(spell.SpellId))
        {
            player->learnSpell(spell.SpellId, false);
            ++learned;
        }
    return learned;
}

uint32 ApplyChoice(Player* player, Subclass::Choice const& wanted)
{
    Record record = RecordOf(player);
    uint32 removed = record.Choice == wanted ? 0 : ForgetBorrowedTrees(player, record.Choice);
    if (record.Choice.SubClass && record.Choice.SubClass != wanted.SubClass)
        removed += ForgetBorrowedBaseSpells(player, record.Choice.SubClass);
    record.Choice = wanted;
    StoreRecord(player, record);
    SyncBorrowedBaseSpells(player);
    if (uint32 const active = GetAscensionActiveSpecialization(player))
        SwitchAscensionSpecialization(player, active);
    SendSubclassState(player);
    return removed;
}

void AddInsight(Player* player)
{
    Record record = RecordOf(player);
    ++record.Insights;
    StoreRecord(player, record);
    SendSubclassState(player);
}

std::string Describe(Subclass::Choice const& choice)
{
    if (choice.Empty())
        return "none";
    if (!HasClassTree(choice.SubClass))
        return Acore::StringFormat("{} ({} and {})", ClassName(choice.SubClass), SpecializationName(choice.SubSpec),
            SpecializationName(choice.SecondSpec));
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
            if ((settings.StandardClasses || !AscensionCompatData::IsCoABorrowOnlyClass(named.Id)) &&
                Subclass::AllowsAnySpecializationOf(pairs, character, named.Id))
                AddGossipItemFor(player, GOSSIP_ICON_TRAINER, ClassName(named.Id), SenderClass, named.Id);
    }
    SendGossipMenuFor(player, ClassTextId, trainer->GetGUID());
}

constexpr uint32 SecondSpecAction(uint16 firstSpec, uint16 secondSpec)
{
    return uint32(firstSpec) << 16 | secondSpec;
}

void AddSpecializationItems(Player* player, uint8 classId, uint16 firstSpec, bool final)
{
    Subclass::Character const character = CharacterOf(player);
    std::lock_guard<std::mutex> lock(subclassMutex);
    for (Subclass::Tree const& specialization : CatalogSpecializations())
    {
        if (specialization.ClassId != classId || specialization.SpecId == firstSpec ||
            !Subclass::AllowsSubclass(pairs, character, classId, specialization.SpecId))
            continue;
        std::string const text = firstSpec ?
            Acore::StringFormat("{}: {} and {}", ClassName(classId), SpecializationName(firstSpec),
                SpecializationName(specialization.SpecId)) :
            Acore::StringFormat("{}: {}", ClassName(classId), SpecializationName(specialization.SpecId));
        uint32 const sender = firstSpec ? SenderSecondSpec : SenderSpec;
        uint32 const action = firstSpec ? SecondSpecAction(firstSpec, specialization.SpecId) : specialization.SpecId;
        std::string const confirm = final ? Acore::StringFormat(
            "Spend one insight of the Book of Origins on {}? Talents from an earlier choice are removed.", text) : "";
        player->PlayerTalkClass->GetGossipMenu().AddMenuItem(-1, GOSSIP_ICON_TRAINER, text, sender, action, confirm,
            0);
    }
}

void ShowSpecializations(Player* player, Creature* trainer, uint8 classId)
{
    ClearGossipMenuFor(player);
    AddSpecializationItems(player, classId, 0, HasClassTree(classId));
    SendGossipMenuFor(player, SpecTextId, trainer->GetGUID());
}

void ShowSecondSpecializations(Player* player, Creature* trainer, uint16 firstSpec)
{
    ClearGossipMenuFor(player);
    AddSpecializationItems(player, ClassOfSpecialization(firstSpec), firstSpec, true);
    SendGossipMenuFor(player, SpecTextId, trainer->GetGUID());
}

void ChooseAtTrainer(Player* player, uint16 specId, uint16 secondSpecId)
{
    CloseGossipMenuFor(player);
    ChatHandler handler(player->GetSession());
    uint8 const classId = ClassOfSpecialization(specId);
    Record const record = RecordOf(player);
    Subclass::Choice const wanted = { classId, specId, secondSpecId };
    bool const hasClassTree = HasClassTree(classId);
    bool allowed = false;
    {
        std::lock_guard<std::mutex> lock(subclassMutex);
        allowed = (settings.StandardClasses || !AscensionCompatData::IsCoABorrowOnlyClass(classId)) &&
            Subclass::AllowsChoice(pairs, CharacterOf(player), wanted) &&
            !Subclass::BorrowedTrees(wanted, hasClassTree).empty() && player->GetLevel() >= settings.MinLevel;
    }
    if (!record.Insights || !allowed || !IsSpecializationOf(classId, specId) ||
        (secondSpecId && !IsSpecializationOf(classId, secondSpecId)))
    {
        handler.SendSysMessage("Your heads cannot agree on that.");
        return;
    }

    uint32 const removed = ApplyChoice(player, wanted);
    Record spent = RecordOf(player);
    spent.Insights = record.Insights - 1;
    StoreRecord(player, spent);
    handler.PSendSysMessage("{}Ha! Now we know the ways of the {}. {} talent spell(s) of the old way were forgotten. "
        "Spend points in the Second Head tab of Character Advancement, or type /subclass.", Head, Describe(wanted),
        removed);
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
        loaded.StandardClasses = sConfigMgr->GetOption<bool>("Subclass.StandardClasses", false);
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
        PLAYERHOOK_ON_DELETE, PLAYERHOOK_ON_LEVEL_CHANGED, PLAYERHOOK_ON_GOSSIP_SELECT,
        PLAYERHOOK_CAN_PLAYER_USE_PRIVATE_CHAT })
    {
    }

    void OnPlayerLogin(Player* player) override
    {
        TrimChoice(player);
        SyncBorrowedBaseSpells(player);
        SchedulePrompt(player, 5s);
    }

    void OnPlayerLevelChanged(Player* player, uint8 oldLevel) override
    {
        if (oldLevel < CurrentSettings().MinLevel)
            SchedulePrompt(player, 1s);
        SyncBorrowedBaseSpells(player);
        SendSubclassState(player);
    }

    bool OnPlayerCanUseChat(Player* player, uint32 type, uint32 language, std::string& message, Player* receiver)
        override
    {
        std::string_view const prefix = AddonPrefix;
        if (type != CHAT_MSG_WHISPER || language != LANG_ADDON || receiver != player ||
            message.size() <= prefix.size() || message.compare(0, prefix.size(), prefix) ||
            message[prefix.size()] != '\t')
            return true;
        HandleAddonMessage(player, std::string_view(message).substr(prefix.size() + 1));
        return false;
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
                if (HasClassTree(ClassOfSpecialization(uint16(action))))
                    ChooseAtTrainer(player, uint16(action), 0);
                else
                    ShowSecondSpecializations(player, creature, uint16(action));
                return true;
            case SenderSecondSpec:
                ChooseAtTrainer(player, uint16(action >> 16), uint16(action & 0xFFFF));
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

    static bool HandleSpecs(ChatHandler* handler, uint8 requestedClassId)
    {
        uint8 const classId = CatalogClassOf(requestedClassId);
        std::string list;
        for (Subclass::Tree const& specialization : CatalogSpecializations())
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

    static bool HandleSet(ChatHandler* handler, uint8 requestedSubClass, uint16 subSpec, Optional<uint16> secondSpec)
    {
        Player* target = Target(handler);
        if (!target)
            return false;
        uint8 const subClass = CatalogClassOf(requestedSubClass);

        Subclass::Character const character = CharacterOf(target);
        Subclass::Choice const wanted = { subClass, subSpec, secondSpec.value_or(0) };
        if (!wanted.Empty())
        {
            if (!IsAscensionCustomClassId(character.Class))
            {
                handler->SendErrorMessage("Only CoA classes can have a subclass.");
                return false;
            }
            if (!IsSpecializationOf(subClass, subSpec) ||
                (wanted.SecondSpec && !IsSpecializationOf(subClass, wanted.SecondSpec)))
            {
                handler->SendErrorMessage("Those specializations do not belong to class {}.",
                    uint32(requestedSubClass));
                return false;
            }
            if (Subclass::BorrowedTrees(wanted, HasClassTree(subClass)).empty())
            {
                handler->SendErrorMessage("A standard class lends two different specializations: "
                    ".subclass set <class> <spec> <second spec>.");
                return false;
            }
            std::lock_guard<std::mutex> lock(subclassMutex);
            if (!Subclass::AllowsChoice(pairs, character, wanted))
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
        handler->PSendSysMessage("{}: subclass set to {} ({} old borrowed talent spell(s) removed).",
            target->GetName(), Describe(wanted), removed);
        return true;
    }

    static bool HandleClear(ChatHandler* handler)
    {
        return HandleSet(handler, 0, 0, std::nullopt);
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
        SendSubclassState(target);
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
        std::vector<Subclass::Tree> const open = OpenTreesOf(player);
        if (open.empty())
            return;
        bool const borrowsClassTree = std::any_of(open.begin(), open.end(), [](Subclass::Tree const& tree)
        {
            return !tree.SpecId;
        });
        std::lock_guard<std::mutex> lock(subclassMutex);
        Subclass::AddBonusPoints(settings, borrowsClassTree, classPoints, specializationPoints);
    });
}
