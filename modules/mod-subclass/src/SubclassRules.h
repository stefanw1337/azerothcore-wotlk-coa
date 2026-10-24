/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license.
 */

#ifndef MOD_SUBCLASS_RULES_H
#define MOD_SUBCLASS_RULES_H

#include <algorithm>
#include <array>
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
    bool ForceBots = false;
    bool TravelFormsWholeBody = true;
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

inline std::uint32_t SpecializationTreeCount(std::vector<Tree> const& trees)
{
    return std::uint32_t(std::count_if(trees.begin(), trees.end(), [](Tree const& tree) { return tree.SpecId != 0; }));
}

inline void AddLentClassPoints(Settings const& settings, bool borrowsClassTree, std::uint32_t specializationTrees,
    std::uint32_t lentClassPoints, std::uint32_t lentSpecializationPoints, std::uint32_t& classPoints,
    std::uint32_t& specializationPoints)
{
    if (borrowsClassTree)
        classPoints += BonusPoints(settings, lentClassPoints);
    specializationPoints += BonusPoints(settings, std::uint32_t(std::min<std::uint64_t>(
        std::uint64_t(lentSpecializationPoints) * specializationTrees, UINT32_MAX)));
}

constexpr std::uint32_t BareCyclopsHead = 1;

inline std::uint32_t CyclopsHelmet(bool ownWardrobe, bool helmHidden, std::uint32_t wornHelmet,
    std::uint32_t chosenHelmet)
{
    if (ownWardrobe && chosenHelmet)
        return chosenHelmet == BareCyclopsHead ? 0 : chosenHelmet;
    return helmHidden ? 0 : wornHelmet;
}

constexpr float CriticalHealthPct = 25.0f;
constexpr float LowHealthPct = 45.0f;
constexpr float AlmostFullHealthPct = 85.0f;

inline bool HealWorthCasting(std::uint32_t expected, std::uint32_t missing, float healthPct)
{
    if (!missing || healthPct >= AlmostFullHealthPct)
        return false;
    return expected <= missing || healthPct < CriticalHealthPct;
}

inline bool BetterHeal(float healthPct, std::uint32_t expected, float bestHealthPct, std::uint32_t bestExpected)
{
    if (healthPct != bestHealthPct)
        return healthPct < bestHealthPct;
    return expected > bestExpected;
}

inline bool TauntNeeded(bool tanking, bool foeOnUs, bool foeOnFriend)
{
    return tanking && !foeOnUs && foeOnFriend;
}

inline std::size_t LeastUsed(std::vector<std::uint32_t> const& counts)
{
    return std::size_t(std::min_element(counts.begin(), counts.end()) - counts.begin());
}

struct Offer
{
    std::uint32_t EntryId = 0;
    std::uint8_t MaxRank = 0;
    std::uint8_t ClassCost = 0;
    std::uint8_t SpecCost = 0;
    std::uint8_t RequiredLevel = 0;
    std::array<std::uint32_t, 3> Requires{};
    std::uint16_t SpecId = 0;
    std::uint32_t ClassInvestment = 0;
    std::uint32_t SpecInvestment = 0;
    std::uint32_t Group = 0;
    bool LinksMustBeMaxed = false;
    std::vector<std::uint32_t> Links;
};

struct Rank
{
    std::uint32_t EntryId = 0;
    std::uint32_t Value = 0;

    bool operator==(Rank const&) const = default;
};

inline std::uint32_t RankIn(std::vector<Rank> const& ranks, std::uint32_t entryId)
{
    auto const found = std::find_if(ranks.begin(), ranks.end(), [entryId](Rank const& rank)
    {
        return rank.EntryId == entryId;
    });
    return found == ranks.end() ? 0 : found->Value;
}

enum class Gate : std::uint8_t
{
    Open,
    Level,
    Link,
    Requirement,
    Investment,
    Group,
};

constexpr std::uint32_t SharedGroup = 1;

inline Offer const* OfferIn(std::vector<Offer> const& offers, std::uint32_t entryId)
{
    auto const found = std::find_if(offers.begin(), offers.end(), [entryId](Offer const& offer)
    {
        return offer.EntryId == entryId;
    });
    return found == offers.end() ? nullptr : &*found;
}

inline std::uint32_t InvestedBelow(std::vector<Offer> const& offers, std::vector<Rank> const& ranks,
    std::uint16_t specId, std::uint32_t requirement, bool classPoints)
{
    std::uint32_t invested = 0;
    for (Offer const& other : offers)
        if (other.SpecId == specId && (classPoints ? other.ClassInvestment : other.SpecInvestment) < requirement)
            invested += RankIn(ranks, other.EntryId) * (classPoints ? other.ClassCost : other.SpecCost);
    return invested;
}

