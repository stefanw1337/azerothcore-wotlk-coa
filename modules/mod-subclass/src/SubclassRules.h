/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license.
 */

#ifndef MOD_SUBCLASS_RULES_H
#define MOD_SUBCLASS_RULES_H

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace Subclass
{
constexpr std::uint8_t GenderMaskMale = 1;
constexpr std::uint8_t GenderMaskFemale = 2;

struct Pair
{
    std::uint8_t Race = 0;
    std::uint8_t GenderMask = GenderMaskMale | GenderMaskFemale;
    std::uint8_t MainClass = 0;
    std::uint8_t SubClass = 0;
    std::uint16_t SubSpec = 0;
};

struct Character
{
    std::uint8_t Race = 0;
    std::uint8_t Gender = 0;
    std::uint8_t Class = 0;
    std::uint8_t Level = 0;
};

struct Choice
{
    std::uint8_t SubClass = 0;
    std::uint16_t SubSpec = 0;
    std::uint16_t SecondSpec = 0;

    bool Empty() const { return !SubClass && !SubSpec && !SecondSpec; }
    bool Complete() const { return SubClass && SubSpec; }
    bool operator==(Choice const&) const = default;
};

struct Tree
{
    std::uint8_t ClassId = 0;
    std::uint16_t SpecId = 0;

    bool operator==(Tree const&) const = default;
};

enum class ClassKinds : std::uint8_t
{
    All = 0,
    Standard = 1,
    CoA = 2
};

inline bool KindAllows(ClassKinds kinds, bool standard)
{
    return kinds == ClassKinds::All || (kinds == ClassKinds::Standard) == standard;
}

struct Settings
{
    bool Enabled = true;
    std::uint8_t MinLevel = 20;
    std::uint32_t BorrowedPointsPercent = 100;
    std::uint32_t QuestId = 9530010;
    std::uint32_t BookItemId = 9530010;
    float BookDropChance = 5.0f;
    bool StandardClasses = false;
    bool StandardMains = false;
    ClassKinds Mains = ClassKinds::All;
    ClassKinds SubClasses = ClassKinds::All;
    std::uint8_t StandardTrees = 3;
    std::uint8_t CoATrees = 1;
    bool ThinHuman = true;
    bool TwoHeadedOgre = true;
};

constexpr std::uint8_t ThinHumanRace = 96;
constexpr std::uint8_t TwoHeadedOgreRace = 85;

inline bool GenderMatches(std::uint8_t genderMask, std::uint8_t gender)
{
    return gender < 2 && (genderMask & (1u << gender)) != 0;
}

inline bool CharacterMatches(Pair const& pair, Character const& character)
{
    return (!pair.Race || pair.Race == character.Race) && GenderMatches(pair.GenderMask, character.Gender) &&
        (!pair.MainClass || pair.MainClass == character.Class);
}

inline bool IsEligible(std::vector<Pair> const& pairs, Character const& character)
{
    return std::any_of(pairs.begin(), pairs.end(), [&character](Pair const& pair)
    {
        return CharacterMatches(pair, character);
    });
}

inline bool AllowsSubclass(std::vector<Pair> const& pairs, Character const& character, std::uint8_t subClass,
    std::uint16_t subSpec)
{
    return subClass && subClass != character.Class && std::any_of(pairs.begin(), pairs.end(), [&](Pair const& pair)
    {
        return CharacterMatches(pair, character) && (!pair.SubClass || pair.SubClass == subClass) &&
            (!pair.SubSpec || pair.SubSpec == subSpec);
    });
}

inline bool AllowsChoice(std::vector<Pair> const& pairs, Character const& character, Choice const& choice)
{
    return AllowsSubclass(pairs, character, choice.SubClass, choice.SubSpec) && (!choice.SecondSpec ||
        (choice.SecondSpec != choice.SubSpec && AllowsSubclass(pairs, character, choice.SubClass, choice.SecondSpec)));
}

inline bool LendsAllSpecializations(std::size_t specCount, std::uint8_t treeCount)
{
    return specCount && treeCount >= specCount;
}

inline std::vector<Tree> BorrowedTrees(Choice const& choice, bool hasClassTree,
    std::vector<std::uint16_t> const& classSpecs = {}, std::uint8_t treeCount = 0)
{
    if (!choice.Complete())
        return {};
    std::vector<Tree> trees;
    if (hasClassTree)
        trees.push_back({ choice.SubClass, 0 });
    std::uint8_t const wanted = treeCount ? treeCount : (hasClassTree ? 1 : 0xFF);
    if (!hasClassTree && classSpecs.empty())
        return {};
    if (LendsAllSpecializations(classSpecs.size(), wanted))
    {
        for (std::uint16_t spec : classSpecs)
            trees.push_back({ choice.SubClass, spec });
        return trees;
    }
    trees.push_back({ choice.SubClass, choice.SubSpec });
    if (wanted >= 2 && choice.SecondSpec && choice.SecondSpec != choice.SubSpec)
        trees.push_back({ choice.SubClass, choice.SecondSpec });
    return trees;
}

inline std::vector<Tree> OpenTrees(Settings const& settings, std::vector<Pair> const& pairs,
    Character const& character, Choice const& choice, bool hasClassTree,
    std::vector<std::uint16_t> const& classSpecs = {}, std::uint8_t treeCount = 0)
{
    if (!settings.Enabled || character.Level < settings.MinLevel || !choice.Complete() ||
        !AllowsChoice(pairs, character, choice))
        return {};
    return BorrowedTrees(choice, hasClassTree, classSpecs, treeCount);
}

inline std::uint32_t BonusPoints(Settings const& settings, std::uint32_t points)
{
    return std::uint32_t(std::min<std::uint64_t>(std::uint64_t(points) * settings.BorrowedPointsPercent / 100,
        UINT32_MAX));
}

struct Story
{
    std::uint8_t Race = 0;
    std::uint8_t GenderMask = GenderMaskMale | GenderMaskFemale;
    std::uint32_t QuestId = 0;
    std::uint32_t RelicItemId = 0;
};

inline Story const* StoryFor(std::vector<Story> const& stories, Character const& character)
{
    Story const* found = nullptr;
    for (Story const& story : stories)
        if ((!story.Race || story.Race == character.Race) && GenderMatches(story.GenderMask, character.Gender) &&
            (!found || (story.Race && !found->Race)))
            found = &story;
    return found;
}

inline Settings WithStory(Settings settings, std::vector<Story> const& stories, Character const& character)
{
    if (Story const* story = StoryFor(stories, character))
    {
        settings.QuestId = story->QuestId;
        settings.BookItemId = story->RelicItemId;
    }
    return settings;
}

inline bool OffersQuest(Settings const& settings, std::vector<Pair> const& pairs, Character const& character,
    Choice const& choice, std::uint32_t insights, bool questUntouched)
{
    return settings.Enabled && settings.QuestId && character.Level >= settings.MinLevel &&
        IsEligible(pairs, character) && choice.Empty() && !insights && questUntouched;
}

inline bool FindsBooks(Settings const& settings, std::vector<Pair> const& pairs, Character const& character,
    bool questRewarded)
{
    return settings.Enabled && settings.BookDropChance > 0.0f && character.Level >= settings.MinLevel &&
        IsEligible(pairs, character) && questRewarded;
}

inline bool AllowsAnySpecializationOf(std::vector<Pair> const& pairs, Character const& character,
    std::uint8_t subClass)
{
    return subClass && subClass != character.Class && std::any_of(pairs.begin(), pairs.end(), [&](Pair const& pair)
    {
        return CharacterMatches(pair, character) && (!pair.SubClass || pair.SubClass == subClass);
    });
}

inline void AddBonusPoints(Settings const& settings, bool borrowsClassTree, std::uint32_t& classPoints,
    std::uint32_t& specializationPoints)
{
    std::uint32_t const classBonus = BonusPoints(settings, classPoints);
    std::uint32_t const specializationBonus = BonusPoints(settings, specializationPoints);
    if (borrowsClassTree)
        classPoints += classBonus;
    else
        specializationPoints += classBonus;
    specializationPoints += specializationBonus;
}

inline Choice Trimmed(std::vector<Pair> const& pairs, Character const& character, Choice const& choice)
{
    if (!choice.Complete() || !AllowsChoice(pairs, character, choice))
        return {};
    return choice;
}
}

#endif
