/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license.
 */

#include "SubclassRules.h"
#include <gtest/gtest.h>

using namespace Subclass;

namespace
{
std::vector<Pair> const FemaleOgres = { { 53, GenderMaskFemale, 0, 0, 0 } };
Character const FemaleOgreBarbarian = { 53, 1, 12, 80 };
Character const MaleOgreBarbarian = { 53, 0, 12, 80 };
Character const FemaleHumanBarbarian = { 1, 1, 12, 80 };
Choice const NecromancerDeath = { 23, 34 };
}

TEST(Subclass, OnlyTheConfiguredRaceAndGenderAreEligible)
{
    EXPECT_TRUE(IsEligible(FemaleOgres, FemaleOgreBarbarian));
    EXPECT_FALSE(IsEligible(FemaleOgres, MaleOgreBarbarian));
    EXPECT_FALSE(IsEligible(FemaleOgres, FemaleHumanBarbarian));
    EXPECT_FALSE(IsEligible({}, FemaleOgreBarbarian));
    EXPECT_FALSE(GenderMatches(GenderMaskMale | GenderMaskFemale, 2));
}

TEST(Subclass, TheSubclassMustBeAnotherAllowedClass)
{
    EXPECT_TRUE(AllowsSubclass(FemaleOgres, FemaleOgreBarbarian, 23, 34));
    EXPECT_FALSE(AllowsSubclass(FemaleOgres, FemaleOgreBarbarian, 12, 2));
    EXPECT_FALSE(AllowsSubclass(FemaleOgres, FemaleOgreBarbarian, 0, 34));
    EXPECT_FALSE(AllowsSubclass(FemaleOgres, MaleOgreBarbarian, 23, 34));
    std::vector<Pair> const deathOnly = { { 53, GenderMaskFemale, 12, 23, 34 } };
    EXPECT_TRUE(AllowsSubclass(deathOnly, FemaleOgreBarbarian, 23, 34));
    EXPECT_FALSE(AllowsSubclass(deathOnly, FemaleOgreBarbarian, 23, 35));
    EXPECT_FALSE(AllowsSubclass(deathOnly, FemaleOgreBarbarian, 24, 34));
}

TEST(Subclass, BorrowedTreesAreTheClassTreeAndTheChosenSpecialization)
{
    std::vector<Tree> const trees = { { 23, 0 }, { 23, 34 } };
    EXPECT_EQ(BorrowedTrees(NecromancerDeath, true), trees);
    EXPECT_TRUE(BorrowedTrees({ 23, 0 }, true).empty());
    EXPECT_TRUE(BorrowedTrees({}, true).empty());
}

TEST(Subclass, ClassesWithoutAClassTreeLendAllTheirSpecializations)
{
    std::vector<std::uint16_t> const mageSpecs = { 86, 87, 88 };
    std::vector<Tree> const arcaneFireFrost = { { 108, 86 }, { 108, 87 }, { 108, 88 } };
    EXPECT_EQ(BorrowedTrees({ 108, 86 }, false, mageSpecs), arcaneFireFrost);
    EXPECT_EQ(BorrowedTrees({ 108, 88 }, false, mageSpecs), arcaneFireFrost);
    EXPECT_EQ(OpenTrees(Settings{}, FemaleOgres, FemaleOgreBarbarian, { 108, 86 }, false, mageSpecs),
        arcaneFireFrost);
    EXPECT_TRUE(BorrowedTrees({ 108, 86 }, false).empty());
    EXPECT_TRUE(BorrowedTrees({ 108, 0 }, false, mageSpecs).empty());
    std::vector<Pair> const fireOnly = { { 53, GenderMaskFemale, 0, 108, 86 } };
    EXPECT_TRUE(OpenTrees(Settings{}, fireOnly, FemaleOgreBarbarian, { 108, 87 }, false, mageSpecs).empty());
}

