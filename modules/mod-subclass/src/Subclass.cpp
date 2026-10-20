/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license.
 */

#include "AllCreatureScript.h"
#include "AllSpellScript.h"
#include "AscensionCoATalentData.h"
#include "AscensionCustomClassData.h"
#include "AscensionCustomResourceData.h"
#include "AscensionLiveBaselineData.h"
#include "DBCStores.h"
#include "AscensionSpecialization.h"
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
#include "Player.h"
#include "PlayerScript.h"
#include "Random.h"
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
#include <unordered_map>
#include <unordered_set>

using namespace Acore::ChatCommands;

void HandleTwinLook(Player* player, std::vector<std::string_view> const& words);
void LearnStandardMainWeaponSkills(Player* player);
std::string TwinWearing(Player const* player);
std::string TwinState(Player const* player);
std::string ToggleTwin(Player* player);
bool SubclassResting(Player const* player);
bool LonelyTwin(Player const* player);
bool OrderBrother(Player* player, uint32 spellId, Unit* target);
bool OffersStandardMains();
bool SubclassRaceEnabled(uint8 race);
bool IsStandardMainClass(uint8 classId);

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

std::mutex& subclassMutex = *new std::mutex;
Subclass::Settings settings;
std::atomic<bool> thinHumanEnabled{ true };
std::atomic<bool> twoHeadedOgreEnabled{ true };
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
    return { player->getRace(), player->getGender(), player->getClass(), player->GetLevel() };
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

bool MainMaySubclass(uint8 classId)
{
    bool const standard = !IsAscensionCustomClassId(classId);
    if (!Subclass::KindAllows(CurrentSettings().Mains, standard))
        return false;
    return !standard || (IsStandardMainClass(classId) && OffersStandardMains());
}

bool IsEligible(Player const* player)
{
    if (!MainMaySubclass(player->getClass()) || !SubclassRaceEnabled(player->getRace()))
        return false;
    std::lock_guard<std::mutex> lock(subclassMutex);
    return Subclass::IsEligible(pairs, CharacterOf(player));
}

std::vector<Subclass::Tree> OpenTreesOf(Player const* player)
{
    if (!MainMaySubclass(player->getClass()) || !SubclassRaceEnabled(player->getRace()))
        return {};
    Subclass::Choice const choice = ChoiceOf(player);
    bool const hasClassTree = HasClassTree(choice.SubClass);
    std::lock_guard<std::mutex> lock(subclassMutex);
    return Subclass::OpenTrees(settings, pairs, CharacterOf(player), choice, hasClassTree, SpecsOf(choice.SubClass),
        TreesFor(choice.SubClass));
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
            if (entry.ClassId == tree.ClassId && entry.SpecId == tree.SpecId &&
                (entry.AECost || entry.TECost || entry.SpellCount))
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
    if (!open.empty())
    {
        uint8 const subClass = record.Choice.SubClass;
        for (AscensionCompatData::ResourceDisplay const& display : AscensionCompatData::ResourceDisplays)
            if (display.ClassId == subClass)
                SendAddon(player, Acore::StringFormat("RES {} {} {}", display.SpellId, display.DisplayMaximum,
                    display.Name));
        std::vector<uint32> powers;
        for (AscensionCompatData::NativePowerGainRule const& rule : AscensionCompatData::NativePowerGainRules)
            if (rule.ClassId == subClass && rule.PowerType != player->getPowerType() &&
                std::find(powers.begin(), powers.end(), uint32(rule.PowerType)) == powers.end())
                powers.push_back(rule.PowerType);
        if (subClass == CLASS_RANGER && player->getPowerType() != POWER_FOCUS)
            powers.push_back(POWER_FOCUS);
        if (Powers const standard = StandardClassPower(subClass); standard != MAX_POWERS &&
            player->getPowerType() != standard)
            powers.push_back(standard);
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
        SendAddon(player, TwinState(player));
        return;
    }
    if (message == "TWINTOGGLE")
    {
        ChatHandler(player->GetSession()).SendSysMessage(ToggleTwin(player));
        SendAddon(player, TwinState(player));
        return;
    }
    std::vector<std::string_view> const words = Acore::Tokenize(message, ' ', false);
    if (!words.empty() && words[0] == "TWINLOOK")
    {
        HandleTwinLook(player, words);
        SendAddon(player, TwinWearing(player));
    }
    else if (words.size() == 1 && words[0] == "TWINWEARING")
        SendAddon(player, TwinWearing(player));
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
}

