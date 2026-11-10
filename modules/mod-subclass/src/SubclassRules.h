/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license.
 */

#ifndef MOD_SUBCLASS_RULES_H
#define MOD_SUBCLASS_RULES_H

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
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
    std::uint32_t BrotherMirrorDisplay = 977602;
    std::uint8_t HeroRaceSecurity = 0;
    std::uint8_t HeroRaceLevel = 60;
};

enum class HeroRaceRefusal
{
    None,
    Security,
    Level
};

inline HeroRaceRefusal HeroRaceRefusalFor(Settings const& settings, std::uint8_t security, std::uint8_t highestLevel)
{
    if (security < settings.HeroRaceSecurity)
        return HeroRaceRefusal::Security;
    if (!security && settings.HeroRaceLevel && highestLevel < settings.HeroRaceLevel)
        return HeroRaceRefusal::Level;
    return HeroRaceRefusal::None;
}

struct SpecializationRoles
{
    bool Melee = false;
    bool Ranged = false;
    bool Caster = false;
    bool Tank = false;
    bool Healer = false;
};

inline bool KeepsDistance(SpecializationRoles const& roles)
{
    return !roles.Tank && (roles.Ranged || roles.Caster || roles.Healer);
}

inline bool HalfFormExpired(std::uint32_t until, std::uint32_t now)
{
    return until && now >= until;
}

constexpr std::uint8_t ThinTwinsBeards = 7;
constexpr std::uint32_t ThinTwinsDisplay = 977601;

constexpr std::uint8_t SameAsPlayer = 255;

inline std::uint8_t BrotherFeature(std::uint8_t own, std::uint8_t chosen)
{
    return chosen == SameAsPlayer ? own : chosen;
}

inline bool BrotherFeatureFits(std::uint8_t chosen, std::uint8_t choices)
{
    return chosen == SameAsPlayer || chosen < choices;
}

inline std::uint32_t BrotherDisplay(std::uint32_t native, std::uint32_t mirror)
{
    return native == ThinTwinsDisplay && mirror ? mirror : native;
}
constexpr std::uint8_t ThinTwinsCutHairstyles = 3;

inline std::uint8_t FacialUnderHat(std::uint8_t facial, std::uint8_t hairStyle, bool hat)
{
    std::uint8_t const beard = facial % ThinTwinsBeards;
    std::uint8_t const cut = hat && hairStyle < ThinTwinsCutHairstyles ? hairStyle + 1 : 0;
    return beard + ThinTwinsBeards * cut;
}

constexpr std::uint8_t ThinHumanRace = 96;
constexpr std::uint8_t TwoHeadedOgreRace = 85;

inline char const* RaceToken(std::uint8_t race)
{
    if (race == TwoHeadedOgreRace)
        return "ogre";
    if (race == ThinHumanRace)
        return "twins";
    return "-";
}

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

struct HeldWeapons
{
    std::array<std::uint32_t, 3> Entries{};
    std::array<std::uint32_t, 3> Enchants{};
};