TEST(Subclass, ABorrowedClassWithoutAClassTreeTurnsItsClassPointsIntoSpecializationPoints)
{
    Settings settings;
    std::uint32_t classPoints = 36;
    std::uint32_t specializationPoints = 35;
    AddBonusPoints(settings, false, classPoints, specializationPoints);
    EXPECT_EQ(classPoints, 36u);
    EXPECT_EQ(specializationPoints, 106u);
    classPoints = 36;
    specializationPoints = 35;
    AddBonusPoints(settings, true, classPoints, specializationPoints);
    EXPECT_EQ(classPoints, 72u);
    EXPECT_EQ(specializationPoints, 70u);
}

TEST(Subclass, TreesOpenOnlyWhenCompleteEligibleAndLevelled)
{
    Settings settings;
    EXPECT_EQ(OpenTrees(settings, FemaleOgres, FemaleOgreBarbarian, NecromancerDeath, true),
        BorrowedTrees(NecromancerDeath, true));
    EXPECT_TRUE(OpenTrees(settings, FemaleOgres, MaleOgreBarbarian, NecromancerDeath, true).empty());
    EXPECT_TRUE(OpenTrees(settings, FemaleOgres, { 53, 1, 12, 19 }, NecromancerDeath, true).empty());
    EXPECT_FALSE(OpenTrees(settings, FemaleOgres, { 53, 1, 12, 20 }, NecromancerDeath, true).empty());
    EXPECT_TRUE(OpenTrees(settings, FemaleOgres, { 53, 1, 23, 80 }, NecromancerDeath, true).empty());
    EXPECT_TRUE(OpenTrees(settings, FemaleOgres, FemaleOgreBarbarian, {}, true).empty());
    settings.Enabled = false;
    EXPECT_TRUE(OpenTrees(settings, FemaleOgres, FemaleOgreBarbarian, NecromancerDeath, true).empty());
}

TEST(Subclass, BonusPointsFollowTheConfiguredPercentage)
{
    Settings settings;
    EXPECT_EQ(BonusPoints(settings, 36), 36u);
    settings.BorrowedPointsPercent = 50;
    EXPECT_EQ(BonusPoints(settings, 35), 17u);
    settings.BorrowedPointsPercent = 0;
    EXPECT_EQ(BonusPoints(settings, 36), 0u);
    settings.BorrowedPointsPercent = UINT32_MAX;
    EXPECT_EQ(BonusPoints(settings, UINT32_MAX), UINT32_MAX);
}

TEST(Subclass, TrimmingDropsChoicesTheCharacterNoLongerQualifiesFor)
{
    EXPECT_EQ(Trimmed(FemaleOgres, FemaleOgreBarbarian, NecromancerDeath), NecromancerDeath);
    EXPECT_TRUE(Trimmed(FemaleOgres, MaleOgreBarbarian, NecromancerDeath).Empty());
    std::vector<Pair> const pyromancyOnly = { { 53, GenderMaskFemale, 0, 24, 0 } };
    EXPECT_TRUE(Trimmed(pyromancyOnly, FemaleOgreBarbarian, NecromancerDeath).Empty());
    EXPECT_TRUE(Trimmed(FemaleOgres, FemaleOgreBarbarian, { 23, 0 }).Empty());
}

TEST(Subclass, TheSecondHeadOffersTheQuestOnceToAnEligibleUndecidedCharacter)
{
    Settings settings;
    EXPECT_TRUE(OffersQuest(settings, FemaleOgres, { 53, 1, 12, 20 }, {}, 0, true));
    EXPECT_FALSE(OffersQuest(settings, FemaleOgres, { 53, 1, 12, 19 }, {}, 0, true));
    EXPECT_FALSE(OffersQuest(settings, FemaleOgres, MaleOgreBarbarian, {}, 0, true));
    EXPECT_FALSE(OffersQuest(settings, FemaleOgres, FemaleOgreBarbarian, NecromancerDeath, 0, true));
    EXPECT_FALSE(OffersQuest(settings, FemaleOgres, FemaleOgreBarbarian, {}, 1, true));
    EXPECT_FALSE(OffersQuest(settings, FemaleOgres, FemaleOgreBarbarian, {}, 0, false));
    settings.Enabled = false;
    EXPECT_FALSE(OffersQuest(settings, FemaleOgres, FemaleOgreBarbarian, {}, 0, true));
}

