/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license.
 */

#include "AllCreatureScript.h"
#include "AllSpellScript.h"
#include "AscensionAppearanceChanges.h"
#include "AscensionCoATalentData.h"
#include "AscensionCustomClassData.h"
#include "AscensionCustomResourceData.h"
#include "AscensionLiveBaselineData.h"
#include "AscensionPooledVitality.h"
#include "DBCStores.h"
#include "AscensionSpecialization.h"
#include "AscensionTaughtAbilityData.h"
#include "Chat.h"
#include "ClientDBC.h"
#include "CoASpellbook.h"
#include "CommandScript.h"
#include "Config.h"
#include "DatabaseEnv.h"
#include "ItemScript.h"
#include "Log.h"
#include "ObjectMgr.h"
#include "Pet.h"
#include "Mail.h"
#include "Player.h"
#include "PlayerScript.h"
#include "Random.h"
#include "ScriptMgr.h"
#include "ScriptedGossip.h"
#include "ServerScript.h"
#include "Spell.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "SubclassRules.h"
#include "StringConvert.h"
#include "Tokenize.h"
#include "Trainer.h"
#include "UnitScript.h"
#include "WorldPacket.h"
#include "WorldScript.h"
#include "WorldSession.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <mutex>
#include <optional>
#include <tuple>
#include <unordered_map>
#include <unordered_set>

using namespace Acore::ChatCommands;

ChatCommandTable const& SubclassTwinCommandTable();
bool HandleSubclassBrain(ChatHandler* handler);
bool HandleSubclassGun(ChatHandler* handler);
bool HandleSubclassBots(ChatHandler* handler, Optional<std::string> mode);

void HandleTwinLook(Player* player, std::vector<std::string_view> const& words);
void AimAtTarget(Player* player);
void DressLeftHead(Player* player, std::string_view choice);
void LearnStandardMainWeaponSkills(Player* player);
std::string TwinWearing(Player const* player);
void EchoCyclopsHelmet(Player* player);
std::string RelicState(Player* player);
std::string HandleRelicMessage(Player* player, std::vector<std::string_view> const& words);
std::string TwinState(Player const* player);
std::string CyclopsModelState(Player const* player);
std::string CyclopsPreview(uint32 itemId);
std::string ToggleTwin(Player* player);
bool SubclassResting(Player const* player);
bool LonelyTwin(Player const* player);
bool OrderBrother(Player* player, uint32 spellId, Unit* target);
void PressForOtherHalf(Player* player, uint32 spellId);
std::vector<uint32> KnownBorrowedActives(Player const* player);
bool OffersStandardMains();
bool SubclassRaceEnabled(uint8 race);
bool ForcedBot(Player const* player);
void AssignBotSubclass(Player* player);
void SpendBotPoints(Player* player);
void ForgetBotPlayer(Player const* player);
void ForgetDeletedBot(uint32 guid);
bool IsStandardMainClass(uint8 classId);
Trainer::Trainer const* ClassTrainerOf(uint8 classId);
bool IsBorrowedSpell(Player const* player, uint32 spellId);
bool InBorrowedFamily(Player const* player, uint8 subClass, SpellInfo const* info);
void ShowQuestOnTwin(Player* player, bool offered);
Subclass::Offer BorrowedOffer(AscensionCompatData::CoATalentEntry const& entry);
void SyncBorrowedMana(Player* player, uint32 milliseconds);
void ResendBrotherShare(Player const* player);
bool TakeHalfForm(Player* player, SpellInfo const* info);
bool OtherHalfFormAllows(Player const* player, SpellInfo const* info);
Powers HalfFormPower(Player const* player);
void ResendHalfForm(Player* player);

uint8 StoryGenderOf(Player const* player)
{
    return player->GetByteValue(PLAYER_BYTES_3, 0);
}

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
constexpr uint32 SenderUnlearnBorrowed = 9530015;
constexpr uint32 UntalentVisual = 14867;
constexpr uint32 ActionAccept = 1;
constexpr uint32 ActionLater = 2;
constexpr char const* BookScriptName = "item_subclass_book_of_origins";
constexpr char const* BadgeScriptName = "item_subclass_bravery_badge";
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

uint8 CatalogClassOf(uint8 classId);
uint8 PlayableClassOf(uint8 catalogClassId);

uint8 CatalogIdOf(NamedClass const& named)
{
    return AscensionCompatData::IsCoABorrowOnlyClass(named.Id) ? CatalogClassOf(PlayableClassOf(named.Id)) :
        named.Id;
}

bool Names(NamedClass const& named, uint8 classId)
{
    return named.Id == classId || CatalogIdOf(named) == classId;
}

std::string ClassName(uint8 classId)
{
    for (NamedClass const& named : Classes)
        if (Names(named, classId))
            return Acore::StringFormat("{}{}|r", named.Color, named.Name);
    return Acore::StringFormat("class {}", uint32(classId));
}

