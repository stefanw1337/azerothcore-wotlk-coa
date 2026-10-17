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
    EXPECT_EQ(BorrowedTrees(NecromancerDeath), trees);
    EXPECT_TRUE(BorrowedTrees({ 23, 0 }).empty());
    EXPECT_TRUE(BorrowedTrees({}).empty());
}

TEST(Subclass, TreesOpenOnlyWhenCompleteEligibleAndLevelled)
{
    Settings settings;
    EXPECT_EQ(OpenTrees(settings, FemaleOgres, FemaleOgreBarbarian, NecromancerDeath), BorrowedTrees(NecromancerDeath));
    EXPECT_TRUE(OpenTrees(settings, FemaleOgres, MaleOgreBarbarian, NecromancerDeath).empty());
    EXPECT_TRUE(OpenTrees(settings, FemaleOgres, { 53, 1, 12, 19 }, NecromancerDeath).empty());
    EXPECT_FALSE(OpenTrees(settings, FemaleOgres, { 53, 1, 12, 20 }, NecromancerDeath).empty());
    EXPECT_TRUE(OpenTrees(settings, FemaleOgres, { 53, 1, 23, 80 }, NecromancerDeath).empty());
    EXPECT_TRUE(OpenTrees(settings, FemaleOgres, FemaleOgreBarbarian, {}).empty());
    settings.Enabled = false;
    EXPECT_TRUE(OpenTrees(settings, FemaleOgres, FemaleOgreBarbarian, NecromancerDeath).empty());
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