TEST(Subclass, BooksDropOnlyAfterTheFirstQuestForEligibleCharacters)
{
    Settings settings;
    EXPECT_TRUE(FindsBooks(settings, FemaleOgres, FemaleOgreBarbarian, true));
    EXPECT_FALSE(FindsBooks(settings, FemaleOgres, FemaleOgreBarbarian, false));
    EXPECT_FALSE(FindsBooks(settings, FemaleOgres, MaleOgreBarbarian, true));
    settings.BookDropChance = 0.0f;
    EXPECT_FALSE(FindsBooks(settings, FemaleOgres, FemaleOgreBarbarian, true));
}

TEST(Subclass, TheClassMenuListsOtherAllowedClasses)
{
    EXPECT_TRUE(AllowsAnySpecializationOf(FemaleOgres, FemaleOgreBarbarian, 23));
    EXPECT_FALSE(AllowsAnySpecializationOf(FemaleOgres, FemaleOgreBarbarian, 12));
    std::vector<Pair> const deathOnly = { { 53, GenderMaskFemale, 12, 23, 34 } };
    EXPECT_TRUE(AllowsAnySpecializationOf(deathOnly, FemaleOgreBarbarian, 23));
    EXPECT_FALSE(AllowsAnySpecializationOf(deathOnly, FemaleOgreBarbarian, 24));
}

TEST(Subclass, EachRaceFollowsItsOwnStory)
{
    std::vector<Story> const stories = { { 0, GenderMaskMale | GenderMaskFemale, 1, 2 },
        { 32, GenderMaskMale, 9530020, 9530020 } };
    Settings const settings;
    Settings const twins = WithStory(settings, stories, { 32, 0, 12, 20 });
    EXPECT_EQ(twins.QuestId, 9530020u);
    EXPECT_EQ(twins.BookItemId, 9530020u);
    EXPECT_EQ(WithStory(settings, stories, FemaleOgreBarbarian).QuestId, 1u);
    EXPECT_EQ(WithStory(settings, { stories[1] }, FemaleOgreBarbarian).QuestId, settings.QuestId);
    EXPECT_EQ(WithStory(settings, { stories[1] }, { 32, 1, 12, 20 }).BookItemId, settings.BookItemId);
}

TEST(Subclass, TheTreeCountLimitsTheBorrowedSpecializations)
{
    std::vector<std::uint16_t> const mageSpecs = { 86, 87, 88 };
    std::vector<Tree> const fire = { { 108, 86 } };
    std::vector<Tree> const fireFrost = { { 108, 86 }, { 108, 88 } };
    std::vector<Tree> const all = { { 108, 86 }, { 108, 87 }, { 108, 88 } };
    EXPECT_EQ(BorrowedTrees({ 108, 86, 88 }, false, mageSpecs, 1), fire);
    EXPECT_EQ(BorrowedTrees({ 108, 86, 88 }, false, mageSpecs, 2), fireFrost);
    EXPECT_EQ(BorrowedTrees({ 108, 86 }, false, mageSpecs, 2), fire);
    EXPECT_EQ(BorrowedTrees({ 108, 86 }, false, mageSpecs, 3), all);
    std::vector<std::uint16_t> const necromancerSpecs = { 34, 35, 36 };
    std::vector<Tree> const classAndTwo = { { 23, 0 }, { 23, 34 }, { 23, 36 } };
    EXPECT_EQ(BorrowedTrees({ 23, 34, 36 }, true, necromancerSpecs, 2), classAndTwo);
    EXPECT_EQ(BorrowedTrees({ 23, 34 }, true, necromancerSpecs, 1), (std::vector<Tree>{ { 23, 0 }, { 23, 34 } }));
}