std::string PlainClassName(uint8 classId)
{
    for (NamedClass const& named : Classes)
        if (Names(named, classId))
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

std::mutex& subclassMutex = *new std::mutex;
Subclass::Settings settings;
std::atomic<bool> thinHumanEnabled{ true };
std::atomic<bool> twoHeadedOgreEnabled{ true };
std::atomic<bool> forceBots{ false };
std::atomic<bool> travelFormsWholeBody{ true };
std::vector<Subclass::Pair> pairs;
std::unordered_map<uint32, Record>& records = *new std::unordered_map<uint32, Record>;

struct Voice
{
    std::string Speaker = "Your second head";
    uint32 PromptText = PromptTextId;
    uint32 ClassText = ClassTextId;
    uint32 SpecText = SpecTextId;
    std::string AcceptOption = "Fine. We go to Shadowfang Keep and get this book.";
    std::string LaterOption = "Not now. Stop talking in my ear.";
    std::string Accepted = "Book. Shadowfang Keep. The mad mage has it. Go!";
    std::string Later = "Hmph. Me ask again later.";
    std::string NotYet = "Not yet. First we do this properly.";
    std::string Used = "We read it together. Now we understand! Go to a class trainer, any of them, and we choose a "
        "second way.";
    std::string Unreadable = "The pages are full of ogre scribbles you cannot make sense of.";
    std::string OneInsight = "One new way at a time. First we choose at a class trainer, then we read again.";
    std::string TrainerOption = "[Second head] We want to learn the way of another class.";
    std::string Chosen = "Ha! Now we know another way.";
    std::string Found = "Look! Another Book of Origins! We can choose again.";
    std::string Refused = "Your heads cannot agree on that.";

    std::string Says(std::string_view line) const
    {
        return Acore::StringFormat("|cffc9a0dc{}:|r {}", Speaker, line);
    }
};

std::vector<Subclass::Story> stories;
std::vector<Voice> voices;

Subclass::Settings CurrentSettings()
{
    std::lock_guard<std::mutex> lock(subclassMutex);
    return settings;
}

Subclass::Character CharacterOf(Player const* player)
{
    return { player->getRace(), StoryGenderOf(player), player->getClass(), player->GetLevel() };
}

uint8 PlayableClassOf(uint8 catalogClassId)
{
    return AscensionCompatData::IsCoABorrowOnlyClass(catalogClassId) ?
        uint8(catalogClassId - AscensionCompatData::CoABorrowOnlyClassBase) : catalogClassId;
}

uint8 CatalogClassOf(uint8 classId)
{
    if (classId >= CLASS_WARRIOR && classId <= CLASS_DRUID && classId != CLASS_HERO &&
        !AscensionCompatData::CoAStockClassesNative)
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

std::vector<uint16> SpecsOf(uint8 classId)
{
    std::vector<uint16> specs;
    for (Subclass::Tree const& specialization : CatalogSpecializations())
        if (specialization.ClassId == classId)
            specs.push_back(specialization.SpecId);
    return specs;
}

bool IsStandardCatalogClass(uint8 catalogClassId)
{
    return !IsAscensionCustomClassId(PlayableClassOf(catalogClassId));
}

uint8 TreesFor(uint8 catalogClassId)
{
    return IsStandardCatalogClass(catalogClassId) ? settings.StandardTrees : settings.CoATrees;
}

bool LendsAllOf(uint8 catalogClassId)
{
    return Subclass::LendsAllSpecializations(SpecsOf(catalogClassId).size(), TreesFor(catalogClassId));
}

std::vector<Subclass::Tree> BorrowedTreesOf(Subclass::Choice const& choice)
{
    return Subclass::BorrowedTrees(choice, HasClassTree(choice.SubClass), SpecsOf(choice.SubClass),
        TreesFor(choice.SubClass));
}

bool StoryTableExists()
{
    return bool(WorldDatabase.Query("SELECT 1 FROM information_schema.TABLES WHERE TABLE_SCHEMA = DATABASE() AND "
        "TABLE_NAME = 'mod_subclass_story'"));
}

void LoadStories()
{
    std::vector<Subclass::Story> loadedStories;
    std::vector<Voice> loadedVoices;
    if (StoryTableExists())
        if (QueryResult result = WorldDatabase.Query(
                "SELECT `race`, `gender_mask`, `quest_id`, `relic_item_id`, `prompt_text`, `class_text`, `spec_text`, "
                "`speaker`, `accept_option`, `later_option`, `accepted`, `later`, `not_yet`, `used`, `unreadable`, "
                "`trainer_option`, `chosen`, `found`, `refused` FROM `mod_subclass_story` WHERE `enabled` = 1"))
        {
            do
            {
                Field* fields = result->Fetch();
                loadedStories.push_back({ fields[0].Get<uint8>(), fields[1].Get<uint8>(), fields[2].Get<uint32>(),
                    fields[3].Get<uint32>() });
                Voice voice;
                voice.PromptText = fields[4].Get<uint32>();
                voice.ClassText = fields[5].Get<uint32>();
                voice.SpecText = fields[6].Get<uint32>();
                voice.Speaker = fields[7].Get<std::string>();
                voice.AcceptOption = fields[8].Get<std::string>();
                voice.LaterOption = fields[9].Get<std::string>();
                voice.Accepted = fields[10].Get<std::string>();
                voice.Later = fields[11].Get<std::string>();
                voice.NotYet = fields[12].Get<std::string>();
                voice.Used = fields[13].Get<std::string>();
                voice.Unreadable = fields[14].Get<std::string>();
                voice.TrainerOption = fields[15].Get<std::string>();
                voice.Chosen = fields[16].Get<std::string>();
                voice.Found = fields[17].Get<std::string>();
                voice.Refused = fields[18].Get<std::string>();
                loadedVoices.push_back(std::move(voice));
            } while (result->NextRow());
        }

    std::lock_guard<std::mutex> lock(subclassMutex);
    stories = std::move(loadedStories);
    voices = std::move(loadedVoices);
    LOG_INFO("module", "mod-subclass: loaded {} race stor(y/ies)", stories.size());
}

Subclass::Settings SettingsOf(Player const* player)
{
    std::lock_guard<std::mutex> lock(subclassMutex);
    return Subclass::WithStory(settings, stories, CharacterOf(player));
}

Voice VoiceOf(Player const* player)
{
    std::lock_guard<std::mutex> lock(subclassMutex);
    if (Subclass::Story const* story = Subclass::StoryFor(stories, CharacterOf(player)))
        return voices[story - stories.data()];
    return {};
}

Voice VoiceOfRelic(uint32 itemId)
{
    std::lock_guard<std::mutex> lock(subclassMutex);
    for (std::size_t index = 0; index < stories.size(); ++index)
        if (stories[index].RelicItemId == itemId)
            return voices[index];
    return {};
}

std::string RelicName(uint32 itemId)
{
    ItemTemplate const* relic = sObjectMgr->GetItemTemplate(itemId);
    return relic ? relic->Name1 : "relic";
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
        record.Insights = std::min<uint32>(fields[3].Get<uint32>(), 1);
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

void ForgetSubclassOf(uint32 guid)
{
    CharacterDatabase.DirectExecute("DELETE FROM `mod_subclass_character` WHERE `guid` = {}", guid);
    ForgetDeletedBot(guid);
    std::lock_guard<std::mutex> lock(subclassMutex);
    records.erase(guid);
}

bool MainMaySubclass(uint8 classId);

bool MayCarrySubclass(Player const* player)
{
    return MainMaySubclass(player->getClass()) && SubclassRaceEnabled(player->getRace());
}

bool MainMaySubclass(uint8 classId)
{
    bool const standard = !IsAscensionCustomClassId(classId);
    if (!Subclass::KindAllows(CurrentSettings().Mains, standard))
        return false;
    return !standard || (IsStandardMainClass(classId) && OffersStandardMains());
}

bool IsEligible(Player const* player)
{
    if (!MayCarrySubclass(player))
        return false;
    std::lock_guard<std::mutex> lock(subclassMutex);
    return Subclass::IsEligible(pairs, CharacterOf(player));
}

std::vector<Subclass::Tree> OpenTreesOf(Player const* player)
{
    if (!MayCarrySubclass(player))
        return {};
    Subclass::Choice const choice = ChoiceOf(player);
    bool const hasClassTree = HasClassTree(choice.SubClass);
    std::lock_guard<std::mutex> lock(subclassMutex);
    return Subclass::OpenTrees(settings, pairs, CharacterOf(player), choice, hasClassTree,
        SpecsOf(choice.SubClass), TreesFor(choice.SubClass));
}

uint32 ForgetBorrowedTrees(Player* player, Subclass::Choice const& choice)
{
    uint32 removed = 0;
    std::vector<Subclass::Tree> const trees = BorrowedTreesOf(choice);
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

bool CallsHunterPet(SpellInfo const* info)
{
    for (SpellEffectInfo const& effect : info->GetEffects())
        if (effect.Effect == SPELL_EFFECT_SUMMON_PET && !effect.MiscValue)
            return true;
    return false;
}

void BringBackParkedHunterPet(Player* player)
{
    PetStable* stable = player->GetPetStable();
    if (!stable || stable->CurrentPet || !player->IsClass(CLASS_HUNTER, CLASS_CONTEXT_PET))
        return;
    auto parked = [stable]()
    {
        return std::find_if(stable->UnslottedPets.begin(), stable->UnslottedPets.end(),
            [](PetStable::PetInfo const& pet) { return pet.Type == HUNTER_PET; });
    };
    if (parked() == stable->UnslottedPets.end())
        return;
    if (Pet* other = player->GetPet(); other && other->getPetType() != HUNTER_PET)
        player->RemovePet(other, PET_SAVE_NOT_IN_SLOT, false);
    if (auto hunter = parked(); hunter != stable->UnslottedPets.end())
        std::rotate(stable->UnslottedPets.begin(), hunter, std::next(hunter));
}

bool HandlesOwnPet(SpellInfo const* info)
{
    auto handles = [](SpellInfo const* candidate)
    {
        return candidate && (candidate->HasEffect(SPELL_EFFECT_TAMECREATURE) ||
            candidate->HasEffect(SPELL_EFFECT_CREATE_TAMED_PET) || candidate->HasEffect(SPELL_EFFECT_SUMMON_PET) ||
            candidate->HasEffect(SPELL_EFFECT_DISMISS_PET) || candidate->HasEffect(SPELL_EFFECT_RESURRECT_PET) ||
            candidate->HasEffect(SPELL_EFFECT_FEED_PET));
    };
    if (handles(info))
        return true;
    for (SpellEffectInfo const& effect : info->GetEffects())
        if (effect.TriggerSpell && handles(sSpellMgr->GetSpellInfo(effect.TriggerSpell)))
            return true;
    return false;
}

std::unordered_set<uint32> runesLent;
std::unordered_set<uint16> tankSpecializations;
std::unordered_set<uint16> distantSpecializations;
constexpr uint32 ChrSpecsFields = 29;
constexpr uint32 ChrSpecsMeleeRole = 11;
constexpr uint32 ChrSpecsRangedRole = 12;
constexpr uint32 ChrSpecsCasterRole = 13;
constexpr uint32 ChrSpecsTankRole = 14;
constexpr uint32 ChrSpecsHealerRole = 15;

void LoadSpecializationRoles()
{
    ClientDBC specs;
    std::unordered_set<uint16> tanks;
    std::unordered_set<uint16> distant;
    if (specs.Load(GetClientDBCPath("ChrSpecs.dbc"), ChrSpecsFields))
        for (uint32 row = 0; row < specs.GetRecordCount(); ++row)
        {
            ClientDBC::Record record = specs.GetRecord(row);
            uint16 const specId = uint16(record.GetUInt32(0));
            bool const close = record.GetUInt32(ChrSpecsMeleeRole) || record.GetUInt32(ChrSpecsTankRole);
            if (record.GetUInt32(ChrSpecsTankRole))
                tanks.insert(specId);
            if (!close && (record.GetUInt32(ChrSpecsRangedRole) || record.GetUInt32(ChrSpecsCasterRole) ||
                    record.GetUInt32(ChrSpecsHealerRole)))
                distant.insert(specId);
        }
    LOG_INFO("module", "mod-subclass: {} tank and {} ranged specialization(s)", tanks.size(), distant.size());
    std::lock_guard<std::mutex> lock(subclassMutex);
    tankSpecializations = std::move(tanks);
    distantSpecializations = std::move(distant);
}

constexpr uint32 AdvancementFields = 121;
constexpr uint32 AdvancementGroup = 29;
constexpr uint32 AdvancementTabClassInvestment = 38;
constexpr uint32 AdvancementTabSpecInvestment = 39;
constexpr uint32 AdvancementLinksAt = 415;
constexpr uint32 AdvancementLinkCount = 15;
constexpr uint32 AdvancementFlagsAt = 479;
constexpr uint32 AdvancementLinksMustBeMaxed = 0x200000;

struct TalentLinks
{
    std::vector<uint32> Links;
    uint32 ClassInvestment = 0;
    uint32 SpecInvestment = 0;
    uint32 Group = 0;
    bool LinksMustBeMaxed = false;
};

std::unordered_map<uint32, TalentLinks> talentLinks;

void LoadTalentLinks()
{
    ClientDBC advancement;
    std::unordered_map<uint32, TalentLinks> loaded;
    if (advancement.Load(GetClientDBCPath("CharacterAdvancement.dbc"), AdvancementFields))
        for (uint32 row = 0; row < advancement.GetRecordCount(); ++row)
        {
            ClientDBC::Record record = advancement.GetRecord(row);
            TalentLinks links;
            for (uint32 index = 0; index < AdvancementLinkCount; ++index)
                if (uint32 const link = record.GetUInt32At(AdvancementLinksAt + index * sizeof(uint32)))
                    links.Links.push_back(link);
            links.ClassInvestment = record.GetUInt32(AdvancementTabClassInvestment);
            links.SpecInvestment = record.GetUInt32(AdvancementTabSpecInvestment);
            links.Group = record.GetUInt32(AdvancementGroup);
            links.LinksMustBeMaxed = (record.GetUInt32At(AdvancementFlagsAt) & AdvancementLinksMustBeMaxed) != 0;
            loaded[record.GetUInt32(0)] = std::move(links);
        }
    LOG_INFO("module", "mod-subclass: talent links for {} advancement entries", loaded.size());
    std::lock_guard<std::mutex> lock(subclassMutex);
    talentLinks = std::move(loaded);
}

void LinkOffer(Subclass::Offer& offer)
{
    std::lock_guard<std::mutex> lock(subclassMutex);
    auto const found = talentLinks.find(offer.EntryId);
    if (found == talentLinks.end())
        return;
    offer.Links = found->second.Links;
    offer.ClassInvestment = found->second.ClassInvestment;
    offer.SpecInvestment = found->second.SpecInvestment;
    offer.Group = found->second.Group;
    offer.LinksMustBeMaxed = found->second.LinksMustBeMaxed;
}
thread_local bool raisingRunes = false;

bool BorrowsDeathKnight(Player const* player)
{
    Subclass::Choice const choice = ChoiceOf(player);
    return player->getClass() != CLASS_DEATH_KNIGHT && choice.Complete() &&
        PlayableClassOf(choice.SubClass) == CLASS_DEATH_KNIGHT;
}

bool HasLentRunes(Player const* player)
{
    std::lock_guard<std::mutex> lock(subclassMutex);
    return runesLent.contains(player->GetGUID().GetCounter());
}

void LendRunes(Player* player)
{
    if (!BorrowsDeathKnight(player) || HasLentRunes(player))
        return;
    raisingRunes = true;
    player->InitRunes();
    raisingRunes = false;
    std::lock_guard<std::mutex> lock(subclassMutex);
    runesLent.insert(player->GetGUID().GetCounter());
}

Powers StandardClassPower(uint8 catalogClassId)
{
    switch (PlayableClassOf(catalogClassId))
    {
        case CLASS_WARRIOR: return POWER_RAGE;
        case CLASS_ROGUE: return POWER_ENERGY;
        case CLASS_DEATH_KNIGHT: return POWER_RUNIC_POWER;
        default: return MAX_POWERS;
    }
}

Powers SubclassPower(uint8 catalogClassId)
{
    if (Powers const standard = StandardClassPower(catalogClassId); standard != MAX_POWERS)
        return standard;
    ChrClassesEntry const* entry = sChrClassesStore.LookupEntry(PlayableClassOf(catalogClassId));
    if (!entry)
        return MAX_POWERS;
    switch (Powers(entry->powerType))
    {
        case POWER_MANA:
        case POWER_RAGE:
        case POWER_FOCUS:
        case POWER_ENERGY:
        case POWER_RUNIC_POWER:
            return Powers(entry->powerType);
        default:
            return MAX_POWERS;
    }
}

bool StanceLike(SpellInfo const* info)
{
    if (!info || info->IsPassive())
        return false;
    SpellSpecificType const specific = info->GetSpellSpecific();
    return specific == SPELL_SPECIFIC_AURA || specific == SPELL_SPECIFIC_ASPECT ||
        specific == SPELL_SPECIFIC_PRESENCE || info->HasAura(SPELL_AURA_MOD_SHAPESHIFT) ||
        (info->HasAttribute(SPELL_ATTR2_USE_SHAPESHIFT_BAR) && !info->HasAttribute(SPELL_ATTR2_NO_SHAPESHIFT_UI));
}

uint32 SpellFamilyOfClass(uint8 classId)
{
    ChrClassesEntry const* entry = sChrClassesStore.LookupEntry(classId);
    return entry ? entry->spellfamily : 0;
}

bool BorrowedFrom(SpellInfo const* info, uint8 subClass, uint32 ownFamily)
{
    uint32 const family = SpellFamilyOfClass(PlayableClassOf(subClass));
    if (family && info->SpellFamilyName == family && family != ownFamily)
        return true;
    uint32 const firstRank = info->GetFirstRankSpell()->Id;
    return std::any_of(AscensionCompatData::ClassSpells.begin(), AscensionCompatData::ClassSpells.end(),
        [subClass, firstRank](AscensionCompatData::ClassSpell const& spell)
        {
            SpellInfo const* granted = sSpellMgr->GetSpellInfo(spell.SpellId);
            return spell.ClassId == subClass && granted && granted->GetFirstRankSpell()->Id == firstRank;
        });
}

std::string BorrowedStances(Player const* player)
{
    Subclass::Choice const choice = ChoiceOf(player);
    std::vector<std::pair<uint32, uint32>> stances;
    if (choice.Complete() && !OpenTreesOf(player).empty())
    {
        uint32 const ownFamily = SpellFamilyOfClass(player->getClass());
        for (auto const& [spellId, spell] : player->GetSpellMap())
        {
            SpellInfo const* info = sSpellMgr->GetSpellInfo(spellId);
            if (spell->State == PLAYERSPELL_REMOVED || !spell->Active || !StanceLike(info) ||
                !BorrowedFrom(info, choice.SubClass, ownFamily))
                continue;
            if (SpellInfo const* next = info->GetNextRankSpell(); next && player->HasSpell(next->Id))
                continue;
            stances.emplace_back(info->GetFirstRankSpell()->Id, spellId);
        }
    }
    std::sort(stances.begin(), stances.end());
    stances.erase(std::unique(stances.begin(), stances.end(), [](auto const& a, auto const& b)
    {
        SpellInfo const* first = sSpellMgr->GetSpellInfo(a.second);
        SpellInfo const* second = sSpellMgr->GetSpellInfo(b.second);
        return first && second && std::string_view(first->SpellName[0]) == second->SpellName[0];
    }), stances.end());
    std::string out = "STANCES";
    for (auto const& stance : stances)
        out += " " + std::to_string(stance.second);
    return out;
}

bool OffersQuest(Player const* player);
void ShowPrompt(Player* player, ObjectGuid source);

std::vector<Subclass::Offer> BorrowedOffers(std::vector<Subclass::Tree> const& open)
{
    std::vector<Subclass::Offer> offers;
    for (AscensionCompatData::CoATalentEntry const& entry : AscensionCompatData::CoATalentEntries)
        if ((entry.AECost || entry.TECost) && InTrees(open, entry))
            offers.push_back(BorrowedOffer(entry));
    return offers;
}

std::vector<AscensionCoATalentState::KnownEntry> KnownBuild(Player const* player)
{
    std::vector<AscensionCoATalentState::KnownEntry> known = GetAscensionExtraTreeEntries(player);
    std::vector<AscensionCoATalentState::KnownEntry> const own = GetAscensionKnownTalentEntries(player);
    known.insert(known.end(), own.begin(), own.end());
    return known;
}

std::vector<Subclass::Rank> RanksOf(std::vector<AscensionCoATalentState::KnownEntry> const& build)
{
    std::vector<Subclass::Rank> ranks;
    for (AscensionCoATalentState::KnownEntry const& item : build)
        ranks.push_back({ item.EntryId, item.Rank });
    return ranks;
}

std::vector<uint32> PresetEntriesOf(Player const* player)
{
    Subclass::Choice const choice = ChoiceOf(player);
    std::vector<uint32> presets;
    if (!choice.Complete() || !IsAscensionCustomClassId(choice.SubClass))
        return presets;
    std::vector<Subclass::Tree> const open = OpenTreesOf(player);
    for (AscensionCompatData::CoASpecialization const& specialization : AscensionCompatData::CoASpecializations)
        if (specialization.SpecId && (specialization.SpecId == choice.SubSpec ||
                specialization.SpecId == choice.SecondSpec))
            if (AscensionCompatData::CoATalentEntry const* signature =
                    FindCatalogEntry(specialization.SignatureEntryId);
                signature && signature->SpellCount && InTrees(open, *signature) &&
                std::find(presets.begin(), presets.end(), signature->EntryId) == presets.end())
                presets.push_back(signature->EntryId);
    return presets;
}

bool IsPresetEntry(Player const* player, uint32 entryId)
{
    std::vector<uint32> const presets = PresetEntriesOf(player);
    return std::find(presets.begin(), presets.end(), entryId) != presets.end();
}

uint32 GrantSpecializationPresets(Player* player)
{
    uint32 granted = 0;
    for (uint32 entryId : PresetEntriesOf(player))
        if (AscensionCompatData::CoATalentEntry const* entry = FindCatalogEntry(entryId);
            entry && !GetAscensionTalentRank(player, entryId) && sSpellMgr->GetSpellInfo(entry->SpellIds[0]))
        {
            player->learnSpell(entry->SpellIds[0], false);
            ++granted;
        }
    return granted;
}

std::string TalentName(uint32 entryId)
{
    AscensionCompatData::CoATalentEntry const* entry = FindCatalogEntry(entryId);
    SpellInfo const* info = entry && entry->SpellCount ? sSpellMgr->GetSpellInfo(entry->SpellIds[0]) : nullptr;
    return info ? std::string(info->SpellName[0]) : Acore::StringFormat("talent {}", entryId);
}

std::string GateRefusal(Subclass::Offer const& offer, Subclass::Gate gate, std::vector<Subclass::Offer> const& offers,
    std::vector<Subclass::Rank> const& ranks)
{
    switch (gate)
    {
        case Subclass::Gate::Level:
            return Acore::StringFormat("{} requires level {}.", TalentName(offer.EntryId), offer.RequiredLevel);
        case Subclass::Gate::Link:
        {
            std::string links;
            for (uint32 link : offer.Links)
                links += (links.empty() ? "" : " or ") + TalentName(link);
            return Acore::StringFormat("Learn {} first.", links);
        }
        case Subclass::Gate::Requirement:
        {
            std::string required;
            for (uint32 entryId : offer.Requires)
                if (entryId && !Subclass::RankIn(ranks, entryId))
                    required += (required.empty() ? "" : " and ") + TalentName(entryId);
            return Acore::StringFormat("Learn {} first.", required);
        }
        case Subclass::Gate::Investment:
            return Acore::StringFormat("Spend {} point(s) in this tree before {}.",
                std::max(offer.ClassInvestment, offer.SpecInvestment), TalentName(offer.EntryId));
        case Subclass::Gate::Group:
            for (Subclass::Offer const& other : offers)
                if (other.EntryId != offer.EntryId && other.Group == offer.Group &&
                    Subclass::RankIn(ranks, other.EntryId))
                    return Acore::StringFormat("You already chose {} instead.", TalentName(other.EntryId));
            return "You already chose another talent of this choice.";
        default:
            return {};
    }
}

void SendTalentGates(Player* player, std::vector<Subclass::Tree> const& open,
    std::vector<AscensionCoATalentState::KnownEntry> const& known)
{
    std::vector<Subclass::Offer> const offers = BorrowedOffers(open);
    std::vector<Subclass::Rank> const ranks = RanksOf(known);
    std::vector<std::string> gates;
    for (Subclass::Offer const& offer : offers)
        if (Subclass::Gate const gate = Subclass::GateOf(offers, ranks, offer, player->GetLevel());
            gate != Subclass::Gate::Open)
            gates.push_back(Acore::StringFormat("{}:{}", offer.EntryId, uint32(gate)));
    SendAddonList(player, "GATES", gates);
    std::vector<std::string> presets;
    for (uint32 entryId : PresetEntriesOf(player))
        presets.push_back(std::to_string(entryId));
    SendAddonList(player, "PRESET", presets);
}

constexpr uint32 BloodmageThirst = 706613;
constexpr uint32 BloodmageThirstPassive = 92112;

constexpr std::array<std::pair<uint32, uint32>, 2> ResourceFeeders = {{
    { AscensionBloodmage::PooledVitality, AscensionBloodmage::PooledVitalityTalent },
    { BloodmageThirst, BloodmageThirstPassive } }};

bool CanBuildResource(Player const* player, uint32 resourceSpellId)
{
    auto const feeder = std::find_if(ResourceFeeders.begin(), ResourceFeeders.end(),
        [resourceSpellId](std::pair<uint32, uint32> const& pair) { return pair.first == resourceSpellId; });
    return feeder == ResourceFeeders.end() || player->HasSpell(feeder->second) || player->HasAura(resourceSpellId);
}

void SendSubclassState(Player* player)
{
    if (!player || !player->IsInWorld())
        return;
    Record const record = RecordOf(player);
    std::vector<Subclass::Tree> const open = OpenTreesOf(player);
    SendAddon(player, Acore::StringFormat("RESET {} {} {}", IsEligible(player) ? 1 : 0, open.size(),
        record.Insights));
    SendAddon(player, Acore::StringFormat("QUESTOFFER {}", OffersQuest(player) ? 1 : 0));
    Subclass::Choice const choice = ChoiceOf(player);
    SendAddon(player, "SUBCLASS " + (choice.Complete() ? PlainClassName(choice.SubClass) : std::string("-")));
    std::vector<std::string> otherSpells;
    std::vector<std::string> offGlobalCooldown;
    for (uint32 spellId : KnownBorrowedActives(player))
    {
        otherSpells.push_back(std::to_string(spellId));
        if (SpellInfo const* info = sSpellMgr->GetSpellInfo(spellId); info && !info->StartRecoveryTime)
            offGlobalCooldown.push_back(std::to_string(spellId));
        if (uint32 const remaining = player->GetSpellCooldownDelay(spellId))
            SendAddon(player, Acore::StringFormat("OTHERCD {} {}", spellId, remaining));
    }
    SendAddonList(player, "OTHERSPELLS", otherSpells);
    SendAddonList(player, "OFFGCD", offGlobalCooldown);
    SendAddon(player, RelicState(player));
    SendAddon(player, BorrowedStances(player));
    for (std::size_t index = 0; index < open.size(); ++index)
    {
        Subclass::Tree const& tree = open[index];
        SendAddon(player, Acore::StringFormat("TREE {} {} {}", index + 1, tree.SpecId,
            tree.SpecId ? SpecializationName(tree.SpecId) : PlainClassName(tree.ClassId)));
        SendAddon(player, Acore::StringFormat("ART {} {}", index + 1, PlainClassName(tree.ClassId)));
        std::vector<std::string> ids;
        std::vector<std::string> abilities;
        for (AscensionCompatData::CoATalentEntry const& entry : AscensionCompatData::CoATalentEntries)
            if (entry.ClassId == tree.ClassId && entry.SpecId == tree.SpecId &&
                (entry.AECost || entry.TECost || entry.SpellCount))
                (entry.Talent ? ids : abilities).push_back(std::to_string(entry.EntryId));
        SendAddonList(player, Acore::StringFormat("IDS {}", index + 1), ids);
        SendAddonList(player, Acore::StringFormat("ABIL {}", index + 1), abilities);
    }
    std::vector<std::string> ranks;
    std::unordered_set<uint32> listed;
    std::vector<AscensionCoATalentState::KnownEntry> const known = KnownBuild(player);
    for (AscensionCoATalentState::KnownEntry const& item : known)
        if (AscensionCompatData::CoATalentEntry const* entry = FindCatalogEntry(item.EntryId);
            item.Rank && entry && InTrees(open, *entry) && listed.insert(item.EntryId).second)
            ranks.push_back(Acore::StringFormat("{}:{}", item.EntryId, item.Rank));
    SendAddonList(player, "RANKS", ranks);
    if (!open.empty())
        SendTalentGates(player, open, known);
    uint32 classPoints = 0;
    uint32 specializationPoints = 0;
    GetAscensionTalentPointsLeft(player, classPoints, specializationPoints);
    SendAddon(player, Acore::StringFormat("POINTS {} {}", classPoints, specializationPoints));
    if (!open.empty())
    {
        uint8 const subClass = record.Choice.SubClass;
        for (AscensionCompatData::ResourceDisplay const& display : AscensionCompatData::ResourceDisplays)
            if (display.ClassId == subClass && CanBuildResource(player, display.SpellId))
                SendAddon(player, Acore::StringFormat("RES {} {} {}", display.SpellId, display.DisplayMaximum,
                    display.Name));
        std::vector<uint32> powers;
        for (AscensionCompatData::NativePowerGainRule const& rule : AscensionCompatData::NativePowerGainRules)
            if (rule.ClassId == subClass && rule.PowerType != player->getPowerType() &&
                std::find(powers.begin(), powers.end(), uint32(rule.PowerType)) == powers.end())
                powers.push_back(rule.PowerType);
        if (subClass == CLASS_RANGER && player->getPowerType() != POWER_FOCUS)
            powers.push_back(POWER_FOCUS);
        for (Powers const borrowed : { SubclassPower(subClass), HalfFormPower(player) })
            if (borrowed != MAX_POWERS && player->getPowerType() != borrowed &&
                std::find(powers.begin(), powers.end(), uint32(borrowed)) == powers.end())
                powers.push_back(borrowed);
        if (!SubclassResting(player))
        {
            for (uint32 power : powers)
                SendAddon(player, Acore::StringFormat("POWER {}", power));
            if (BorrowsDeathKnight(player) && HasLentRunes(player))
                SendAddon(player, "RUNES");
        }
    }
    SendAddon(player, "DONE");
}

std::string TalentChangeRefusal(Player* player, uint32 entryId, uint32 rank)
{
    std::vector<Subclass::Offer> const offers = BorrowedOffers(OpenTreesOf(player));
    Subclass::Offer const* offer = Subclass::OfferIn(offers, entryId);
    if (!offer)
        return "That talent is not in your borrowed trees.";
    std::vector<Subclass::Rank> const before = RanksOf(KnownBuild(player));
    uint32 const current = Subclass::RankIn(before, entryId);
    if (rank < current && IsPresetEntry(player, entryId))
        return Acore::StringFormat("{} comes with your specialization and stays learned.", TalentName(entryId));
    if (rank > current)
        if (Subclass::Gate const gate = Subclass::GateOf(offers, before, *offer, player->GetLevel());
            gate != Subclass::Gate::Open)
            return GateRefusal(*offer, gate, offers, before);
    std::vector<Subclass::Rank> after = { { entryId, rank } };
    after.insert(after.end(), before.begin(), before.end());
    if (uint32 const stranded = Subclass::StrandedBy(offers, before, after, player->GetLevel()))
        return Acore::StringFormat("Unlearn {} first, it builds on this talent.", TalentName(stranded));
    return {};
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
    if (std::string const refusal = TalentChangeRefusal(player, entryId, rank); !refusal.empty())
    {
        SendAddon(player, "RESULT 0 " + refusal);
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
        ResendBrotherShare(player);
        ResendHalfForm(player);
        SendSubclassState(player);
        SendAddon(player, TwinState(player));
        SendAddon(player, RelicState(player));
        SendAddon(player, CyclopsModelState(player));
        return;
    }
    if (message == "QUESTASK")
    {
        if (OffersQuest(player))
            ShowPrompt(player, player->GetGUID());
        return;
    }
    if (message == "AIM")
    {
        AimAtTarget(player);
        return;
    }
    if (message == "TWINTOGGLE")
    {
        ChatHandler(player->GetSession()).SendSysMessage(ToggleTwin(player));
        SendAddon(player, TwinState(player));
        return;
    }
    std::vector<std::string_view> const words = Acore::Tokenize(message, ' ', false);
    if (words.size() == 2 && words[0] == "PRESS")
    {
        if (Optional<uint32> spellId = Acore::StringTo<uint32>(words[1]))
            PressForOtherHalf(player, *spellId);
        return;
    }
    if (words.size() == 2 && words[0] == "LEFTHEAD")
    {
        DressLeftHead(player, words[1]);
        SendAddon(player, TwinWearing(player));
        return;
    }
    if (!words.empty() && words[0] == "OWNLOOK")
    {
        std::vector<std::pair<uint32, uint32>> changes;
        for (std::size_t index = 1; index < words.size(); ++index)
        {
            std::vector<std::string_view> const pair = Acore::Tokenize(words[index], ':', false);
            Optional<uint32> category = pair.size() == 2 ? Acore::StringTo<uint32>(pair[0]) : std::nullopt;
            Optional<uint32> appearance = pair.size() == 2 ? Acore::StringTo<uint32>(pair[1]) : std::nullopt;
            if (category && appearance)
                changes.emplace_back(*category, *appearance);
        }
        if (changes.empty() || !ApplyAscensionAppearanceChanges(player, changes))
            ChatHandler(player->GetSession()).SendSysMessage("Your wardrobe could not apply that look.");
        return;
    }
    if (!words.empty() && words[0] == "TWINLOOK")
    {
        HandleTwinLook(player, words);
        SendAddon(player, TwinWearing(player));
    }
    else if (words.size() == 1 && words[0] == "TWINWEARING")
        SendAddon(player, TwinWearing(player));
    else if (!words.empty() && words[0].starts_with("RELIC"))
    {
        if (std::string const refusal = HandleRelicMessage(player, words); !refusal.empty())
            ChatHandler(player->GetSession()).SendSysMessage(refusal);
        SendAddon(player, RelicState(player));
    }
    else if (words.size() == 1 && words[0] == "CYCLOPSECHO")
    {
        EchoCyclopsHelmet(player);
        SendAddon(player, TwinWearing(player));
    }
    else if (words.size() == 2 && words[0] == "CYCLOPSPREVIEW")
    {
        if (Optional<uint32> itemId = Acore::StringTo<uint32>(words[1]))
            SendAddon(player, CyclopsPreview(*itemId));
    }
    else if (words.size() == 3 && words[0] == "LEARN")
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

std::vector<uint16> ClassSkillLinesOf(std::vector<uint32> const& spells);

std::vector<uint32> TaughtBy(Trainer::Trainer const* trainer)
{
    std::vector<uint32> taught;
    if (!trainer)
        return taught;
    for (Trainer::Spell const& spell : trainer->GetSpells())
    {
        taught.push_back(spell.SpellId);
        if (SpellInfo const* info = sSpellMgr->GetSpellInfo(spell.SpellId))
            for (SpellEffectInfo const& effect : info->GetEffects())
                if (effect.IsEffect(SPELL_EFFECT_LEARN_SPELL) && effect.TriggerSpell)
                    taught.push_back(effect.TriggerSpell);
    }
    return taught;
}

uint32 ForgetStandardKit(Player* player, uint8 playableClass)
{
    std::vector<uint32> const own = TaughtBy(ClassTrainerOf(player->getClass()));
    std::vector<uint16> const ownLines = ClassSkillLinesOf(own);
    for (uint16 skill : ClassSkillLinesOf(TaughtBy(ClassTrainerOf(playableClass))))
        if (player->HasSkill(skill) && std::find(ownLines.begin(), ownLines.end(), skill) == ownLines.end())
            player->SetSkill(skill, 0, 0, 0);
    uint32 removed = 0;
    for (uint32 spellId : TaughtBy(ClassTrainerOf(playableClass)))
        if (player->HasSpell(spellId) && std::find(own.begin(), own.end(), spellId) == own.end())
        {
            player->removeSpell(spellId, SPEC_MASK_ALL, false);
            ++removed;
        }
    return removed;
}

void ForgetStandardWeapons(Player* player, uint8 playableClass);
void ForgetCoAWeapons(Player* player, uint8 subClass);

bool TaughtToSubclass(Player const* player, Subclass::Choice const& choice,
    AscensionCompatData::TaughtAbility const& entry)
{
    return entry.ClassId == choice.SubClass && (entry.SpecId == choice.SubSpec || entry.SpecId == choice.SecondSpec) &&
        player->GetLevel() >= entry.RequiredLevel && player->HasSpell(entry.ParentSpellId);
}

bool IsTaughtAbilityParent(uint32 spellId)
{
    return std::any_of(AscensionCompatData::TaughtAbilities.begin(), AscensionCompatData::TaughtAbilities.end(),
        [spellId](AscensionCompatData::TaughtAbility const& entry) { return entry.ParentSpellId == spellId; });
}

uint32 SyncBorrowedTaughtAbilities(Player* player, uint8 subClass, Subclass::Choice const& choice)
{
    uint32 changed = 0;
    for (AscensionCompatData::TaughtAbility const& entry : AscensionCompatData::TaughtAbilities)
    {
        if (entry.ClassId != subClass || entry.ClassId == player->getClass())
            continue;
        SpellInfo const* info = sSpellMgr->GetSpellInfo(entry.SpellId);
        bool const taught = TaughtToSubclass(player, choice, entry);
        if (taught && info && !player->HasSpell(entry.SpellId))
            player->learnSpell(entry.SpellId, false);
        else if (!taught && player->HasSpell(entry.SpellId) && InBorrowedFamily(player, subClass, info))
            player->removeSpell(entry.SpellId, SPEC_MASK_ALL, false);
        else
            continue;
        ++changed;
    }
    return changed;
}

uint32 ForgetBorrowedBaseSpells(Player* player, uint8 subClass)
{
    if (IsStandardCatalogClass(subClass))
    {
        ForgetStandardWeapons(player, PlayableClassOf(subClass));
        return ForgetStandardKit(player, PlayableClassOf(subClass));
    }
    ForgetCoAWeapons(player, subClass);
    uint32 removed = SyncBorrowedTaughtAbilities(player, subClass, {});
    for (AscensionCompatData::ClassSpell const& spell : AscensionCompatData::ClassSpells)
        if (spell.ClassId == subClass && player->HasSpell(spell.SpellId) && !OwnClassGrants(player, spell.SpellId))
        {
            player->removeSpell(spell.SpellId, SPEC_MASK_ALL, false);
            ++removed;
        }
    return removed;
}

bool IsClassSkillLine(uint16 skillId)
{
    SkillLineEntry const* skillLine = sSkillLineStore.LookupEntry(skillId);
    return skillLine && skillLine->categoryId == SKILL_CATEGORY_CLASS;
}

bool OwnClassSkill(Player const* player, uint16 skillId)
{
    return std::any_of(AscensionLiveBaseline::Skills.begin(), AscensionLiveBaseline::Skills.end(),
        [player, skillId](AscensionLiveBaseline::Skill const& skill)
        {
            return skill.ClassId == player->getClass() && skill.SkillId == skillId;
        });
}

void ForgetBorrowedSkillLines(Player* player, uint8 subClass)
{
    for (AscensionLiveBaseline::Skill const& skill : AscensionLiveBaseline::Skills)
        if (skill.ClassId == subClass && IsClassSkillLine(skill.SkillId) && player->HasSkill(skill.SkillId) &&
            !OwnClassSkill(player, skill.SkillId))
            player->SetSkill(skill.SkillId, 0, 0, 0);
}

void SyncBorrowedSkillLines(Player* player, uint8 subClass)
{
    for (AscensionLiveBaseline::Skill const& skill : AscensionLiveBaseline::Skills)
        if (skill.ClassId == subClass && IsClassSkillLine(skill.SkillId) && !player->HasSkill(skill.SkillId))
            player->SetSkill(skill.SkillId, 0, skill.Rank, skill.Maximum);
}

constexpr float BorrowedFocus = 100.0f;
std::unordered_map<uint32, bool> focusLent;

void RefreshPowerPools(Player* player)
{
    bool const lend = player->getClass() != CLASS_RANGER && ChoiceOf(player).SubClass == CLASS_RANGER &&
        !OpenTreesOf(player).empty();
    bool lent = false;
    {
        std::lock_guard<std::mutex> lock(subclassMutex);
        lent = focusLent[player->GetGUID().GetCounter()];
        focusLent[player->GetGUID().GetCounter()] = lend;
    }
    if (lend != lent)
        player->HandleStatFlatModifier(UNIT_MOD_FOCUS, TOTAL_VALUE, BorrowedFocus, lend);
    for (Powers power : { POWER_FOCUS, POWER_ENERGY, POWER_RUNIC_POWER, POWER_RAGE })
        player->UpdateMaxPower(power);
    SyncBorrowedMana(player, 0);
}

void RestoreSavedPowers(Player* player)
{
    QueryResult result = CharacterDatabase.Query("SELECT `power1`, `power2`, `power3`, `power4`, `power5`, `power6`, "
        "`power7` FROM `characters` WHERE `guid` = {}", player->GetGUID().GetCounter());
    if (!result)
        return;
    Field* fields = result->Fetch();
    for (uint8 index = 0; index < MAX_POWERS; ++index)
    {
        Powers const power = Powers(index);
        uint32 const saved = std::min(fields[index].Get<uint32>(), player->GetMaxPower(power));
        if (saved > player->GetPower(power))
            player->SetPower(power, saved);
    }
}

constexpr uint32 KitPasses = 8;

std::vector<uint16> ClassSkillLinesOf(std::vector<uint32> const& spells)
{
    std::vector<uint16> lines;
    for (uint32 spellId : spells)
    {
        SkillLineAbilityMapBounds const bounds = sSpellMgr->GetSkillLineAbilityMapBounds(spellId);
        for (auto itr = bounds.first; itr != bounds.second; ++itr)
            if (uint16 const skill = uint16(itr->second->SkillLine); IsClassSkillLine(skill) &&
                std::find(lines.begin(), lines.end(), skill) == lines.end())
                lines.push_back(skill);
    }
    return lines;
}

void LearnStandardSkillLines(Player* player, uint8 playableClass)
{
    for (uint16 skill : ClassSkillLinesOf(TaughtBy(ClassTrainerOf(playableClass))))
        if (!player->HasSkill(skill))
            player->SetSkill(skill, 0, 1, 1);
}

constexpr std::array<uint8, 10> ProficiencyRaces = { RACE_HUMAN, RACE_ORC, RACE_DWARF, RACE_NIGHTELF,
    RACE_UNDEAD_PLAYER, RACE_TAUREN, RACE_GNOME, RACE_TROLL, RACE_BLOODELF, RACE_DRAENEI };

bool ClassWields(uint8 playableClass, uint8 race, uint32 skill)
{
    if (GetSkillRaceClassInfo(skill, race, playableClass))
        return true;
    return std::any_of(ProficiencyRaces.begin(), ProficiencyRaces.end(), [playableClass, skill](uint8 other)
    {
        return GetSkillRaceClassInfo(skill, other, playableClass) != nullptr;
    });
}

std::vector<std::pair<uint32, uint32>> GearProficiencies()
{
    std::vector<std::pair<uint32, uint32>> found;
    for (SkillLineAbilityEntry const* ability : sSkillLineAbilityStore)
    {
        SkillLineEntry const* line = ability ? sSkillLineStore.LookupEntry(ability->SkillLine) : nullptr;
        bool const gear = line && (line->categoryId == SKILL_CATEGORY_WEAPON ||
            line->categoryId == SKILL_CATEGORY_ARMOR);
        SpellInfo const* spell = gear ? sSpellMgr->GetSpellInfo(ability->Spell) : nullptr;
        if (spell && (spell->HasEffect(SPELL_EFFECT_WEAPON) || spell->HasEffect(SPELL_EFFECT_PROFICIENCY)))
            found.emplace_back(ability->SkillLine, ability->Spell);
    }
    return found;
}

void LearnStandardWeapons(Player* player, uint8 playableClass)
{
    for (auto const& [skill, spellId] : GearProficiencies())
        if (!player->HasSpell(spellId) && ClassWields(playableClass, player->getRace(), skill))
            player->learnSpell(spellId, false);
}

void ForgetStandardWeapons(Player* player, uint8 playableClass)
{
    for (auto const& [skill, spellId] : GearProficiencies())
        if (player->HasSpell(spellId) && ClassWields(playableClass, player->getRace(), skill) &&
            !ClassWields(player->getClass(), player->getRace(), skill))
        {
            player->removeSpell(spellId, SPEC_MASK_ALL, false);
            player->SetSkill(uint16(skill), 0, 0, 0);
        }
}

bool CoAClassWields(uint8 subClass, uint32 skill)
{
    return std::any_of(AscensionLiveBaseline::Skills.begin(), AscensionLiveBaseline::Skills.end(),
        [subClass, skill](AscensionLiveBaseline::Skill const& baseline)
        {
            return baseline.ClassId == subClass && baseline.SkillId == skill;
        });
}

void LearnCoAWeapons(Player* player, uint8 subClass)
{
    if (IsAscensionCustomClassId(player->getClass()))
        return;
    for (auto const& [skill, spellId] : GearProficiencies())
        if (!player->HasSpell(spellId) && CoAClassWields(subClass, skill))
            player->learnSpell(spellId, false);
}

void ForgetCoAWeapons(Player* player, uint8 subClass)
{
    if (IsAscensionCustomClassId(player->getClass()))
        return;
    for (auto const& [skill, spellId] : GearProficiencies())
        if (player->HasSpell(spellId) && CoAClassWields(subClass, skill) &&
            !ClassWields(player->getClass(), player->getRace(), skill))
        {
            player->removeSpell(spellId, SPEC_MASK_ALL, false);
            player->SetSkill(uint16(skill), 0, 0, 0);
        }
}

uint32 DropUnwieldedGearSkills(Player* player)
{
    Subclass::Choice const choice = ChoiceOf(player);
    uint8 const subClass = choice.Complete() ? choice.SubClass : 0;
    uint32 dropped = 0;
    for (auto const& [skill, spellId] : GearProficiencies())
    {
        if (!player->HasSpell(spellId) || ClassWields(player->getClass(), player->getRace(), skill))
            continue;
        bool const borrowed = subClass && (IsStandardCatalogClass(subClass) ?
            ClassWields(PlayableClassOf(subClass), player->getRace(), skill) : CoAClassWields(subClass, skill));
        if (borrowed)
            continue;
        player->removeSpell(spellId, SPEC_MASK_ALL, false);
        if (player->HasSkill(uint16(skill)))
            player->SetSkill(uint16(skill), 0, 0, 0);
        ++dropped;
    }
    return dropped;
}

uint32 LearnStandardKit(Player* player, uint8 playableClass)
{
    LearnStandardSkillLines(player, playableClass);
    LearnStandardWeapons(player, playableClass);
    Trainer::Trainer const* trainer = ClassTrainerOf(playableClass);
    if (!trainer)
        return 0;
    uint32 learned = 0;
    for (uint32 pass = 0; pass < KitPasses; ++pass)
    {
        uint32 const before = learned;
        for (Trainer::Spell const& spell : trainer->GetSpells())
            if (trainer->CanTeachSpell(player, &spell))
            {
                if (spell.IsCastable())
                    player->CastSpell(player, spell.SpellId, true);
                else
                    player->learnSpell(spell.SpellId, false);
                ++learned;
            }
        if (learned == before)
            break;
    }
    return learned;
}

constexpr uint32 BookCopyOffset = 1100000;
constexpr uint32 BookTrainerId = 900100;

std::unordered_set<uint32> const& BookCopies()
{
    static std::unordered_set<uint32> const copies = []
    {
        std::unordered_set<uint32> loaded;
        auto const& trainers = sObjectMgr->GetTrainers();
        if (auto itr = trainers.find(BookTrainerId); itr != trainers.end())
            for (Trainer::Spell const& spell : itr->second.GetSpells())
                if (spell.SpellId > BookCopyOffset)
                    loaded.insert(spell.SpellId);
        return loaded;
    }();
    return copies;
}

uint32 BookCopyOf(uint32 spellId)
{
    return spellId && spellId < BookCopyOffset && BookCopies().contains(spellId + BookCopyOffset) ?
        spellId + BookCopyOffset : 0;
}

uint32 PreferBorrowedBookCopies(Player* player)
{
    std::vector<uint32> stock;
    for (auto const& [spellId, known] : player->GetSpellMap())
        if (known->State != PLAYERSPELL_REMOVED && BookCopyOf(spellId) && IsBorrowedSpell(player, spellId))
            stock.push_back(spellId);
    for (uint32 spellId : stock)
    {
        uint32 const copy = BookCopyOf(spellId);
        if (!player->HasSpell(copy) && sSpellMgr->GetSpellInfo(copy))
            player->learnSpell(copy, false);
        if (player->HasSpell(copy))
            player->removeSpell(spellId, SPEC_MASK_ALL, false);
    }
    return stock.size();
}

uint32 SyncBorrowedBaseSpells(Player* player)
{
    if (OpenTreesOf(player).empty())
        return 0;
    uint8 const subClass = ChoiceOf(player).SubClass;
    SyncBorrowedSkillLines(player, subClass);
    if (IsStandardCatalogClass(subClass))
    {
        uint32 const learned = LearnStandardKit(player, PlayableClassOf(subClass));
        PreferBorrowedBookCopies(player);
        return learned;
    }
    LearnCoAWeapons(player, subClass);
    uint32 learned = GrantSpecializationPresets(player);
    for (AscensionCompatData::ClassSpell const& spell : AscensionCompatData::ClassSpells)
        if (spell.ClassId == subClass && spell.RequiredLevel <= player->GetLevel() &&
            !player->HasSpell(spell.SpellId) && sSpellMgr->GetSpellInfo(spell.SpellId))
        {
            player->learnSpell(spell.SpellId, false);
            ++learned;
        }
    return learned + SyncBorrowedTaughtAbilities(player, subClass, ChoiceOf(player));
}

uint32 ApplyChoice(Player* player, Subclass::Choice const& wanted)
{
    Record record = RecordOf(player);
    uint32 removed = record.Choice == wanted ? 0 : ForgetBorrowedTrees(player, record.Choice);
    if (record.Choice.SubClass && record.Choice.SubClass != wanted.SubClass)
    {
        removed += ForgetBorrowedBaseSpells(player, record.Choice.SubClass);
        ForgetBorrowedSkillLines(player, record.Choice.SubClass);
    }
    record.Choice = wanted;
    StoreRecord(player, record);
    LendRunes(player);
    SyncBorrowedBaseSpells(player);
    RefreshPowerPools(player);
    if (uint32 const active = GetAscensionActiveSpecialization(player))
        SwitchAscensionSpecialization(player, active);
    GrantSpecializationPresets(player);
    SendSubclassState(player);
    return removed;
}

void AddInsight(Player* player)
{
    Record record = RecordOf(player);
    record.Insights = 1;
    StoreRecord(player, record);
    SendSubclassState(player);
}

std::string Describe(Subclass::Choice const& choice)
{
    if (choice.Empty())
        return "none";
    std::string trees;
    for (Subclass::Tree const& tree : BorrowedTreesOf(choice))
        trees += (trees.empty() ? "" : ", ") + (tree.SpecId ? SpecializationName(tree.SpecId) :
            std::string("class tree"));
    return Acore::StringFormat("{} ({})", ClassName(choice.SubClass), trees.empty() ? "no trees" : trees);
}

bool OffersQuest(Player const* player)
{
    if (!MainMaySubclass(player->getClass()) || LonelyTwin(player))
        return false;
    Record const record = RecordOf(player);
    Subclass::Settings const current = SettingsOf(player);
    bool const untouched = player->GetQuestStatus(current.QuestId) == QUEST_STATUS_NONE;
    std::lock_guard<std::mutex> lock(subclassMutex);
    return Subclass::OffersQuest(current, pairs, CharacterOf(player), record.Choice, record.Insights, untouched);
}

void ShowPrompt(Player* player, ObjectGuid source)
{
    Voice const voice = VoiceOf(player);
    ClearGossipMenuFor(player);
    AddGossipItemFor(player, GOSSIP_ICON_CHAT, voice.AcceptOption, SenderPrompt, ActionAccept);
    AddGossipItemFor(player, GOSSIP_ICON_CHAT, voice.LaterOption, SenderPrompt, ActionLater);
    player->PlayerTalkClass->GetGossipMenu().SetMenuId(PromptMenuId);
    SendGossipMenuFor(player, voice.PromptText, source);
}

void NoteQuestOffer(Player* player)
{
    bool const offered = OffersQuest(player);
    SendAddon(player, Acore::StringFormat("QUESTOFFER {}", offered ? 1 : 0));
    ShowQuestOnTwin(player, offered);
}

void ScheduleQuestOffer(Player* player, Milliseconds delay)
{
    ObjectGuid const guid = player->GetGUID();
    player->m_Events.AddEventAtOffset([player, guid]()
    {
        if (player->IsInWorld() && player->GetGUID() == guid)
            NoteQuestOffer(player);
    }, delay);
}

void AcceptQuest(Player* player)
{
    CloseGossipMenuFor(player);
    Quest const* quest = sObjectMgr->GetQuestTemplate(SettingsOf(player).QuestId);
    if (!quest || !OffersQuest(player) || !player->CanTakeQuest(quest, true) || !player->CanAddQuest(quest, true))
        return;
    player->AddQuestAndCheckCompletion(quest, nullptr);
    ChatHandler(player->GetSession()).SendSysMessage(VoiceOf(player).Says(VoiceOf(player).Accepted));
    NoteQuestOffer(player);
}

bool OfferedAsSubclass(uint8 mainClass, uint8 catalogClassId)
{
    if (PlayableClassOf(catalogClassId) == mainClass ||
        !Subclass::KindAllows(settings.SubClasses, IsStandardCatalogClass(catalogClassId)))
        return false;
    if (!IsAscensionCustomClassId(mainClass))
        return true;
    return settings.StandardClasses || !AscensionCompatData::IsCoABorrowOnlyClass(catalogClassId);
}

void ShowClasses(Player* player, Creature* trainer)
{
    ClearGossipMenuFor(player);
    Subclass::Character const character = CharacterOf(player);
    {
        std::lock_guard<std::mutex> lock(subclassMutex);
        for (NamedClass const& named : Classes)
            if (uint8 const classId = CatalogIdOf(named); OfferedAsSubclass(character.Class, classId) &&
                Subclass::AllowsAnySpecializationOf(pairs, character, classId) && !SpecsOf(classId).empty())
                AddGossipItemFor(player, GOSSIP_ICON_TRAINER, ClassName(classId), SenderClass, classId);
    }
    SendGossipMenuFor(player, VoiceOf(player).ClassText, trainer->GetGUID());
}

void AddSpecializationItems(Player* player, uint8 classId)
{
    Subclass::Character const character = CharacterOf(player);
    std::string const relic = RelicName(SettingsOf(player).BookItemId);
    std::lock_guard<std::mutex> lock(subclassMutex);
    bool const lendsAll = LendsAllOf(classId);
    bool const picksTwo = !lendsAll && TreesFor(classId) >= 2;
    for (Subclass::Tree const& specialization : CatalogSpecializations())
    {
        if (specialization.ClassId != classId ||
            !Subclass::AllowsSubclass(pairs, character, classId, specialization.SpecId))
            continue;
        std::string const text = lendsAll ? Acore::StringFormat("{}: all specializations", ClassName(classId)) :
            Acore::StringFormat("{}: {}", ClassName(classId), SpecializationName(specialization.SpecId));
        if (picksTwo)
            AddGossipItemFor(player, GOSSIP_ICON_TRAINER, text, SenderSpec, specialization.SpecId);
        else
            player->PlayerTalkClass->GetGossipMenu().AddMenuItem(-1, GOSSIP_ICON_TRAINER, text, SenderSpec,
                specialization.SpecId, Acore::StringFormat("Spend one insight of the {} on {}? Talents from an "
                "earlier choice are removed.", relic, text), 0);
        if (lendsAll)
            return;
    }
}

bool WantsSecondSpecialization(uint16 specId)
{
    uint8 const classId = ClassOfSpecialization(specId);
    std::lock_guard<std::mutex> lock(subclassMutex);
    return !LendsAllOf(classId) && TreesFor(classId) >= 2;
}

void ShowSecondSpecializations(Player* player, Creature* trainer, uint16 firstSpecId)
{
    ClearGossipMenuFor(player);
    uint8 const classId = ClassOfSpecialization(firstSpecId);
    Subclass::Character const character = CharacterOf(player);
    std::string const relic = RelicName(SettingsOf(player).BookItemId);
    {
        std::lock_guard<std::mutex> lock(subclassMutex);
        for (Subclass::Tree const& specialization : CatalogSpecializations())
        {
            if (specialization.ClassId != classId || specialization.SpecId == firstSpecId ||
                !Subclass::AllowsSubclass(pairs, character, classId, specialization.SpecId))
                continue;
            std::string const text = Acore::StringFormat("{}: {} and {}", ClassName(classId),
                SpecializationName(firstSpecId), SpecializationName(specialization.SpecId));
            player->PlayerTalkClass->GetGossipMenu().AddMenuItem(-1, GOSSIP_ICON_TRAINER, text, SenderSecondSpec,
                (uint32(firstSpecId) << 16) | specialization.SpecId, Acore::StringFormat("Spend one insight of the "
                "{} on {}? Talents from an earlier choice are removed.", relic, text), 0);
        }
    }
    SendGossipMenuFor(player, VoiceOf(player).SpecText, trainer->GetGUID());
}

void ShowSpecializations(Player* player, Creature* trainer, uint8 classId)
{
    ClearGossipMenuFor(player);
    AddSpecializationItems(player, classId);
    SendGossipMenuFor(player, VoiceOf(player).SpecText, trainer->GetGUID());
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
        allowed = OfferedAsSubclass(player->getClass(), classId) &&
            (!secondSpecId || TreesFor(classId) >= 2) &&
            Subclass::AllowsChoice(pairs, CharacterOf(player), wanted) &&
            !BorrowedTreesOf(wanted).empty() && player->GetLevel() >= settings.MinLevel;
    }
    if (!record.Insights || !allowed || !IsSpecializationOf(classId, specId) ||
        (secondSpecId && !IsSpecializationOf(classId, secondSpecId)))
    {
        handler.SendSysMessage(VoiceOf(player).Refused);
        return;
    }

    uint32 const removed = ApplyChoice(player, wanted);
    Record spent = RecordOf(player);
    spent.Insights = record.Insights - 1;
    StoreRecord(player, spent);
    handler.SendSysMessage(VoiceOf(player).Says(VoiceOf(player).Chosen));
    handler.PSendSysMessage("Learned the ways of the {}. {} talent spell(s) of the old way were forgotten. "
        "Spend points in the Two-Head or Two-Thins tab, or type /subclass.", Describe(wanted), removed);
    LOG_INFO("module", "mod-subclass: {} chose {} at a trainer", player->GetName(), Describe(wanted));
}

void GrantBook(Player* player, uint32 bookItemId)
{
    if (RecordOf(player).Insights || player->HasItemCount(bookItemId, 1, true) || !player->AddItem(bookItemId, 1))
        return;
    ChatHandler(player->GetSession()).SendSysMessage(VoiceOf(player).Says(VoiceOf(player).Found));
}
}

bool LendsTankSpecialization(Player const* player)
{
    Subclass::Choice const choice = ChoiceOf(player);
    if (!choice.Complete() || AscensionCompatData::IsCoABorrowOnlyClass(choice.SubClass))
        return false;
    std::lock_guard<std::mutex> lock(subclassMutex);
    return tankSpecializations.contains(choice.SubSpec);
}

bool LendsRangedSpecialization(Player const* player)
{
    Subclass::Choice const choice = ChoiceOf(player);
    if (!choice.Complete())
        return false;
    std::lock_guard<std::mutex> lock(subclassMutex);
    return distantSpecializations.contains(choice.SubSpec);
}

void RefreshSubclassResources(Player* player)
{
    RefreshPowerPools(player);
    SendSubclassState(player);
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
        std::string const classModel = sConfigMgr->GetOption<std::string>("CoA.ClassModel", "coa");
        std::vector<std::string_view> const models = Acore::Tokenize(classModel, ' ', false);
        loaded.StandardMains = sConfigMgr->GetOption<bool>("Subclass.StandardMains", false) &&
            std::find(models.begin(), models.end(), "coa") != models.end();
        auto kinds = [](char const* name)
        {
            return Subclass::ClassKinds(std::min<uint32>(sConfigMgr->GetOption<uint32>(name, 0), 2));
        };
        auto trees = [](char const* name, uint32 fallback)
        {
            return uint8(std::clamp<uint32>(sConfigMgr->GetOption<uint32>(name, fallback), 1, 5));
        };
        loaded.Mains = kinds("Subclass.MainClasses");
        loaded.SubClasses = kinds("Subclass.SubClasses");
        loaded.StandardTrees = trees("Subclass.StandardTrees", 3);
        loaded.CoATrees = trees("Subclass.CoATrees", 1);
        loaded.ThinHuman = sConfigMgr->GetOption<bool>("Subclass.ThinHuman.Enable", true);
        loaded.TwoHeadedOgre = sConfigMgr->GetOption<bool>("Subclass.TwoHeadedOgre.Enable", true);
        loaded.ForceBots = sConfigMgr->GetOption<bool>("Subclass.Bots.Force", false);
        loaded.TravelFormsWholeBody = sConfigMgr->GetOption<bool>("Subclass.TravelForms.WholeBody", true);
        thinHumanEnabled = loaded.ThinHuman;
        twoHeadedOgreEnabled = loaded.TwoHeadedOgre;
        forceBots = loaded.ForceBots;
        travelFormsWholeBody = loaded.TravelFormsWholeBody;
        std::lock_guard<std::mutex> lock(subclassMutex);
        settings = loaded;
    }

    void OnStartup() override
    {
        LoadPairs();
        LoadStories();
        LoadSpecializationRoles();
        LoadTalentLinks();
    }
};

bool ForcedBot(Player const* player)
{
    WorldSession const* session = player->GetSession();
    return forceBots && session && session->IsBot();
}

bool SubclassEligible(Player const* player)
{
    return IsEligible(player);
}

std::vector<Subclass::Choice> BotChoicesFor(Player const* player)
{
    uint8 const mainClass = player->getClass();
    Subclass::Character const character = CharacterOf(player);
    std::vector<uint8> classes;
    for (Subclass::Tree const& specialization : CatalogSpecializations())
        if (std::find(classes.begin(), classes.end(), specialization.ClassId) == classes.end())
            classes.push_back(specialization.ClassId);
    std::vector<Subclass::Choice> options;
    std::lock_guard<std::mutex> lock(subclassMutex);
    for (uint8 classId : classes)
    {
        if (!OfferedAsSubclass(mainClass, classId))
            continue;
        std::vector<uint16> const specs = SpecsOf(classId);
        bool const lendsAll = LendsAllOf(classId);
        bool const twoSpecs = !lendsAll && TreesFor(classId) >= 2;
        for (std::size_t first = 0; first < specs.size(); ++first)
        {
            if (twoSpecs)
                for (std::size_t second = first + 1; second < specs.size(); ++second)
                    options.push_back({ classId, specs[first], specs[second] });
            else
                options.push_back({ classId, specs[first], 0 });
            if (lendsAll)
                break;
        }
    }
    std::erase_if(options, [&character](Subclass::Choice const& choice)
    {
        return BorrowedTreesOf(choice).empty() || !Subclass::AllowsChoice(pairs, character, choice);
    });
    return options;
}

Subclass::Choice SubclassChoiceOf(Player const* player)
{
    return ChoiceOf(player);
}

void ApplySubclassChoice(Player* player, Subclass::Choice const& choice)
{
    ApplyChoice(player, choice);
}

std::vector<AscensionCompatData::CoATalentEntry const*> OpenBorrowedEntries(Player const* player)
{
    std::vector<Subclass::Tree> const trees = OpenTreesOf(player);
    std::vector<AscensionCompatData::CoATalentEntry const*> entries;
    for (AscensionCompatData::CoATalentEntry const& entry : AscensionCompatData::CoATalentEntries)
        if (InTrees(trees, entry))
            entries.push_back(&entry);
    return entries;
}

std::string DescribeSubclassChoice(Subclass::Choice const& choice)
{
    return Describe(choice);
}

uint8 SubclassMinLevel()
{
    return CurrentSettings().MinLevel;
}

std::string SubclassClassName(uint8 classId)
{
    return PlainClassName(CatalogClassOf(classId));
}

void SendSubclassMessage(Player* player, std::string const& message)
{
    SendAddon(player, message);
}

bool NeedsBorrowedSkill(Player const* player, ItemTemplate const* proto)
{
    uint32 const skill = proto ? proto->GetSkill() : 0;
    return skill && !ClassWields(player->getClass(), player->getRace(), skill);
}

uint32 StowBorrowedGear(Player* player)
{
    if (!SubclassResting(player))
        return 0;
    uint32 stowed = 0;
    for (uint8 slot = EQUIPMENT_SLOT_START; slot < EQUIPMENT_SLOT_END; ++slot)
    {
        Item* item = player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
        if (!item || !NeedsBorrowedSkill(player, item->GetTemplate()))
            continue;
        ItemPosCountVec destination;
        if (player->CanStoreItem(NULL_BAG, NULL_SLOT, destination, item, false) == EQUIP_ERR_OK)
        {
            player->RemoveItem(INVENTORY_SLOT_BAG_0, slot, true);
            player->StoreItem(destination, item, true);
        }
        else
        {
            player->MoveItemFromInventory(INVENTORY_SLOT_BAG_0, slot, true);
            CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
            item->DeleteFromInventoryDB(trans);
            item->SaveToDB(trans);
            MailDraft("Put away while your heads agree", "Your bags were full, so this came by mail.").AddItem(item)
                .SendMailTo(trans, player, MailSender(player, MAIL_STATIONERY_GM), MAIL_CHECK_MASK_COPIED);
            CharacterDatabase.CommitTransaction(trans);
        }
        ++stowed;
    }
    if (stowed)
        ChatHandler(player->GetSession()).PSendSysMessage("Your heads agree: {} piece(s) of the other head's gear "
            "put away.", stowed);
    return stowed;
}

bool SubclassChosen(Player const* player)
{
    return RecordOf(player).Choice.Complete();
}

void OfferStoryLater(Player* player)
{
    ScheduleQuestOffer(player, 2s);
}

bool SubclassQuestOffered(Player const* player)
{
    return OffersQuest(player);
}

void ShowSubclassPrompt(Player* player, ObjectGuid source)
{
    ShowPrompt(player, source);
}

bool AnswerSubclassPrompt(Player* player, uint32 sender, uint32 action)
{
    if (sender != SenderPrompt)
        return false;
    if (action == ActionAccept)
        AcceptQuest(player);
    else
    {
        CloseGossipMenuFor(player);
        ChatHandler(player->GetSession()).SendSysMessage(VoiceOf(player).Says(VoiceOf(player).Later));
    }
    return true;
}

bool UsesBow(SpellInfo const* info)
{
    constexpr uint32 Shooters = (1 << ITEM_SUBCLASS_WEAPON_BOW) | (1 << ITEM_SUBCLASS_WEAPON_GUN) |
        (1 << ITEM_SUBCLASS_WEAPON_CROSSBOW);
    return info && (info->HasAttribute(SPELL_ATTR0_USES_RANGED_SLOT) ||
        (info->EquippedItemClass == ITEM_CLASS_WEAPON && (info->EquippedItemSubClassMask & Shooters)));
}

bool SharedByManyClasses(uint32 spellId)
{
    constexpr std::size_t ManyClasses = 3;
    std::size_t classes = 0;
    for (AscensionCompatData::ClassSpell const& spell : AscensionCompatData::ClassSpells)
        if (spell.SpellId == spellId)
            ++classes;
    return classes >= ManyClasses;
}

bool ClassShoots(uint8 classId)
{
    static std::mutex shootersMutex;
    static std::unordered_map<uint8, bool> shooters;
    std::lock_guard<std::mutex> lock(shootersMutex);
    if (auto known = shooters.find(classId); known != shooters.end())
        return known->second;
    uint32 because = 0;
    auto consider = [&because](uint32 spellId)
    {
        if (!because && spellId && !SharedByManyClasses(spellId) && UsesBow(sSpellMgr->GetSpellInfo(spellId)))
            because = spellId;
    };
    for (AscensionCompatData::ClassSpell const& spell : AscensionCompatData::ClassSpells)
        if (spell.ClassId == classId)
            consider(spell.SpellId);
    for (AscensionCompatData::CoATalentEntry const& entry : AscensionCompatData::CoATalentEntries)
        if (entry.ClassId == classId)
            for (uint32 spellId : entry.SpellIds)
                consider(spellId);
    bool const shoots = PlayableClassOf(classId) == CLASS_HUNTER || because;
    LOG_INFO("module", "Class {} {} a bow{}", uint32(classId), shoots ? "uses" : "does not use",
        because ? Acore::StringFormat(" (spell {})", because) : std::string());
    shooters.emplace(classId, shoots);
    return shoots;
}

bool OtherHalfShoots(Player const* player)
{
    Subclass::Choice const choice = ChoiceOf(player);
    return choice.Complete() && ClassShoots(choice.SubClass);
}

bool MainHalfShoots(Player const* player)
{
    return ClassShoots(player->getClass());
}

uint32 ClassFamily(uint8 classId)
{
    switch (PlayableClassOf(classId))
    {
        case CLASS_WARRIOR: return SPELLFAMILY_WARRIOR;
        case CLASS_PALADIN: return SPELLFAMILY_PALADIN;
        case CLASS_HUNTER: return SPELLFAMILY_HUNTER;
        case CLASS_ROGUE: return SPELLFAMILY_ROGUE;
        case CLASS_PRIEST: return SPELLFAMILY_PRIEST;
        case CLASS_DEATH_KNIGHT: return SPELLFAMILY_DEATHKNIGHT;
        case CLASS_SHAMAN: return SPELLFAMILY_SHAMAN;
        case CLASS_MAGE: return SPELLFAMILY_MAGE;
        case CLASS_WARLOCK: return SPELLFAMILY_WARLOCK;
        case CLASS_DRUID: return SPELLFAMILY_DRUID;
        default: break;
    }
    static std::mutex familiesMutex;
    static std::unordered_map<uint8, uint32> families;
    std::lock_guard<std::mutex> lock(familiesMutex);
    if (auto known = families.find(classId); known != families.end())
        return known->second;
    std::unordered_map<uint32, uint32> counts;
    for (AscensionCompatData::ClassSpell const& spell : AscensionCompatData::ClassSpells)
        if (spell.ClassId == classId)
            if (SpellInfo const* info = sSpellMgr->GetSpellInfo(spell.SpellId); info && info->SpellFamilyName)
                ++counts[info->SpellFamilyName];
    uint32 family = 0;
    uint32 most = 0;
    for (auto const& [candidate, count] : counts)
        if (count > most)
        {
            family = candidate;
            most = count;
        }
    families.emplace(classId, family);
    return family;
}

bool InBorrowedFamily(Player const* player, uint8 subClass, SpellInfo const* info)
{
    uint32 const family = ClassFamily(subClass);
    return info && family && info->SpellFamilyName == family && family != ClassFamily(player->getClass());
}

Subclass::Offer BorrowedOffer(AscensionCompatData::CoATalentEntry const& entry)
{
    Subclass::Offer offer{ entry.EntryId, entry.SpellCount, entry.AECost, entry.TECost, entry.RequiredLevel,
        { entry.RequiredEntryIds[0], entry.RequiredEntryIds[1], entry.RequiredEntryIds[2] }, entry.SpecId };
    LinkOffer(offer);
    return offer;
}

Powers PowerBothHalvesUse(Player const* player)
{
    Subclass::Choice const choice = ChoiceOf(player);
    if (!choice.Complete() || SubclassResting(player) || OpenTreesOf(player).empty())
        return MAX_POWERS;
    Powers const formed = HalfFormPower(player);
    Powers const borrowed = formed != MAX_POWERS ? formed : SubclassPower(choice.SubClass);
    return borrowed == player->getPowerType() ? borrowed : MAX_POWERS;
}

void RefreshSubclassState(Player* player)
{
    SendSubclassState(player);
}

bool LendsMana(Player const* player)
{
    Subclass::Choice const choice = ChoiceOf(player);
    return choice.Complete() && SubclassPower(choice.SubClass) == POWER_MANA &&
        player->getPowerType() != POWER_MANA && !SubclassResting(player);
}

uint32 ClassBaseMana(uint8 classId, uint8 level)
{
    PlayerClassLevelInfo info;
    if (classId && classId < MAX_CLASSES && sChrClassesStore.LookupEntry(classId))
        sObjectMgr->GetPlayerClassLevelInfo(classId, level, &info);
    return info.basemana;
}

namespace
{
float SpiritRegenRatio(uint8 classId, uint8 level)
{
    if (!classId || !level)
        return 0.0f;
    GtRegenMPPerSptEntry const* entry = sGtRegenMPPerSptStore.LookupEntry((uint32(classId) - 1) * GT_MAX_LEVEL +
        std::min<uint32>(level, GT_MAX_LEVEL) - 1);
    return entry ? entry->ratio : 0.0f;
}

std::unordered_map<uint32, float> lentManaCarried;

void RegenerateLentMana(Player* player, uint32 milliseconds)
{
    uint8 const level = player->GetLevel();
    float const ratio = SpiritRegenRatio(PlayableClassOf(ChoiceOf(player).SubClass), level) -
        SpiritRegenRatio(player->getClass(), level);
    if (ratio <= 0.0f || !milliseconds || !player->IsAlive() ||
        player->GetPower(POWER_MANA) >= player->GetMaxPower(POWER_MANA) ||
        player->HasAuraTypeWithMiscvalue(SPELL_AURA_PREVENT_REGENERATE_POWER, POWER_MANA + 1) ||
        !sScriptMgr->OnPlayerCanRegenerate(player, POWER_MANA))
        return;
    float perSecond = std::sqrt(player->GetStat(STAT_INTELLECT)) * player->GetStat(STAT_SPIRIT) * ratio *
        player->GetTotalAuraMultiplierByMiscValue(SPELL_AURA_MOD_POWER_REGEN_PERCENT, POWER_MANA);
    if (player->IsUnderLastManaUseEffect())
        perSecond = CalculatePct(perSecond, std::min(player->GetTotalAuraModifier(SPELL_AURA_MOD_MANA_REGEN_INTERRUPT),
            100));
    int32 gained = 0;
    {
        std::lock_guard<std::mutex> lock(subclassMutex);
        float& carried = lentManaCarried[player->GetGUID().GetCounter()];
        carried += perSecond * sWorld->getRate(RATE_POWER_MANA) * milliseconds / IN_MILLISECONDS;
        gained = int32(carried);
        carried -= float(gained);
    }
    if (gained > 0)
        player->ModifyPower(POWER_MANA, gained);
}
}

namespace
{
std::unordered_map<uint32, std::array<int32, MAX_STATS>> standInStats;

Optional<Stats> StatOfItemMod(uint8 itemMod)
{
    switch (itemMod)
    {
        case ITEM_MOD_STRENGTH: return STAT_STRENGTH;
        case ITEM_MOD_AGILITY: return STAT_AGILITY;
        case ITEM_MOD_STAMINA: return STAT_STAMINA;
        case ITEM_MOD_INTELLECT: return STAT_INTELLECT;
        case ITEM_MOD_SPIRIT: return STAT_SPIRIT;
        default: return std::nullopt;
    }
}

std::vector<Stats> SpecializationStats(uint16 specId)
{
    std::vector<Stats> stats;
    for (AscensionCompatData::CoASpecialization const& specialization : AscensionCompatData::CoASpecializations)
        if (specId && specialization.SpecId == specId)
            for (uint8 itemMod : specialization.PrimaryStats)
                if (Optional<Stats> stat = StatOfItemMod(itemMod); stat && *stat != STAT_STAMINA)
                    stats.push_back(*stat);
    return stats;
}

std::array<int32, MAX_STATS> StandInsOf(Player const* player)
{
    std::lock_guard<std::mutex> lock(subclassMutex);
    auto const itr = standInStats.find(player->GetGUID().GetCounter());
    return itr == standInStats.end() ? std::array<int32, MAX_STATS>{} : itr->second;
}

float OwnStat(Player const* player, std::array<int32, MAX_STATS> const& standIns, Stats stat)
{
    return player->GetStat(stat) - float(standIns[stat]);
}

Stats MainStatOf(Player const* player, std::array<int32, MAX_STATS> const& standIns)
{
    for (Stats stat : SpecializationStats(uint16(GetAscensionActiveSpecialization(player))))
        if (stat == STAT_STRENGTH || stat == STAT_AGILITY || stat == STAT_INTELLECT)
            return stat;
    Stats best = STAT_STRENGTH;
    for (Stats stat : { STAT_AGILITY, STAT_INTELLECT })
        if (OwnStat(player, standIns, stat) > OwnStat(player, standIns, best))
            best = stat;
    return best;
}

std::vector<Stats> StatsTheSubclassNeeds(Player const* player)
{
    Subclass::Choice const choice = ChoiceOf(player);
    if (!choice.Complete() || SubclassResting(player) || OpenTreesOf(player).empty())
        return {};
    std::vector<Stats> needed = SpecializationStats(choice.SubSpec);
    for (Stats stat : SpecializationStats(choice.SecondSpec))
        needed.push_back(stat);
    if (SubclassPower(choice.SubClass) == POWER_MANA)
        needed.push_back(STAT_INTELLECT);
    std::sort(needed.begin(), needed.end());
    needed.erase(std::unique(needed.begin(), needed.end()), needed.end());
    return needed;
}

void SyncStandInStats(Player* player)
{
    std::array<int32, MAX_STATS> const applied = StandInsOf(player);
    std::array<int32, MAX_STATS> wanted{};
    if (std::vector<Stats> const needed = StatsTheSubclassNeeds(player); !needed.empty())
    {
        Stats const main = MainStatOf(player, applied);
        float const mainValue = OwnStat(player, applied, main);
        for (Stats stat : needed)
            if (stat != main)
                wanted[stat] = std::max(0, int32(mainValue - OwnStat(player, applied, stat)));
    }
    if (wanted == applied)
        return;
    for (uint8 stat = STAT_STRENGTH; stat < MAX_STATS; ++stat)
    {
        if (wanted[stat] == applied[stat])
            continue;
        UnitMods const mod = UnitMods(UNIT_MOD_STAT_START + stat);
        if (applied[stat])
            player->HandleStatFlatModifier(mod, TOTAL_VALUE, float(applied[stat]), false);
        if (wanted[stat])
            player->HandleStatFlatModifier(mod, TOTAL_VALUE, float(wanted[stat]), true);
    }
    std::lock_guard<std::mutex> lock(subclassMutex);
    standInStats[player->GetGUID().GetCounter()] = wanted;
}
}

void SyncBorrowedMana(Player* player, uint32 milliseconds)
{
    SyncStandInStats(player);
    bool const lends = LendsMana(player);
    uint8 const classId = lends ? PlayableClassOf(ChoiceOf(player).SubClass) : player->getClass();
    uint32 const wanted = ClassBaseMana(classId, player->GetLevel());
    if (player->GetCreateMana() != wanted)
    {
        player->SetCreateMana(wanted);
        player->UpdateMaxPower(POWER_MANA);
        if (player->GetPower(POWER_MANA) > player->GetMaxPower(POWER_MANA))
            player->SetPower(POWER_MANA, player->GetMaxPower(POWER_MANA));
    }
    if (lends)
        RegenerateLentMana(player, milliseconds);
}

bool IsBorrowedSpell(Player const* player, uint32 spellId)
{
    Record const record = RecordOf(player);
    if (!record.Choice.Complete() || OwnClassGrants(player, spellId))
        return false;
    if (InBorrowedFamily(player, record.Choice.SubClass, sSpellMgr->GetSpellInfo(spellId)))
        return true;
    for (AscensionCompatData::ClassSpell const& spell : AscensionCompatData::ClassSpells)
        if (spell.ClassId == record.Choice.SubClass && spell.SpellId == spellId)
            return true;
    std::vector<Subclass::Tree> const trees = BorrowedTreesOf(record.Choice);
    for (AscensionCompatData::CoATalentEntry const& entry : AscensionCompatData::CoATalentEntries)
        if (InTrees(trees, entry) &&
            std::find(entry.SpellIds.begin(), entry.SpellIds.end(), spellId) != entry.SpellIds.end())
            return true;
    return false;
}

std::vector<uint32> KnownBorrowedActives(Player const* player)
{
    std::vector<uint32> actives;
    Record const record = RecordOf(player);
    if (!record.Choice.Complete())
        return actives;
    auto consider = [&](uint32 spellId)
    {
        SpellInfo const* info = spellId ? sSpellMgr->GetSpellInfo(spellId) : nullptr;
        if (!info || info->IsPassive() || !player->HasSpell(spellId) || OwnClassGrants(player, spellId))
            return;
        uint32 const next = sSpellMgr->GetNextSpellInChain(spellId);
        if (next && player->HasSpell(next))
            return;
        if (std::find(actives.begin(), actives.end(), spellId) == actives.end())
            actives.push_back(spellId);
    };
    for (AscensionCompatData::ClassSpell const& spell : AscensionCompatData::ClassSpells)
        if (spell.ClassId == record.Choice.SubClass)
            consider(spell.SpellId);
    std::vector<Subclass::Tree> const trees = BorrowedTreesOf(record.Choice);
    for (AscensionCompatData::CoATalentEntry const& entry : AscensionCompatData::CoATalentEntries)
        if (InTrees(trees, entry))
            for (uint32 spellId : entry.SpellIds)
                consider(spellId);
    for (auto const& [spellId, known] : player->GetSpellMap())
        if (known->State != PLAYERSPELL_REMOVED && known->Active &&
            InBorrowedFamily(player, record.Choice.SubClass, sSpellMgr->GetSpellInfo(spellId)))
            consider(spellId);
    return actives;
}

std::vector<uint32> KnownBorrowedPassives(Player const* player)
{
    std::vector<uint32> passives;
    Record const record = RecordOf(player);
    if (!record.Choice.Complete())
        return passives;
    auto consider = [&](uint32 spellId)
    {
        SpellInfo const* info = spellId ? sSpellMgr->GetSpellInfo(spellId) : nullptr;
        if (info && info->IsPassive() && player->HasSpell(spellId) && !OwnClassGrants(player, spellId))
            passives.push_back(spellId);
    };
    for (AscensionCompatData::ClassSpell const& spell : AscensionCompatData::ClassSpells)
        if (spell.ClassId == record.Choice.SubClass)
            consider(spell.SpellId);
    std::vector<Subclass::Tree> const trees = BorrowedTreesOf(record.Choice);
    for (AscensionCompatData::CoATalentEntry const& entry : AscensionCompatData::CoATalentEntries)
        if (InTrees(trees, entry))
            for (uint32 spellId : entry.SpellIds)
                consider(spellId);
    return passives;
}

void SetBorrowedPassives(Player* player, bool active)
{
    for (uint32 spellId : KnownBorrowedPassives(player))
    {
        if (!active)
            player->RemoveAurasDueToSpell(spellId);
        else if (!player->HasAura(spellId))
            player->CastSpell(player, spellId, true);
    }
}

bool PressedByHand(Spell const* spell)
{
    return !spell->IsTriggered() && !spell->HasTriggeredCastFlag(TRIGGERED_COUNTS_AS_CAST);
}

class mod_subclass_spells : public AllSpellScript
{
public:
    mod_subclass_spells() : AllSpellScript("mod_subclass_spells", { ALLSPELLHOOK_ON_SPELL_CHECK_CAST }) { }

    void OnSpellCheckCast(Spell* spell, bool /*strict*/, SpellCastResult& res) override
    {
        Player* player = spell->GetCaster() ? spell->GetCaster()->ToPlayer() : nullptr;
        if (player && PressedByHand(spell) &&
            (res == SPELL_FAILED_ONLY_SHAPESHIFT || res == SPELL_FAILED_NOT_SHAPESHIFT) &&
            IsBorrowedSpell(player, spell->GetSpellInfo()->Id) &&
            OtherHalfFormAllows(player, spell->GetSpellInfo()))
        {
            PressForOtherHalf(player, spell->GetSpellInfo()->Id);
            res = SPELL_FAILED_DONT_REPORT;
            return;
        }
        if (res != SPELL_CAST_OK || !player || !PressedByHand(spell))
            return;
        if (CallsHunterPet(spell->GetSpellInfo()))
            BringBackParkedHunterPet(player);
        if (!IsBorrowedSpell(player, spell->GetSpellInfo()->Id))
            return;
        if (TakeHalfForm(player, spell->GetSpellInfo()))
        {
            res = SPELL_FAILED_DONT_REPORT;
            return;
        }
        if (HandlesOwnPet(spell->GetSpellInfo()))
        {
            if (SubclassResting(player))
            {
                res = SPELL_FAILED_DONT_REPORT;
                ChatHandler(player->GetSession()).SendNotification(
                    "That ability belongs to your other half, and they are not with you right now.");
            }
            return;
        }
        if (!SubclassResting(player))
        {
            Unit* target = spell->m_targets.GetUnitTarget();
            if (OrderBrother(player, spell->GetSpellInfo()->Id, target ? target : player))
                res = SPELL_FAILED_DONT_REPORT;
            return;
        }
        res = SPELL_FAILED_DONT_REPORT;
        ChatHandler(player->GetSession()).SendNotification(
            "That ability belongs to your other half, and they are not with you right now.");
    }
};

class mod_subclass_player : public PlayerScript
{
public:
    mod_subclass_player() : PlayerScript("mod_subclass_player", { PLAYERHOOK_ON_LOGIN, PLAYERHOOK_ON_LOGOUT,
        PLAYERHOOK_ON_CREATE, PLAYERHOOK_ON_DELETE, PLAYERHOOK_ON_LEVEL_CHANGED, PLAYERHOOK_ON_GOSSIP_SELECT,
        PLAYERHOOK_ON_QUEST_ABANDON, PLAYERHOOK_CAN_PLAYER_USE_PRIVATE_CHAT, PLAYERHOOK_ON_PLAYER_IS_CLASS,
        PLAYERHOOK_ON_PLAYER_HAS_ACTIVE_POWER_TYPE, PLAYERHOOK_ON_LEARN_SPELL, PLAYERHOOK_CAN_EQUIP_ITEM })
    {
    }

    bool OnPlayerCanEquipItem(Player* player, uint8 /*slot*/, uint16& /*dest*/, Item* item, bool /*swap*/,
        bool notLoading) override
    {
        if (!notLoading || !item || !SubclassResting(player) || !NeedsBorrowedSkill(player, item->GetTemplate()))
            return true;
        ChatHandler(player->GetSession()).SendSysMessage("Your heads agree right now; that belongs to the other one.");
        return false;
    }

    void OnPlayerLearnSpell(Player* player, uint32 spellId) override
    {
        if (IsTaughtAbilityParent(spellId))
            if (Subclass::Choice const choice = ChoiceOf(player); choice.Complete())
                SyncBorrowedTaughtAbilities(player, choice.SubClass, choice);
        if (player->IsInWorld() && StanceLike(sSpellMgr->GetSpellInfo(spellId)))
            SendAddon(player, BorrowedStances(player));
    }

    bool OnPlayerHasActivePowerType(Player const* player, Powers power) override
    {
        Subclass::Choice const choice = ChoiceOf(player);
        return choice.Complete() && player->getPowerType() != power && !SubclassResting(player) &&
            (SubclassPower(choice.SubClass) == power || HalfFormPower(player) == power);
    }

    Optional<bool> OnPlayerIsClass(Player const* player, ::Classes playerClass, ClassContext context) override
    {
        switch (context)
        {
            case CLASS_CONTEXT_ABILITY:
            case CLASS_CONTEXT_ABILITY_REACTIVE:
            case CLASS_CONTEXT_PET:
            case CLASS_CONTEXT_PET_CHARM:
                break;
            default:
                return std::nullopt;
        }
        if (context == CLASS_CONTEXT_ABILITY && playerClass != player->getClass() &&
            IsAscensionCustomClassId(playerClass) && UsesAscensionClassMechanics(player, playerClass))
            return true;
        if (playerClass < CLASS_WARRIOR || playerClass > CLASS_DRUID || playerClass == player->getClass())
            return std::nullopt;
        if (playerClass == CLASS_DEATH_KNIGHT && raisingRunes)
            return true;
        if (playerClass == CLASS_DEATH_KNIGHT && !HasLentRunes(player))
            return std::nullopt;
        Subclass::Choice const choice = ChoiceOf(player);
        if (!choice.Complete() || PlayableClassOf(choice.SubClass) != playerClass || SubclassResting(player))
            return std::nullopt;
        return true;
    }

    void OnPlayerLogin(Player* player) override
    {
        TrimChoice(player);
        AssignBotSubclass(player);
        LearnStandardMainWeaponSkills(player);
        LendRunes(player);
        SyncBorrowedBaseSpells(player);
        if (SubclassRaceEnabled(player->getRace()))
            DropUnwieldedGearSkills(player);
        RefreshPowerPools(player);
        RestoreSavedPowers(player);
        ScheduleQuestOffer(player, 5s);
    }

    void OnPlayerLevelChanged(Player* player, uint8 oldLevel) override
    {
        if (oldLevel < CurrentSettings().MinLevel)
            ScheduleQuestOffer(player, 1s);
        SyncBorrowedBaseSpells(player);
        SpendBotPoints(player);
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
        if (menuId == PromptMenuId)
            AnswerSubclassPrompt(player, sender, action);
    }

    void OnPlayerQuestAbandon(Player* player, uint32 questId) override
    {
        if (questId == SettingsOf(player).QuestId)
            ScheduleQuestOffer(player, 1s);
    }

    void OnPlayerLogout(Player* player) override
    {
        ForgetBotPlayer(player);
        std::lock_guard<std::mutex> lock(subclassMutex);
        records.erase(player->GetGUID().GetCounter());
        focusLent.erase(player->GetGUID().GetCounter());
        lentManaCarried.erase(player->GetGUID().GetCounter());
        standInStats.erase(player->GetGUID().GetCounter());
        runesLent.erase(player->GetGUID().GetCounter());
    }

    void OnPlayerCreate(Player* player) override
    {
        ForgetSubclassOf(player->GetGUID().GetCounter());
    }

    void OnPlayerDelete(ObjectGuid guid, uint32 /*accountId*/) override
    {
        ForgetSubclassOf(guid.GetCounter());
    }

private:
    static void TrimChoice(Player* player)
    {
        Subclass::Choice const choice = ChoiceOf(player);
        if (choice.Empty())
            return;

        Subclass::Character const character = CharacterOf(player);
        Subclass::Choice kept;
        if (MayCarrySubclass(player) && IsSpecializationOf(choice.SubClass, choice.SubSpec) &&
            PlayableClassOf(choice.SubClass) != character.Class)
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

class item_subclass_relic : public ItemScript
{
public:
    explicit item_subclass_relic(char const* scriptName) : ItemScript(scriptName) { }

    bool OnUse(Player* player, Item* item, SpellCastTargets const& /*targets*/) override
    {
        ChatHandler handler(player->GetSession());
        uint32 const bookItemId = item->GetEntry();
        Subclass::Settings const current = SettingsOf(player);
        if (!IsEligible(player) || current.BookItemId != bookItemId)
        {
            handler.SendSysMessage(VoiceOfRelic(bookItemId).Unreadable);
            return true;
        }

        Voice const voice = VoiceOf(player);
        Quest const* quest = sObjectMgr->GetQuestTemplate(current.QuestId);
        QuestStatus const status = quest ? player->GetQuestStatus(quest->GetQuestId()) : QUEST_STATUS_NONE;
        if (status == QUEST_STATUS_COMPLETE && player->CanRewardQuest(quest, true))
            player->RewardQuest(quest, 0, player);
        else if (status == QUEST_STATUS_REWARDED)
        {
            if (RecordOf(player).Insights)
            {
                handler.SendSysMessage(voice.Says(voice.OneInsight));
                return true;
            }
            player->DestroyItemCount(bookItemId, 1, true);
        }
        else
        {
            handler.SendSysMessage(voice.Says(voice.NotYet));
            return true;
        }

        AddInsight(player);
        handler.SendSysMessage(voice.Says(voice.Used));
        return true;
    }
};

bool TeachesSubclass(Player const* player, Creature const* creature)
{
    Subclass::Choice const choice = ChoiceOf(player);
    if (!choice.Complete() || OpenTreesOf(player).empty())
        return false;
    Trainer::Trainer const* trainer = sObjectMgr->GetTrainer(creature->GetEntry());
    return trainer && trainer->GetTrainerType() == Trainer::Type::Class &&
        trainer->GetTrainerRequirement() == PlayableClassOf(choice.SubClass);
}

bool PaidBorrowedEntry(Player const* player, AscensionCompatData::CoATalentEntry const& entry,
    std::vector<Subclass::Tree> const& open)
{
    return (entry.AECost || entry.TECost) && InTrees(open, entry) && GetAscensionTalentRank(player, entry.EntryId) &&
        !IsPresetEntry(player, entry.EntryId);
}

bool SpendsBorrowedPoints(Player const* player)
{
    std::vector<Subclass::Tree> const open = OpenTreesOf(player);
    return std::any_of(AscensionCompatData::CoATalentEntries.begin(), AscensionCompatData::CoATalentEntries.end(),
        [player, &open](AscensionCompatData::CoATalentEntry const& entry)
        {
            return PaidBorrowedEntry(player, entry, open);
        });
}

uint32 ForgetPaidBorrowedTalents(Player* player)
{
    std::vector<Subclass::Tree> const open = OpenTreesOf(player);
    std::vector<uint32> const presets = PresetEntriesOf(player);
    uint32 removed = 0;
    for (AscensionCompatData::CoATalentEntry const& entry : AscensionCompatData::CoATalentEntries)
    {
        if (!(entry.AECost || entry.TECost) || !InTrees(open, entry) ||
            std::find(presets.begin(), presets.end(), entry.EntryId) != presets.end())
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

void UnlearnBorrowedTalentsAt(Player* player, Creature* creature)
{
    uint32 const cost = player->resetTalentsCost();
    if (!player->HasEnoughMoney(cost))
    {
        player->SendBuyError(BUY_ERR_NOT_ENOUGHT_MONEY, creature, 0, 0);
        return;
    }
    uint32 const removed = ForgetPaidBorrowedTalents(player);
    if (!removed)
    {
        ChatHandler(player->GetSession()).SendNotification("You have no subclass talents to unlearn.");
        return;
    }
    player->ModifyMoney(-int32(cost));
    SynchronizeAscensionTalentReplacements(player);
    creature->CastSpell(player, UntalentVisual, true);
    SendSubclassState(player);
    LOG_INFO("module", "mod-subclass: {} unlearned the subclass talents at {} ({} rank spell(s), {} copper)",
        player->GetName(), creature->GetName(), removed, cost);
}

void DropDualSpecOptions(Player* player)
{
    GossipMenu& menu = player->PlayerTalkClass->GetGossipMenu();
    std::vector<std::tuple<uint32, GossipMenuItem, std::optional<GossipMenuItemData>>> kept;
    for (auto const& [id, item] : menu.GetMenuItems())
        if (item.OptionType != GOSSIP_OPTION_LEARNDUALSPEC && item.OptionType != GOSSIP_OPTION_DUALSPEC_INFO)
        {
            GossipMenuItemData const* data = menu.GetItemData(id);
            kept.emplace_back(id, item, data ? std::optional<GossipMenuItemData>(*data) : std::nullopt);
        }
    if (kept.size() == menu.GetMenuItemCount())
        return;
    menu.ClearMenu();
    for (auto const& [id, item, data] : kept)
    {
        menu.AddMenuItem(int32(id), item.MenuItemIcon, item.Message, item.Sender, item.OptionType, item.BoxMessage,
            item.BoxMoney, item.IsCoded);
        if (data)
            menu.AddGossipMenuItemData(id, data->GossipActionMenuId, data->GossipActionPoi);
    }
}

class mod_subclass_trainers : public AllCreatureScript
{
public:
    mod_subclass_trainers() : AllCreatureScript("mod_subclass_trainers") { }

    bool CanCreatureGossipHello(Player* player, Creature* creature) override
    {
        if (!creature->HasNpcFlag(UNIT_NPC_FLAG_TRAINER_CLASS) || !IsEligible(player))
            return false;
        bool const insight = RecordOf(player).Insights != 0;
        bool const respec = TeachesSubclass(player, creature) && SpendsBorrowedPoints(player);
        player->PrepareGossipMenu(creature, creature->GetGossipMenuId(), true);
        DropDualSpecOptions(player);
        if (insight)
            AddGossipItemFor(player, GOSSIP_ICON_TRAINER, VoiceOf(player).TrainerOption, SenderTrainer, 0);
        if (respec)
            AddGossipItemFor(player, GOSSIP_ICON_CHAT, "I wish to unlearn my subclass Talents.", SenderUnlearnBorrowed,
                0, "Do you want to unlearn all your subclass talents? Your subclass and its specialization stay.",
                player->resetTalentsCost(), false);
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
                if (WantsSecondSpecialization(uint16(action)))
                    ShowSecondSpecializations(player, creature, uint16(action));
                else
                    ChooseAtTrainer(player, uint16(action), 0);
                return true;
            case SenderSecondSpec:
                ChooseAtTrainer(player, uint16(action >> 16), uint16(action & 0xFFFF));
                return true;
            case SenderUnlearnBorrowed:
                CloseGossipMenuFor(player);
                if (TeachesSubclass(player, creature))
                    UnlearnBorrowedTalentsAt(player, creature);
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

        for (auto const& reference : map->GetPlayers())
        {
            Player* player = reference.GetSource();
            if (!player || !player->IsInWorld() || !player->IsAtGroupRewardDistance(boss) ||
                !MainMaySubclass(player->getClass()))
                continue;
            Subclass::Settings const current = SettingsOf(player);
            bool finds = false;
            {
                std::lock_guard<std::mutex> lock(subclassMutex);
                finds = Subclass::FindsBooks(current, pairs, CharacterOf(player),
                    player->GetQuestRewardStatus(current.QuestId));
            }
            if (finds && roll_chance_f(current.BookDropChance))
                GrantBook(player, current.BookItemId);
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
            { "brain", HandleSubclassBrain, SEC_GAMEMASTER, Console::No },
            { "gun", HandleSubclassGun, SEC_GAMEMASTER, Console::No },
            { "bots", HandleSubclassBots, SEC_GAMEMASTER, Console::Yes },
            { "twin", SubclassTwinCommandTable() },
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
        Subclass::Settings const current = SettingsOf(target);
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
            if (!MainMaySubclass(character.Class))
            {
                handler->SendErrorMessage("Only CoA classes and standard mains of a subclass race can have a "
                    "subclass.");
                return false;
            }
            if (PlayableClassOf(subClass) == character.Class)
            {
                handler->SendErrorMessage("{} is already that class; a subclass must be another class.",
                    target->GetName());
                return false;
            }
            if (!IsSpecializationOf(subClass, subSpec) ||
                (wanted.SecondSpec && !IsSpecializationOf(subClass, wanted.SecondSpec)))
            {
                handler->SendErrorMessage("Those specializations do not belong to class {}.",
                    uint32(requestedSubClass));
                return false;
            }
            if (BorrowedTreesOf(wanted).empty())
            {
                handler->SendErrorMessage("That class lends no trees: .subclass set <class> <spec>.");
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
        ShowPrompt(target, target->GetGUID());
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
        LoadStories();
        LoadSpecializationRoles();
        LoadTalentLinks();
        handler->SendSysMessage("Reloaded mod_subclass_pair and mod_subclass_story.");
        return true;
    }
};

Trainer::ClassTrainerFor PreviousTrainerFor = nullptr;

Trainer::Trainer const* ClassTrainerOf(uint8 classId)
{
    Trainer::Trainer const* largest = nullptr;
    for (auto const& [id, trainer] : sObjectMgr->GetTrainers())
        if (trainer.GetTrainerType() == Trainer::Type::Class && trainer.GetTrainerRequirement() == classId &&
            (!largest || trainer.GetSpells().size() > largest->GetSpells().size()))
            largest = &trainer;
    return largest;
}

Trainer::Trainer* TrainerWithSubclass(Trainer::Trainer const& trainer, Player const* player)
{
    Trainer::Trainer* replaced = PreviousTrainerFor ? PreviousTrainerFor(trainer, player) : nullptr;
    Trainer::Trainer const& own = replaced ? *replaced : trainer;
    Subclass::Choice const choice = ChoiceOf(player);
    if (!choice.Complete() || own.GetTrainerRequirement() != player->getClass())
        return replaced;
    Trainer::Trainer const* borrowed = ClassTrainerOf(PlayableClassOf(choice.SubClass));
    if (!borrowed)
        return replaced;
    std::vector<Trainer::Spell> spells = own.GetSpells();
    for (Trainer::Spell const& spell : borrowed->GetSpells())
        if (std::none_of(spells.begin(), spells.end(),
                [&spell](Trainer::Spell const& known) { return known.SpellId == spell.SpellId; }))
            spells.push_back(spell);
    for (Trainer::Spell& spell : spells)
        if (uint32 const copy = BookCopyOf(spell.SpellId); copy && player->HasSpell(copy))
            spell.SpellId = copy;
    thread_local std::optional<Trainer::Trainer> merged;
    merged.emplace(own.WithSpells(std::move(spells)));
    return &*merged;
}

constexpr uint16 RealmInfoOpcode = 0x09BC;
constexpr std::size_t RealmCreationFlagsOffset = 36;
constexpr std::size_t ConquestOfAzerothFlag = 6;
constexpr std::size_t WarcraftRebornFlag = 7;
thread_local bool resendingRealmInfo = false;

bool TravelFormsShapeWholeBody()
{
    return travelFormsWholeBody;
}

bool SubclassRaceEnabled(uint8 race)
{
    if (race == Subclass::ThinHumanRace)
        return thinHumanEnabled;
    if (race == Subclass::TwoHeadedOgreRace)
        return twoHeadedOgreEnabled;
    return true;
}

bool OffersStandardMains()
{
    std::lock_guard<std::mutex> lock(subclassMutex);
    return settings.Enabled && settings.StandardMains;
}

bool IsStandardMainClass(uint8 classId)
{
    return classId >= CLASS_WARRIOR && classId <= CLASS_DRUID && classId != CLASS_DEATH_KNIGHT &&
        classId != CLASS_HERO;
}

bool MayPlayStandardMain(uint8 race, uint8 gender)
{
    std::lock_guard<std::mutex> lock(subclassMutex);
    return Subclass::StoryFor(stories, { race, gender, 0, 0 }) != nullptr;
}

constexpr std::array<uint8, 10> StandardRaces = { RACE_HUMAN, RACE_ORC, RACE_DWARF, RACE_NIGHTELF,
    RACE_UNDEAD_PLAYER, RACE_TAUREN, RACE_GNOME, RACE_TROLL, RACE_BLOODELF, RACE_DRAENEI };

void LearnStandardMainWeaponSkills(Player* player)
{
    if (!IsStandardMainClass(player->getClass()) || !MayPlayStandardMain(player->getRace(), StoryGenderOf(player)))
        return;
    for (uint8 race : StandardRaces)
        if (PlayerInfo const* info = sObjectMgr->GetPlayerInfo(race, player->getClass()))
            for (PlayerCreateInfoSkill const& skill : info->skills)
                if (SkillLineEntry const* line = sSkillLineStore.LookupEntry(skill.SkillId))
                    if ((line->categoryId == SKILL_CATEGORY_WEAPON || line->categoryId == SKILL_CATEGORY_ARMOR) &&
                        !player->HasSkill(skill.SkillId))
                        player->LearnDefaultSkill(skill.SkillId, skill.Rank);
}

bool OpenStandardClassesAtCreation(WorldSession* session, WorldPacket const& packet)
{
    std::size_t const flag = RealmCreationFlagsOffset + WarcraftRebornFlag;
    if (resendingRealmInfo || packet.GetOpcode() != RealmInfoOpcode || packet.size() <= flag ||
        !packet.contents()[RealmCreationFlagsOffset + ConquestOfAzerothFlag] || packet.contents()[flag] ||
        !OffersStandardMains())
        return true;
    WorldPacket opened(packet);
    opened.put<uint8>(flag, 1);
    resendingRealmInfo = true;
    session->SendPacket(&opened);
    resendingRealmInfo = false;
    return false;
}

bool IsRelic(Item const* item)
{
    if (!item)
        return false;
    std::string const& script = sObjectMgr->GetScriptName(item->GetTemplate()->ScriptId);
    return script == BookScriptName || script == BadgeScriptName;
}

bool CarriesRelic(Item const* item)
{
    if (IsRelic(item))
        return true;
    Bag const* bag = item ? item->ToBag() : nullptr;
    if (!bag)
        return false;
    for (uint32 slot = 0; slot < bag->GetBagSize(); ++slot)
        if (IsRelic(bag->GetItemByPos(uint8(slot))))
            return true;
    return false;
}

bool IsBankBag(uint8 bag)
{
    return bag >= BANK_SLOT_BAG_START && bag < BANK_SLOT_BAG_END;
}

bool KeepsRelicOutOfBank(WorldSession* session, WorldPacket const& packet)
{
    Player* player = session->GetPlayer();
    if (!player)
        return true;
    auto byte = [&packet](std::size_t at) { return packet.read<uint8>(at); };
    auto movesRelicIn = [player](uint8 fromBag, uint8 fromSlot, uint8 toBag, uint8 toSlot)
    {
        return (CarriesRelic(player->GetItemByPos(fromBag, fromSlot)) && Player::IsBankPos(toBag, toSlot)) ||
            (CarriesRelic(player->GetItemByPos(toBag, toSlot)) && Player::IsBankPos(fromBag, fromSlot));
    };
    bool refused = false;
    switch (packet.GetOpcode())
    {
        case CMSG_AUTOBANK_ITEM:
            refused = packet.size() >= 2 && CarriesRelic(player->GetItemByPos(byte(0), byte(1)));
            break;
        case CMSG_SWAP_INV_ITEM:
            refused = packet.size() >= 2 && movesRelicIn(INVENTORY_SLOT_BAG_0, byte(1), INVENTORY_SLOT_BAG_0, byte(0));
            break;
        case CMSG_SWAP_ITEM:
            refused = packet.size() >= 4 && movesRelicIn(byte(2), byte(3), byte(0), byte(1));
            break;
        case CMSG_AUTOSTORE_BAG_ITEM:
            refused = packet.size() >= 3 && CarriesRelic(player->GetItemByPos(byte(0), byte(1))) && IsBankBag(byte(2));
            break;
        default:
            return true;
    }
    if (refused)
    {
        player->SendEquipError(EQUIP_ERR_ITEM_DOESNT_GO_TO_SLOT, nullptr);
        ChatHandler(session).SendNotification("This relic stays with you; it cannot go into the bank.");
    }
    return !refused;
}

bool AllowsCreation(WorldSession* session, WorldPacket const& packet)
{
    if (packet.GetOpcode() != CMSG_CHAR_CREATE)
        return true;
    WorldPacket reader(packet);
    reader.rpos(0);
    std::string name;
    uint8 race = 0;
    reader >> name >> race;
    if (SubclassRaceEnabled(race))
        return true;
    WorldPacket refusal(SMSG_CHAR_CREATE, 1);
    refusal << uint8(CHAR_CREATE_DISABLED);
    session->SendPacket(&refusal);
    return false;
}

class mod_subclass_standard_mains : public ServerScript
{
public:
    mod_subclass_standard_mains() : ServerScript("mod_subclass_standard_mains", { SERVERHOOK_CAN_PACKET_RECEIVE,
        SERVERHOOK_CAN_PACKET_SEND }) { }

    bool CanPacketSend(WorldSession* session, WorldPacket const& packet) override
    {
        return !session || OpenStandardClassesAtCreation(session, packet);
    }

    bool CanPacketReceive(WorldSession* session, WorldPacket const& packet) override
    {
        return !session || (AllowsCreation(session, packet) && KeepsRelicOutOfBank(session, packet));
    }
};

void AddSC_mod_subclass()
{
    new mod_subclass_standard_mains();
    PreviousTrainerFor = Trainer::SetClassTrainerFor(&TrainerWithSubclass);
    CoASpellbook::SetExtraClasses([](Player const* player)
    {
        std::vector<std::pair<uint8, uint32>> classes;
        Subclass::Choice const choice = ChoiceOf(player);
        if (choice.Complete() && !SubclassResting(player))
            classes.emplace_back(choice.SubClass, choice.SubSpec);
        return classes;
    });
    new mod_subclass_world();
    new mod_subclass_player();
    new item_subclass_relic(BookScriptName);
    new item_subclass_relic(BadgeScriptName);
    new mod_subclass_trainers();
    new mod_subclass_bosses();
    new mod_subclass_commands();
    new mod_subclass_spells();

    AddAscensionClassMechanicsProvider([](Player const* player, uint8 classId)
    {
        Subclass::Choice const choice = RecordOf(player).Choice;
        return classId && choice.Complete() && choice.SubClass == classId && !SubclassResting(player);
    });
    AddAscensionTalentTreeProvider([](Player const* player)
    {
        AscensionCoATalentState::TalentGrant grant;
        for (Subclass::Tree const& tree : OpenTreesOf(player))
            grant.ExtraTrees.push_back({ tree.ClassId, tree.SpecId });
        return grant;
    });
    AddAscensionClassMechanicsProvider([](Player const* player, uint8 classId)
    {
        if (!IsEligible(player))
            return false;
        Subclass::Choice const choice = ChoiceOf(player);
        return choice.SubClass == classId && !OpenTreesOf(player).empty();
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
        uint8 const subClass = open.front().ClassId;
        std::lock_guard<std::mutex> lock(subclassMutex);
        uint32 lentClassPoints = 0;
        uint32 lentSpecializationPoints = 0;
        if (!IsAscensionCustomClassId(player->getClass()) && IsAscensionCustomClassId(subClass) &&
            AscensionCompatData::GetCoATalentBudget(subClass, GetAscensionCustomClassTalentLevel(player->GetLevel()),
                lentClassPoints, lentSpecializationPoints))
            Subclass::AddLentClassPoints(settings, borrowsClassTree, Subclass::SpecializationTreeCount(open),
                lentClassPoints, lentSpecializationPoints, classPoints, specializationPoints);
        else if (!IsAscensionCustomClassId(player->getClass()))
            specializationPoints += Subclass::BonusPoints(settings, player->CalculateTalentsPoints());
        else
            Subclass::AddBonusPoints(settings, borrowsClassTree, classPoints, specializationPoints);
    });
}