inline bool Linked(std::vector<Offer> const& offers, std::vector<Rank> const& ranks, Offer const& offer)
{
    return offer.Links.empty() || std::any_of(offer.Links.begin(), offer.Links.end(), [&](std::uint32_t link)
    {
        std::uint32_t const rank = RankIn(ranks, link);
        Offer const* linked = OfferIn(offers, link);
        return rank && (!offer.LinksMustBeMaxed || !linked || rank >= linked->MaxRank);
    });
}

inline Gate GateOf(std::vector<Offer> const& offers, std::vector<Rank> const& ranks, Offer const& offer,
    std::uint8_t level)
{
    if (offer.RequiredLevel > level)
        return Gate::Level;
    if (!Linked(offers, ranks, offer))
        return Gate::Link;
    if (!std::all_of(offer.Requires.begin(), offer.Requires.end(),
            [&](std::uint32_t required) { return !required || RankIn(ranks, required); }))
        return Gate::Requirement;
    if ((offer.ClassInvestment &&
            InvestedBelow(offers, ranks, offer.SpecId, offer.ClassInvestment, true) < offer.ClassInvestment) ||
        (offer.SpecInvestment &&
            InvestedBelow(offers, ranks, offer.SpecId, offer.SpecInvestment, false) < offer.SpecInvestment))
        return Gate::Investment;
    if (offer.Group > SharedGroup && std::any_of(offers.begin(), offers.end(), [&](Offer const& other)
        {
            return other.EntryId != offer.EntryId && other.Group == offer.Group && RankIn(ranks, other.EntryId);
        }))
        return Gate::Group;
    return Gate::Open;
}

inline bool Affords(Offer const& offer, std::uint32_t current, std::uint32_t rank, std::uint32_t classPointsLeft,
    std::uint32_t specPointsLeft)
{
    if (rank <= current)
        return true;
    std::uint32_t const cost = (rank - current) * (offer.SpecId ? offer.SpecCost : offer.ClassCost);
    return cost <= (offer.SpecId ? specPointsLeft : classPointsLeft);
}

inline std::uint32_t StrandedBy(std::vector<Offer> const& offers, std::vector<Rank> const& before,
    std::vector<Rank> const& after, std::uint8_t level)
{
    for (Offer const& offer : offers)
        if (RankIn(after, offer.EntryId) && GateOf(offers, before, offer, level) == Gate::Open &&
            GateOf(offers, after, offer, level) != Gate::Open)
            return offer.EntryId;
    return 0;
}

inline std::vector<Rank> PlanRanks(std::vector<Offer> const& offers, std::vector<Rank> const& known,
    std::uint8_t level, std::uint32_t classPoints, std::uint32_t specPoints)
{
    std::vector<Rank> planned;
    for (Offer const& offer : offers)
        planned.push_back({ offer.EntryId, RankIn(known, offer.EntryId) });
    bool raised = true;
    while (raised)
    {
        raised = false;
        for (std::size_t index = 0; index < offers.size(); ++index)
        {
            Offer const& offer = offers[index];
            if (planned[index].Value >= offer.MaxRank || (!offer.ClassCost && !offer.SpecCost) ||
                offer.ClassCost > classPoints || offer.SpecCost > specPoints)
                continue;
            std::vector<Rank> held = planned;
            held.insert(held.end(), known.begin(), known.end());
            if (GateOf(offers, held, offer, level) != Gate::Open)
                continue;
            ++planned[index].Value;
            classPoints -= offer.ClassCost;
            specPoints -= offer.SpecCost;
            raised = true;
        }
    }
    return planned;
}

inline float SplitShare(float his, std::uint32_t before, std::uint32_t total, float half, bool hisSpend)
{
    if (his < 0.0f)
        his = float(total) / 2.0f;
    else if (total > before)
        his += float(total - before) / 2.0f;
    else if (total < before)
    {
        float const spent = float(before - total);
        float const mine = float(before) - his;
        his -= hisSpend ? std::min(spent, his) : std::max(0.0f, spent - mine);
    }
    return std::clamp(his, std::max(0.0f, float(total) - half), std::min(half, float(total)));
}

inline Choice Trimmed(std::vector<Pair> const& pairs, Character const& character, Choice const& choice)
{
    if (!choice.Complete() || !AllowsChoice(pairs, character, choice))
        return {};
    return choice;
}
}

#endif