TEST(Subclass, ClassKindsFilterMainsAndSubclasses)
{
    EXPECT_TRUE(KindAllows(ClassKinds::All, true));
    EXPECT_TRUE(KindAllows(ClassKinds::All, false));
    EXPECT_TRUE(KindAllows(ClassKinds::Standard, true));
    EXPECT_FALSE(KindAllows(ClassKinds::Standard, false));
    EXPECT_FALSE(KindAllows(ClassKinds::CoA, true));
    EXPECT_TRUE(KindAllows(ClassKinds::CoA, false));
}

TEST(Subclass, TheCyclopsEchoesTheHelmetUntilSheChoosesHerOwn)
{
    EXPECT_EQ(CyclopsHelmet(false, false, 12640, 0), 12640u);
    EXPECT_EQ(CyclopsHelmet(false, false, 12640, 16955), 12640u);
    EXPECT_EQ(CyclopsHelmet(false, true, 12640, 16955), 0u);
    EXPECT_EQ(CyclopsHelmet(true, false, 12640, 0), 12640u);
    EXPECT_EQ(CyclopsHelmet(true, true, 12640, 0), 0u);
    EXPECT_EQ(CyclopsHelmet(true, true, 12640, 16955), 16955u);
    EXPECT_EQ(CyclopsHelmet(true, false, 12640, BareCyclopsHead), 0u);
}

TEST(Subclass, AStandardMainBorrowsTheCoAClassBudgetForTheLentTrees)
{
    Subclass::Settings settings;
    std::vector<Subclass::Tree> const trees = { { 12, 0 }, { 12, 4001 } };
    std::uint32_t classPoints = 0;
    std::uint32_t specializationPoints = 0;
    Subclass::AddLentClassPoints(settings, true, Subclass::SpecializationTreeCount(trees), 36, 35, classPoints,
        specializationPoints);
    EXPECT_EQ(classPoints, 36u);
    EXPECT_EQ(specializationPoints, 35u);

    settings.BorrowedPointsPercent = 50;
    classPoints = 0;
    specializationPoints = 0;
    Subclass::AddLentClassPoints(settings, false, 2, 36, 35, classPoints, specializationPoints);
    EXPECT_EQ(classPoints, 0u);
    EXPECT_EQ(specializationPoints, 35u);
}

TEST(Subclass, TheOtherHalfHealsWhatIsMissingWithoutOverhealing)
{
    EXPECT_FALSE(HealWorthCasting(500, 0, 100.0f));
    EXPECT_FALSE(HealWorthCasting(100, 1000, 90.0f));
    EXPECT_TRUE(HealWorthCasting(800, 1000, 70.0f));
    EXPECT_FALSE(HealWorthCasting(3000, 1000, 70.0f));
    EXPECT_FALSE(HealWorthCasting(9000, 6000, 40.0f));
    EXPECT_TRUE(HealWorthCasting(9000, 8000, 20.0f));
}

TEST(Subclass, TheMostHurtFriendGetsTheBiggestFittingHeal)
{
    EXPECT_TRUE(BetterHeal(30.0f, 500, 60.0f, 2000));
    EXPECT_FALSE(BetterHeal(60.0f, 2000, 30.0f, 500));
    EXPECT_TRUE(BetterHeal(50.0f, 2000, 50.0f, 500));
}

TEST(Subclass, TheOtherHalfTauntsOnlyWhatATankLostToAFriend)
{
    EXPECT_TRUE(TauntNeeded(true, false, true));
    EXPECT_FALSE(TauntNeeded(true, true, false));
    EXPECT_FALSE(TauntNeeded(false, false, true));
    EXPECT_FALSE(TauntNeeded(true, false, false));
}

TEST(Subclass, BotsTakeTheLeastUsedSubclass)
{
    EXPECT_EQ(LeastUsed({ 3, 1, 2, 1 }), 1u);
    EXPECT_EQ(LeastUsed({ 0, 0 }), 0u);
}

