/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license.
 */

#ifndef MOD_SUBCLASS_RULES_H
#define MOD_SUBCLASS_RULES_H

#include <algorithm>
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

    bool Empty() const { return !SubClass && !SubSpec; }
    bool Complete() const { return SubClass && SubSpec; }
    bool operator==(Choice const&) const = default;
};

struct Tree
{
    std::uint8_t ClassId = 0;
    std::uint16_t SpecId = 0;

    bool operator==(Tree const&) const = default;
};

struct Settings
{
    bool Enabled = true;
    std::uint8_t MinLevel = 20;
    std::uint32_t BorrowedPointsPercent = 100;
    std::uint32_t QuestId = 9530010;
    std::uint32_t BookItemId = 9530010;
    float BookDropChance = 5.0f;
};

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

inline std::vector<Tree> BorrowedTrees(Choice const& choice)
{
    if (!choice.Complete())
        return {};
    return { { choice.SubClass, 0 }, { choice.SubClass, choice.SubSpec } };
}

inline std::vector<Tree> OpenTrees(Settings const& settings, std::vector<Pair> const& pairs,
    Character const& character, Choice const& choice)
{
    if (!settings.Enabled || character.Level < settings.MinLevel || !choice.Complete() ||
        !AllowsSubclass(pairs, character, choice.SubClass, choice.SubSpec))
        return {};
    return BorrowedTrees(choice);
}

inline std::uint32_t BonusPoints(Settings const& settings, std::uint32_t points)
{
    return std::uint32_t(std::min<std::uint64_t>(std::uint64_t(points) * settings.BorrowedPointsPercent / 100,
        UINT32_MAX));
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

inline Choice Trimmed(std::vector<Pair> const& pairs, Character const& character, Choice const& choice)
{
    if (!choice.Complete() || !AllowsSubclass(pairs, character, choice.SubClass, choice.SubSpec))
        return {};
    return choice;
}
}

#endif
