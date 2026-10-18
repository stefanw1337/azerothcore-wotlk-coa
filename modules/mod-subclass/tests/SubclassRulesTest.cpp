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

TEST(Subclass, ClassesWithoutAClassTreeLendOnlyTheChosenSpecialization)
{
    std::vector<Tree> const fireAndFrost = { { 108, 86 }, { 108, 87 } };
    EXPECT_EQ(BorrowedTrees({ 108, 86, 87 }, false), fireAndFrost);
    EXPECT_EQ(OpenTrees(Settings{}, FemaleOgres, FemaleOgreBarbarian, { 108, 86, 87 }, false), fireAndFrost);
    EXPECT_TRUE(BorrowedTrees({ 108, 87 }, false).empty());
    EXPECT_TRUE(BorrowedTrees({ 108, 87, 87 }, false).empty());
    std::vector<Pair> const fireOnly = { { 53, GenderMaskFemale, 0, 108, 86 } };
    EXPECT_TRUE(OpenTrees(Settings{}, fireOnly, FemaleOgreBarbarian, { 108, 86, 87 }, false).empty());
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
