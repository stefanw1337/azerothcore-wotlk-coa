#include "CoACreatureScalingPolicy.h"
#include <gtest/gtest.h>

namespace
{
    using CreatureScaling::CanBeHandedOver;
    using CreatureScaling::Context;
    using CreatureScaling::Multiplier;
    using CreatureScaling::Settings;

    constexpr std::uint32_t Deadmines = 36;
    constexpr std::uint32_t BlackrockSpire = 229;
    constexpr std::uint32_t BlackwingLair = 469;
    constexpr std::uint32_t NoOverrideMap = 33;
    constexpr bool Owned = true;
    constexpr bool NotOwned = false;
}

TEST(CreatureScalingAuthority, OnlyDungeonAndRaidCanBeHandedOver)
{
    EXPECT_TRUE(CanBeHandedOver(Context::Dungeon));
    EXPECT_TRUE(CanBeHandedOver(Context::Raid));
    EXPECT_FALSE(CanBeHandedOver(Context::World));
    EXPECT_FALSE(CanBeHandedOver(Context::None));
}

TEST(CreatureScalingAuthority, UnownedCreaturesKeepTheConfiguredMultipliers)
{
    Settings const settings;
    EXPECT_FLOAT_EQ(Multiplier(settings.health, Context::Dungeon, NoOverrideMap, 20, NotOwned), 2.5f);
    EXPECT_FLOAT_EQ(Multiplier(settings.health, Context::Dungeon, Deadmines, 20, NotOwned), 2.2f);
    EXPECT_FLOAT_EQ(Multiplier(settings.health, Context::Raid, NoOverrideMap, 60, NotOwned), 2.2f);
    EXPECT_FLOAT_EQ(Multiplier(settings.health, Context::Raid, BlackwingLair, 60, NotOwned), 5.0f);
    EXPECT_FLOAT_EQ(Multiplier(settings.damage, Context::Dungeon, NoOverrideMap, 20, NotOwned), 1.5f);
    EXPECT_FLOAT_EQ(Multiplier(settings.damage, Context::Raid, NoOverrideMap, 60, NotOwned), 1.0f);
}

TEST(CreatureScalingAuthority, OwnedDungeonAndRaidCreaturesAreNeutralForHealthAndDamage)
{
    Settings const settings;
    for (std::uint32_t mapId : { NoOverrideMap, Deadmines, BlackrockSpire, BlackwingLair })
    {
        for (Context context : { Context::Dungeon, Context::Raid })
        {
            EXPECT_FLOAT_EQ(Multiplier(settings.health, context, mapId, 20, Owned), 1.0f) << mapId;
            EXPECT_FLOAT_EQ(Multiplier(settings.damage, context, mapId, 20, Owned), 1.0f) << mapId;
        }
    }
}

TEST(CreatureScalingAuthority, OpenWorldIsNeverHandedOver)
{
    Settings const settings;
    float const world = Multiplier(settings.health, Context::World, NoOverrideMap, 40, NotOwned);
    EXPECT_FLOAT_EQ(world, 1.17f + 0.013f * 40.0f);
    EXPECT_FLOAT_EQ(Multiplier(settings.health, Context::World, NoOverrideMap, 40, Owned), world);
    EXPECT_FLOAT_EQ(Multiplier(settings.damage, Context::World, NoOverrideMap, 40, Owned),
        Multiplier(settings.damage, Context::World, NoOverrideMap, 40, NotOwned));
}

TEST(CreatureScalingAuthority, CoreHealthHookSkipsCreaturesABudgetOwnerHasAlreadyFoldedTheMultiplierInto)
{
    Settings const settings;
    float const world = Multiplier(settings.health, Context::World, NoOverrideMap, 40, NotOwned);
    // With a budget owner registered the hook is a no-op, so World.Health reaches the creature once (through the
    // owner), never twice; without one the hook applies it as before.
    EXPECT_FLOAT_EQ(CreatureScaling::HookMultiplier(world, true), 1.0f);
    EXPECT_FLOAT_EQ(CreatureScaling::HookMultiplier(world, false), world);
    EXPECT_FLOAT_EQ(CreatureScaling::HookMultiplier(1.0f, false), 1.0f);
}

TEST(CreatureScalingAuthority, ContextNoneStaysNeutralWhetherOwnedOrNot)
{
    Settings const settings;
    EXPECT_FLOAT_EQ(Multiplier(settings.health, Context::None, Deadmines, 20, NotOwned), 1.0f);
    EXPECT_FLOAT_EQ(Multiplier(settings.health, Context::None, Deadmines, 20, Owned), 1.0f);
}

TEST(CreatureScalingAuthority, WithoutAnOwnerTheResultIsTheConfiguredFormulaOrMapEntry)
{
    Settings const settings;
    auto const expected = [](CreatureScaling::Multipliers const& m, Context context, std::uint32_t mapId, std::uint8_t level)
    {
        if (context == Context::None)
            return 1.0f;
        if (auto itr = m.maps.find(mapId); itr != m.maps.end())
            return itr->second.Evaluate(level);
        return (context == Context::World ? m.world : context == Context::Dungeon ? m.dungeon : m.raid).Evaluate(level);
    };
    for (Context context : { Context::None, Context::World, Context::Dungeon, Context::Raid })
        for (std::uint32_t mapId : { 0u, NoOverrideMap, Deadmines, BlackrockSpire, BlackwingLair })
            for (int level = 1; level <= 80; ++level)
            {
                std::uint8_t const lvl = std::uint8_t(level);
                EXPECT_FLOAT_EQ(Multiplier(settings.health, context, mapId, lvl), expected(settings.health, context, mapId, lvl));
                EXPECT_FLOAT_EQ(Multiplier(settings.damage, context, mapId, lvl, NotOwned), expected(settings.damage, context, mapId, lvl));
            }
}

TEST(CreatureScalingAuthority, OwnedSpellDamageIgnoresCoreMapAndWeaponMultipliers)
{
    Settings settings;
    settings.spellDamage.maps[Deadmines] = CreatureScaling::Formula::Of("7");
    for (bool weaponSpell : { false, true })
        EXPECT_FLOAT_EQ(CreatureScaling::SpellMultiplier(settings, Context::Dungeon, Deadmines, 20,
            weaponSpell, Owned), 1.0f);
    EXPECT_FLOAT_EQ(CreatureScaling::SpellMultiplier(settings, Context::Dungeon, Deadmines, 20,
        false, NotOwned), 7.0f);
    EXPECT_FLOAT_EQ(CreatureScaling::SpellMultiplier(settings, Context::World, NoOverrideMap, 20,
        false, Owned), 1.0f);
}