uint32 SyncBorrowedBaseSpells(Player* player)
{
    if (OpenTreesOf(player).empty())
        return 0;
    uint8 const subClass = ChoiceOf(player).SubClass;
    SyncBorrowedSkillLines(player, subClass);
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

void ShowPrompt(Player* player)
{
    Voice const voice = VoiceOf(player);
    ClearGossipMenuFor(player);
    AddGossipItemFor(player, GOSSIP_ICON_CHAT, voice.AcceptOption, SenderPrompt, ActionAccept);
    AddGossipItemFor(player, GOSSIP_ICON_CHAT, voice.LaterOption, SenderPrompt, ActionLater);
    player->PlayerTalkClass->GetGossipMenu().SetMenuId(PromptMenuId);
    SendGossipMenuFor(player, voice.PromptText, player->GetGUID());
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
    Quest const* quest = sObjectMgr->GetQuestTemplate(SettingsOf(player).QuestId);
    if (!quest || !OffersQuest(player) || !player->CanTakeQuest(quest, true) || !player->CanAddQuest(quest, true))
        return;
    player->AddQuestAndCheckCompletion(quest, nullptr);
    ChatHandler(player->GetSession()).SendSysMessage(VoiceOf(player).Says(VoiceOf(player).Accepted));
}

bool OfferedAsSubclass(uint8 mainClass, uint8 catalogClassId)
{
    if (PlayableClassOf(catalogClassId) == mainClass ||
        !Subclass::KindAllows(settings.SubClasses, IsStandardCatalogClass(catalogClassId)))
        return false;
    if (!IsAscensionCustomClassId(mainClass))
        return !IsAscensionCustomClassId(PlayableClassOf(catalogClassId));
    return settings.StandardClasses || !AscensionCompatData::IsCoABorrowOnlyClass(catalogClassId);
}

void ShowClasses(Player* player, Creature* trainer)
{
    ClearGossipMenuFor(player);
    Subclass::Character const character = CharacterOf(player);
    {
        std::lock_guard<std::mutex> lock(subclassMutex);
        for (NamedClass const& named : Classes)
            if (OfferedAsSubclass(character.Class, named.Id) &&
                Subclass::AllowsAnySpecializationOf(pairs, character, named.Id))
                AddGossipItemFor(player, GOSSIP_ICON_TRAINER, ClassName(named.Id), SenderClass, named.Id);
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
    if (!player->AddItem(bookItemId, 1))
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
        loaded.StandardMains = sConfigMgr->GetOption<bool>("Subclass.StandardMains", false) &&
            sConfigMgr->GetOption<std::string>("CoA.ClassModel", "coa") == "coa";
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
        thinHumanEnabled = loaded.ThinHuman;
        twoHeadedOgreEnabled = loaded.TwoHeadedOgre;
        std::lock_guard<std::mutex> lock(subclassMutex);
        settings = loaded;
    }

    void OnStartup() override
    {
        LoadPairs();
        LoadStories();
        LoadSpecializationRoles();
    }
};

bool SubclassChosen(Player const* player)
{
    return RecordOf(player).Choice.Complete();
}

void OfferStoryLater(Player* player)
{
    SchedulePrompt(player, 2s);
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

class mod_subclass_spells : public AllSpellScript
{
public:
    mod_subclass_spells() : AllSpellScript("mod_subclass_spells", { ALLSPELLHOOK_ON_SPELL_CHECK_CAST }) { }

    void OnSpellCheckCast(Spell* spell, bool /*strict*/, SpellCastResult& res) override
    {
        Player* player = spell->GetCaster() ? spell->GetCaster()->ToPlayer() : nullptr;
        if (res != SPELL_CAST_OK || !player || spell->IsTriggered())
            return;
        if (CallsHunterPet(spell->GetSpellInfo()))
            BringBackParkedHunterPet(player);
        if (!IsBorrowedSpell(player, spell->GetSpellInfo()->Id))
            return;
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
        PLAYERHOOK_ON_DELETE, PLAYERHOOK_ON_LEVEL_CHANGED, PLAYERHOOK_ON_GOSSIP_SELECT,
        PLAYERHOOK_CAN_PLAYER_USE_PRIVATE_CHAT, PLAYERHOOK_ON_PLAYER_IS_CLASS,
        PLAYERHOOK_ON_PLAYER_HAS_ACTIVE_POWER_TYPE })
    {
    }

    bool OnPlayerHasActivePowerType(Player const* player, Powers power) override
    {
        Subclass::Choice const choice = ChoiceOf(player);
        return choice.Complete() && StandardClassPower(choice.SubClass) == power && player->getPowerType() != power &&
            !SubclassResting(player);
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
        LearnStandardMainWeaponSkills(player);
        LendRunes(player);
        SyncBorrowedBaseSpells(player);
        RefreshPowerPools(player);
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
            ChatHandler(player->GetSession()).SendSysMessage(VoiceOf(player).Says(VoiceOf(player).Later));
        }
    }

    void OnPlayerLogout(Player* player) override
    {
        std::lock_guard<std::mutex> lock(subclassMutex);
        records.erase(player->GetGUID().GetCounter());
        focusLent.erase(player->GetGUID().GetCounter());
        runesLent.erase(player->GetGUID().GetCounter());
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
        if (MainMaySubclass(character.Class) && SubclassRaceEnabled(character.Race) &&
            IsSpecializationOf(choice.SubClass, choice.SubSpec))
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
            player->DestroyItemCount(bookItemId, 1, true);
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

class mod_subclass_trainers : public AllCreatureScript
{
public:
    mod_subclass_trainers() : AllCreatureScript("mod_subclass_trainers") { }

    bool CanCreatureGossipHello(Player* player, Creature* creature) override
    {
        if (!creature->HasNpcFlag(UNIT_NPC_FLAG_TRAINER_CLASS) || !IsEligible(player) || !RecordOf(player).Insights)
            return false;
        player->PrepareGossipMenu(creature, creature->GetGossipMenuId(), true);
        AddGossipItemFor(player, GOSSIP_ICON_TRAINER, VoiceOf(player).TrainerOption, SenderTrainer, 0);
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
        LoadStories();
        LoadSpecializationRoles();
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
    thread_local std::optional<Trainer::Trainer> merged;
    merged.emplace(own.WithSpells(std::move(spells)));
    return &*merged;
}

constexpr uint16 RealmInfoOpcode = 0x09BC;
constexpr std::size_t RealmCreationFlagsOffset = 36;
constexpr std::size_t ConquestOfAzerothFlag = 6;
constexpr std::size_t WarcraftRebornFlag = 7;
thread_local bool resendingRealmInfo = false;

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
    if (!IsStandardMainClass(player->getClass()) || !MayPlayStandardMain(player->getRace(), player->getGender()))
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

bool AllowsCreation(WorldSession* session, WorldPacket const& packet)
{
    if (packet.GetOpcode() != CMSG_CHAR_CREATE)
        return true;
    WorldPacket reader(packet);
    reader.rpos(0);
    std::string name;
    uint8 race = 0;
    uint8 classId = 0;
    uint8 gender = 0;
    reader >> name >> race >> classId >> gender;
    ResponseCodes refusalCode = CHAR_CREATE_RESTRICTED_RACECLASS;
    if (!SubclassRaceEnabled(race))
        refusalCode = CHAR_CREATE_DISABLED;
    else if (!OffersStandardMains() || !IsStandardMainClass(classId) || MayPlayStandardMain(race, gender))
        return true;
    WorldPacket refusal(SMSG_CHAR_CREATE, 1);
    refusal << uint8(refusalCode);
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
        return !session || AllowsCreation(session, packet);
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
        std::lock_guard<std::mutex> lock(subclassMutex);
        if (!IsAscensionCustomClassId(player->getClass()))
            specializationPoints += Subclass::BonusPoints(settings, player->CalculateTalentsPoints());
        else
            Subclass::AddBonusPoints(settings, borrowsClassTree, classPoints, specializationPoints);
    });
}
