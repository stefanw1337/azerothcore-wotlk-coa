/* Copyright (C) 2016+ AzerothCore, GNU AGPL v3. */

#ifndef ASCENSION_COMPANION_LOOT_H
#define ASCENSION_COMPANION_LOOT_H

#include <cstdint>

class Player;

namespace Ascension::CompanionLooting
{
constexpr std::uint32_t LootAppearanceCategory = 38;
constexpr std::uint32_t SkinningAppearanceCategory = 61;
constexpr std::uint32_t LootTransfiguratorAppearance = 47520;
constexpr std::uint32_t SkinPeelerAppearance = 639807;
constexpr std::uint32_t LootTransfiguratorSpell = 84419;
constexpr std::uint32_t SkinPeelerSpell = 92864;
constexpr std::uint32_t Lootbot3000Creature = 44022;
constexpr std::uint32_t KleptomanicDemonCreature = 79026;

constexpr bool IsSelfLootingCompanion(std::uint32_t creatureEntry)
{
    return creatureEntry == Lootbot3000Creature || creatureEntry == KleptomanicDemonCreature;
}

constexpr bool LootsWith(bool appearanceActive, bool selfLootingCompanionOut, bool inRaid)
{
    return appearanceActive || (selfLootingCompanionOut && !inRaid);
}

constexpr bool SkinsWith(bool appearanceActive)
{
    return appearanceActive;
}

[[nodiscard]] bool HasLootAppearance(Player const* player);
[[nodiscard]] bool HasSkinningAppearance(Player const* player);
[[nodiscard]] bool CompanionLoots(Player const* player);
[[nodiscard]] bool CompanionSkins(Player const* player);
[[nodiscard]] float LootRadius(Player const* player);
[[nodiscard]] float SkinningRadius(Player const* player);
}

#endif