inline void HoldShownWeapons(HeldWeapons& held, HeldWeapons const& shown)
{
    for (std::size_t index = 0; index < held.Entries.size(); ++index)
        if (shown.Entries[index])
        {
            held.Entries[index] = shown.Entries[index];
            held.Enchants[index] = shown.Enchants[index];
        }
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

constexpr std::uint8_t MaxPairLevelGap = 3;

inline bool IsTwoPlayersBody(std::uint8_t race, std::uint8_t gender)
{
    return (race == TwoHeadedOgreRace && gender == 0) || (race == ThinHumanRace && gender == 1);
}

inline bool HasAnyRole(SpecializationRoles const& roles)
{
    return roles.Melee || roles.Ranged || roles.Caster || roles.Tank || roles.Healer;
}

inline SpecializationRoles Joined(SpecializationRoles const& first, SpecializationRoles const& second)
{
    return { first.Melee || second.Melee, first.Ranged || second.Ranged, first.Caster || second.Caster,
        first.Tank || second.Tank, first.Healer || second.Healer };
}

inline bool FitsLeftHead(SpecializationRoles const& roles)
{
    return roles.Melee || roles.Tank;
}

inline bool FitsRightHead(SpecializationRoles const& roles)
{
    return roles.Caster || roles.Ranged || roles.Healer;
}

struct PairHead
{
    bool TwoPlayers = false;
    bool Paired = false;
    std::uint8_t Level = 0;
    SpecializationRoles Roles;
};

enum class PairRefusal
{
    None,
    LeftNotTwoPlayers,
    RightNotTwoPlayers,
    LeftPaired,
    RightPaired,
    LevelGap,
    LeftSpecialization,
    RightSpecialization
};

inline PairRefusal CheckPair(PairHead const& left, PairHead const& right)
{
    if (!left.TwoPlayers)
        return PairRefusal::LeftNotTwoPlayers;
    if (!right.TwoPlayers)
        return PairRefusal::RightNotTwoPlayers;
    if (left.Paired)
        return PairRefusal::LeftPaired;
    if (right.Paired)
        return PairRefusal::RightPaired;
    std::uint8_t const gap = left.Level > right.Level ? left.Level - right.Level : right.Level - left.Level;
    if (gap > MaxPairLevelGap)
        return PairRefusal::LevelGap;
    if (!FitsLeftHead(left.Roles))
        return PairRefusal::LeftSpecialization;
    if (!FitsRightHead(right.Roles))
        return PairRefusal::RightSpecialization;
    return PairRefusal::None;
}

constexpr std::uint8_t EquipmentSlotCount = 19;
constexpr std::uint8_t ShirtSlot = 3;
constexpr std::uint8_t TabardSlot = 18;

inline bool IsEchoSlot(std::uint8_t slot)
{
    return slot < EquipmentSlotCount && slot != ShirtSlot && slot != TabardSlot;
}

constexpr std::uint32_t TwoPlayersBodyDisplay = 985001;
constexpr std::uint32_t MutatedBodyDisplay = 985002;

constexpr std::uint32_t TwoPlayersVehicle = 9600;

enum class Borrow
{
    None,
    Movement,
    Melee,
    Ranged
};

struct SpellShape
{
    std::array<std::uint32_t, 3> Effects{};
    std::array<std::uint32_t, 3> Auras{};
    std::array<std::int32_t, 3> AuraValues{};
    std::array<std::uint32_t, 3> TargetsA{};
    bool Passive = false;
    std::int32_t EquippedClass = -1;
    std::uint32_t EquippedSubClassMask = 0;
    std::uint32_t DamageClass = 0;
    float MaxRange = 0.0f;
    std::int32_t CastTime = 0;
    bool Channeled = false;
    std::int32_t Duration = 0;
    bool Positive = false;
};

constexpr float MeleeReach = 6.0f;
constexpr std::int32_t LongBuff = 60000;

inline bool MovesTheBody(SpellShape const& spell)
{
    constexpr std::uint32_t Teleport = 5, Leap = 29, Jump = 41, JumpDest = 42, Charge = 96, LeapBack = 138,
        ChargeDest = 149;
    constexpr std::uint32_t Fear = 7, IncreaseSpeed = 31, Mounted = 32, SpeedAlways = 129, SpeedNotStack = 171;
    constexpr std::uint32_t Caster = 1, CasterParty = 20;
    bool fears = false;
    bool mounts = false;
    for (std::uint32_t aura : spell.Auras)
    {
        fears = fears || aura == Fear;
        mounts = mounts || aura == Mounted;
    }
    for (std::size_t i = 0; i < 3; ++i)
    {
        std::uint32_t const effect = spell.Effects[i];
        if (effect == Leap || effect == Jump || effect == JumpDest || effect == Charge || effect == LeapBack ||
            effect == ChargeDest || (effect == Teleport && spell.TargetsA[i] == Caster))
            return true;
        std::uint32_t const aura = spell.Auras[i];
        bool const own = spell.TargetsA[i] == Caster || spell.TargetsA[i] == CasterParty;
        if (!fears && !mounts && own && (aura == IncreaseSpeed || aura == SpeedAlways || aura == SpeedNotStack))
            return true;
    }
    return false;
}

inline bool ChangesForm(SpellShape const& spell)
{
    constexpr std::uint32_t Shapeshift = 36, Mounted = 32;
    return std::any_of(spell.Auras.begin(), spell.Auras.end(),
        [](std::uint32_t aura) { return aura == Shapeshift || aura == Mounted; });
}

inline bool FightsUpClose(SpellShape const& spell)
{
    constexpr std::uint32_t MeleeDamage = 2, AttackMe = 114;
    constexpr std::uint32_t DamageShield = 15, Taunt = 11, ModStat = 29, Parry = 47, Dodge = 49, Block = 51,
        WeaponCrit = 52, HitChance = 54, AttackPower = 99, MeleeHaste = 138, Expertise = 240;
    constexpr std::int32_t Strength = 0;
    constexpr std::int32_t WeaponClass = 2;
    constexpr std::uint32_t RangedWeapons = (1u << 2) | (1u << 3) | (1u << 16) | (1u << 18) | (1u << 19);
    if (spell.DamageClass == MeleeDamage)
        return true;
    if (spell.EquippedClass == WeaponClass && spell.EquippedSubClassMask &&
        !(spell.EquippedSubClassMask & RangedWeapons))
        return true;
    for (std::size_t i = 0; i < 3; ++i)
    {
        std::uint32_t const aura = spell.Auras[i];
        if (spell.Effects[i] == AttackMe || aura == Taunt || aura == DamageShield || aura == Parry || aura == Dodge ||
            aura == Block || aura == WeaponCrit || aura == HitChance || aura == AttackPower || aura == MeleeHaste ||
            aura == Expertise || (aura == ModStat && spell.AuraValues[i] == Strength))
            return true;
    }
    return false;
}

inline bool WorksFromAfar(SpellShape const& spell)
{
    constexpr std::uint32_t RangedDamage = 3;
    constexpr std::int32_t WeaponClass = 2;
    constexpr std::uint32_t RangedWeapons = (1u << 2) | (1u << 3) | (1u << 16) | (1u << 18) | (1u << 19);
    if (spell.EquippedClass == WeaponClass && (spell.EquippedSubClassMask & RangedWeapons) &&
        !(spell.EquippedSubClassMask & ~RangedWeapons))
        return true;
    if (spell.DamageClass == RangedDamage || spell.CastTime > 0 || spell.Channeled || spell.MaxRange > MeleeReach)
        return true;
    bool const buff = spell.Positive && std::any_of(spell.Auras.begin(), spell.Auras.end(),
        [](std::uint32_t aura) { return aura != 0; });
    return buff && (spell.Duration < 0 || spell.Duration >= LongBuff);
}

inline Borrow BorrowOf(SpellShape const& spell)
{
    if (spell.Passive)
        return Borrow::None;
    if (MovesTheBody(spell))
        return Borrow::Movement;
    if (ChangesForm(spell))
        return Borrow::None;
    if (FightsUpClose(spell))
        return Borrow::Melee;
    if (WorksFromAfar(spell))
        return Borrow::Ranged;
    return Borrow::None;
}

inline bool BodyCasts(Borrow borrow, bool pressedByLeftHead)
{
    return ((borrow == Borrow::Movement || borrow == Borrow::Melee) && pressedByLeftHead) ||
        (borrow == Borrow::Ranged && !pressedByLeftHead);
}

inline std::uint32_t ShareOfOwnPower(std::uint32_t cost, std::uint32_t ownerMax, std::uint32_t payerMax)
{
    if (!cost || !ownerMax)
        return 0;
    return std::uint32_t((std::uint64_t(cost) * payerMax + ownerMax - 1) / ownerMax);
}

enum class AccordMode
{
    None,
    Agree,
    Disagree
};

struct Accord
{
    AccordMode Mode = AccordMode::None;
    std::uint32_t Stacks = 0;
    std::uint64_t MeleeTarget = 0;
    std::uint64_t RangedTarget = 0;
    bool MeleeHit = false;
    bool RangedHit = false;
    std::uint32_t LastStackAt = 0;
};

struct AccordSettings
{
    std::uint32_t AgreeMaxStacks = 3;
    std::uint32_t DisagreeMaxStacks = 3;
    std::uint32_t HoldMs = 5000;
};

inline bool AccordExpired(Accord const& accord, std::uint32_t now, AccordSettings const& settings)
{
    return accord.Mode != AccordMode::None && now - accord.LastStackAt > settings.HoldMs;
}

inline Accord AccordAfterHit(Accord accord, bool byMeleeHead, std::uint64_t target, std::uint32_t now,
    AccordSettings const& settings)
{
    if (AccordExpired(accord, now, settings))
        accord = Accord{ AccordMode::None, 0, accord.MeleeTarget, accord.RangedTarget, false, false, 0 };
    (byMeleeHead ? accord.MeleeTarget : accord.RangedTarget) = target;
    (byMeleeHead ? accord.MeleeHit : accord.RangedHit) = true;
    if (!accord.MeleeHit || !accord.RangedHit)
        return accord;
    AccordMode const mode = accord.MeleeTarget == accord.RangedTarget ? AccordMode::Agree : AccordMode::Disagree;
    std::uint32_t const most = mode == AccordMode::Agree ? settings.AgreeMaxStacks : settings.DisagreeMaxStacks;
    accord.Stacks = mode == accord.Mode ? std::min(most, accord.Stacks + 1) : std::min<std::uint32_t>(most, 1);
    accord.Mode = mode;
    accord.MeleeHit = false;
    accord.RangedHit = false;
    accord.LastStackAt = now;
    return accord;
}

inline std::int32_t QuestGoldShare(std::int32_t money, bool meleeHead)
{
    if (money <= 0)
        return money;
    return meleeHead ? money - money / 2 : money / 2;
}

inline std::pair<std::uint32_t, std::uint32_t> SharedPool(std::uint32_t first, std::uint32_t firstMax,
    std::uint32_t second, std::uint32_t secondMax)
{
    std::uint64_t const most = std::uint64_t(firstMax) + secondMax;
    if (!most)
        return { first, second };
    std::uint64_t const held = std::uint64_t(first) + second;
    std::uint32_t const firstShare = std::uint32_t((held * firstMax + most / 2) / most);
    return { std::min(firstShare, firstMax), std::uint32_t(std::min<std::uint64_t>(held - firstShare, secondMax)) };
}

inline bool RangedHeadMayNeed(std::uint32_t itemClass)
{
    constexpr std::uint32_t Weapon = 2, Armor = 4;
    return itemClass != Weapon && itemClass != Armor;
}

inline std::optional<std::pair<std::string, std::string>> SplitJoinedName(std::string_view joined)
{
    std::size_t const mark = joined.find('\'');
    if (mark == std::string_view::npos || mark == 0 || mark + 1 >= joined.size() ||
        joined.find('\'', mark + 1) != std::string_view::npos)
        return std::nullopt;
    return std::make_pair(std::string(joined.substr(0, mark)), std::string(joined.substr(mark + 1)));
}

inline Choice Trimmed(std::vector<Pair> const& pairs, Character const& character, Choice const& choice)
{
    if (!choice.Complete() || !AllowsChoice(pairs, character, choice))
        return {};
    return choice;
}
}

#endif