TEST(Subclass, ABotSpreadsItsBorrowedPointsRankByRank)
{
    std::vector<Offer> const offers = {
        { 1, 3, 0, 1, 10, {} },
        { 2, 2, 0, 1, 10, {} },
        { 3, 1, 1, 0, 10, {} },
        { 4, 1, 0, 1, 60, {} },
        { 5, 1, 0, 1, 10, { 9 } },
        { 6, 1, 0, 1, 10, { 3 } }
    };
    std::vector<Rank> const planned = PlanRanks(offers, { { 1, 1 } }, 40, 1, 4);
    EXPECT_EQ(RankIn(planned, 1), 3u);
    EXPECT_EQ(RankIn(planned, 2), 1u);
    EXPECT_EQ(RankIn(planned, 3), 1u);
    EXPECT_EQ(RankIn(planned, 4), 0u);
    EXPECT_EQ(RankIn(planned, 5), 0u);
    EXPECT_EQ(RankIn(planned, 6), 1u);
}

TEST(Subclass, ABorrowedTalentOpensFromAnyLinkedTalent)
{
    std::vector<Offer> offers = {
        { 1, 1, 1, 0, 10, {} },
        { 2, 2, 1, 0, 10, {} },
        { 3, 1, 1, 0, 10, {} },
        { 4, 1, 1, 0, 10, {} },
    };
    offers[2].Links = { 1, 2 };
    offers[3].Links = { 2 };
    offers[3].LinksMustBeMaxed = true;
    EXPECT_EQ(GateOf(offers, {}, offers[2], 20), Gate::Link);
    EXPECT_EQ(GateOf(offers, { { 2, 1 } }, offers[2], 20), Gate::Open);
    EXPECT_EQ(GateOf(offers, { { 2, 1 } }, offers[3], 20), Gate::Link);
    EXPECT_EQ(GateOf(offers, { { 2, 2 } }, offers[3], 20), Gate::Open);
    EXPECT_EQ(GateOf(offers, { { 2, 2 } }, offers[3], 9), Gate::Level);
}

TEST(Subclass, ABorrowedTalentRowOpensWithPointsSpentInLowerRows)
{
    std::vector<Offer> offers = {
        { 1, 5, 1, 0, 10, {} },
        { 2, 5, 1, 0, 10, {} },
        { 3, 1, 1, 0, 10, {} },
        { 4, 1, 0, 1, 10, {} },
    };
    offers[1].ClassInvestment = 5;
    offers[2].ClassInvestment = 9;
    offers[3].SpecId = 7;
    offers[3].SpecInvestment = 1;
    EXPECT_EQ(GateOf(offers, { { 1, 4 } }, offers[1], 20), Gate::Investment);
    EXPECT_EQ(GateOf(offers, { { 1, 5 } }, offers[1], 20), Gate::Open);
    EXPECT_EQ(GateOf(offers, { { 1, 5 }, { 2, 3 } }, offers[2], 20), Gate::Investment);
    EXPECT_EQ(GateOf(offers, { { 1, 5 }, { 2, 4 } }, offers[2], 20), Gate::Open);
    EXPECT_EQ(GateOf(offers, { { 1, 5 } }, offers[3], 20), Gate::Investment);
}

TEST(Subclass, OnlyOneTalentOfAChoiceGroupIsHeld)
{
    std::vector<Offer> offers = {
        { 1, 1, 1, 0, 10, {} },
        { 2, 1, 1, 0, 10, {} },
        { 3, 1, 1, 0, 10, {} },
    };
    offers[0].Group = offers[1].Group = 681436;
    offers[2].Group = SharedGroup;
    EXPECT_EQ(GateOf(offers, { { 1, 1 } }, offers[1], 20), Gate::Group);
    EXPECT_EQ(GateOf(offers, { { 1, 1 } }, offers[0], 20), Gate::Open);
    EXPECT_EQ(GateOf(offers, { { 1, 1 } }, offers[2], 20), Gate::Open);
    EXPECT_EQ(GateOf(offers, {}, { 9, 1, 1, 0, 10, { 5 } }, 20), Gate::Requirement);
}

