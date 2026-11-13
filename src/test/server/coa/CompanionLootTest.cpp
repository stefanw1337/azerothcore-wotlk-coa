#include "AscensionCompanionLoot.h"
#include <gtest/gtest.h>

using namespace Ascension::CompanionLooting;

TEST(CompanionLooting, LootbotAndKleptomanicDemonLootByThemselves)
{
    EXPECT_TRUE(IsSelfLootingCompanion(44022));
    EXPECT_TRUE(IsSelfLootingCompanion(79026));
    EXPECT_FALSE(IsSelfLootingCompanion(0));
    EXPECT_FALSE(IsSelfLootingCompanion(7385));
}

TEST(CompanionLooting, ActiveAppearanceLootsEverywhere)
{
    EXPECT_TRUE(LootsWith(true, false, false));
    EXPECT_TRUE(LootsWith(true, false, true));
    EXPECT_TRUE(LootsWith(true, true, true));
}

TEST(CompanionLooting, SelfLootingCompanionStopsInRaids)
{
    EXPECT_TRUE(LootsWith(false, true, false));
    EXPECT_FALSE(LootsWith(false, true, true));
}

TEST(CompanionLooting, OrdinaryCompanionWithoutAppearanceDoesNotLoot)
{
    EXPECT_FALSE(LootsWith(false, false, false));
    EXPECT_FALSE(LootsWith(false, false, true));
}

TEST(CompanionLooting, SkinningNeedsTheSkinPeeler)
{
    EXPECT_TRUE(SkinsWith(true));
    EXPECT_FALSE(SkinsWith(false));
}