TEST(Subclass, UnlearningATalentMayNotStrandTheTalentsBuiltOnIt)
{
    std::vector<Offer> offers = {
        { 1, 1, 1, 0, 10, {} },
        { 2, 1, 1, 0, 10, {} },
        { 3, 1, 1, 0, 10, {} },
    };
    offers[1].Links = { 1 };
    offers[2].Links = { 9 };
    std::vector<Rank> const before = { { 1, 1 }, { 2, 1 }, { 3, 1 } };
    EXPECT_EQ(StrandedBy(offers, before, { { 1, 0 }, { 2, 1 }, { 3, 1 } }, 20), 2u);
    EXPECT_EQ(StrandedBy(offers, before, { { 1, 1 }, { 2, 0 }, { 3, 1 } }, 20), 0u);
}

TEST(Subclass, ABotOnlySpendsWhereTheTreeIsOpen)
{
    std::vector<Offer> offers = {
        { 1, 1, 1, 0, 10, {} },
        { 2, 1, 1, 0, 10, {} },
        { 3, 1, 1, 0, 10, {} },
    };
    offers[0].Links = { 2 };
    offers[2].Links = { 9 };
    std::vector<Rank> const planned = PlanRanks(offers, {}, 40, 5, 0);
    EXPECT_EQ(RankIn(planned, 1), 1u);
    EXPECT_EQ(RankIn(planned, 2), 1u);
    EXPECT_EQ(RankIn(planned, 3), 0u);
}

TEST(Subclass, TheTwinsSplitOnePoolIntoTwoHalves)
{
    EXPECT_FLOAT_EQ(SplitShare(-1.0f, 0, 1000, 500.0f, false), 500.0f);
    EXPECT_FLOAT_EQ(SplitShare(500.0f, 1000, 800, 500.0f, true), 300.0f);
    EXPECT_FLOAT_EQ(SplitShare(300.0f, 800, 600, 500.0f, false), 300.0f);
    EXPECT_FLOAT_EQ(SplitShare(300.0f, 600, 700, 500.0f, false), 350.0f);
    EXPECT_FLOAT_EQ(SplitShare(350.0f, 700, 100, 500.0f, false), 100.0f);
    EXPECT_FLOAT_EQ(SplitShare(100.0f, 100, 0, 500.0f, true), 0.0f);
    EXPECT_FLOAT_EQ(SplitShare(450.0f, 900, 1000, 500.0f, false), 500.0f);
}

TEST(Subclass, ABorrowedRankCostsPointsFromItsOwnPool)
{
    Offer const classNode = { 1, 2, 1, 0, 10, {}, 0 };
    Offer const specNode = { 2, 3, 0, 1, 10, {}, 99 };
    EXPECT_TRUE(Affords(classNode, 0, 1, 1, 0));
    EXPECT_FALSE(Affords(classNode, 0, 1, 0, 5));
    EXPECT_FALSE(Affords(classNode, 0, 2, 1, 0));
    EXPECT_TRUE(Affords(specNode, 1, 3, 0, 2));
    EXPECT_FALSE(Affords(specNode, 0, 1, 5, 0));
    EXPECT_TRUE(Affords(specNode, 2, 1, 0, 0));
}

TEST(Subclass, ARiderKeepsTheWeaponsHeHoldsWhenHandedBlankSlots)
{
    HeldWeapons held;
    HoldShownWeapons(held, { { 5200, 0, 125796 }, { 7, 0, 0 } });
    HoldShownWeapons(held, { { 0, 0, 125796 }, { 0, 0, 0 } });
    EXPECT_EQ(held.Entries[0], 5200u);
    EXPECT_EQ(held.Enchants[0], 7u);
    EXPECT_EQ(held.Entries[2], 125796u);
    HoldShownWeapons(held, { { 3441, 0, 0 }, { 0, 0, 0 } });
    EXPECT_EQ(held.Entries[0], 3441u);
    EXPECT_EQ(held.Enchants[0], 0u);
}
