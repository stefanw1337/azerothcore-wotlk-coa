/* Copyright (C) 2016+ AzerothCore, GNU AGPL v3. */

#ifndef ASCENSION_WARCRAFT_REBORN_RULES_H
#define ASCENSION_WARCRAFT_REBORN_RULES_H

#include "AscensionFreepickRules.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace AscensionWarcraftReborn
{
constexpr std::uint32_t REBORN_SPELL_OFFSET = 1100000;
constexpr std::uint32_t REBORN_SKILL_OFFSET = 11000;
constexpr std::uint32_t ROW_ALTERNATE = 0x80000;
constexpr std::size_t REQUIRED_ABILITY_COUNT = 3;
constexpr std::uint32_t DARK_APOTHEOSIS = 1154321;
constexpr std::array<std::uint32_t, 2> DARK_APOTHEOSIS_PASSIVES = { 1154322, 1154323 };
constexpr std::array<std::uint32_t, 3> DARK_APOTHEOSIS_SPELLS = { 1159674, 1161363, 1161294 };
constexpr std::array<std::uint32_t, 56> NON_WOTLK_ENTRIES = {
    1359, 1693, 1754, 1897, 1898, 1899, 1904, 1905, 1931, 1948, 1962, 2007, 2018, 2019, 2023, 2026,
    2034, 2061, 2065, 2066, 2076, 2081, 2083, 2135, 2137, 2139, 12797, 12798, 12799, 12800, 13099,
    13400, 13401, 13537, 13538, 13653, 13655, 13659, 13701, 13825, 13853, 14242, 14243, 14244, 18572,
    18573, 18619, 18657, 18804, 18805, 18806, 18836, 18840, 18843, 32232, 137836 };
constexpr std::array<std::uint32_t, 20> NON_WOTLK_SPELLS = {
    1100760, 1106118, 1122361, 1122486, 1122536, 1182010, 1325782, 1325894, 1325898, 1325899, 1325916,
    1325918, 1327141, 1327143, 1348933, 1348934, 1348937, 1348938, 1954807, 1954861 };

constexpr bool WotlkEntry(std::uint32_t entryId)
{
    return std::find(NON_WOTLK_ENTRIES.begin(), NON_WOTLK_ENTRIES.end(), entryId) == NON_WOTLK_ENTRIES.end();
}

struct Grant
{
    std::uint32_t Source;
    std::vector<std::uint32_t> Spells;
};

std::vector<Grant> const& Grants();

struct Data
{
    AscensionFreepick::Catalog Catalog;
    std::unordered_map<std::uint32_t, std::vector<std::uint32_t>> Ladders;
    std::unordered_map<std::uint32_t, std::uint32_t> FirstRanks;
    std::unordered_set<std::uint32_t> Trainable;
    std::unordered_map<std::uint32_t, std::vector<std::uint32_t>> SkillLines;
    std::unordered_set<std::uint32_t> Automatic;

    [[nodiscard]] std::vector<std::uint32_t> const* LadderOf(std::uint32_t spellId) const;
};

bool LoadData(Data& data);

std::vector<std::vector<std::uint32_t>> RankChains(Data const& data);

std::vector<std::uint32_t> NonWotlkSpells(Data const& data);

std::vector<std::uint32_t> StartingSpells(Data const& data, AscensionFreepick::Realm const& realm,
    std::uint32_t classId, std::uint32_t level);
std::vector<std::uint32_t> ClassSkillLines(Data const& data, AscensionFreepick::Realm const& realm,
    std::uint32_t classId);
std::vector<std::uint32_t> AutomaticSpells(Data const& data, AscensionFreepick::Realm const& realm,
    std::uint32_t classId, std::uint32_t level);

struct TrainerSpell
{
    std::uint32_t SpellId = 0;
    std::uint32_t MoneyCost = 0;
    std::uint32_t ReqSkillLine = 0;
    std::uint32_t ReqSkillRank = 0;
    std::array<std::uint32_t, REQUIRED_ABILITY_COUNT> ReqAbility{};
    std::uint32_t ReqLevel = 0;
};

using SpellExists = std::function<bool(std::uint32_t spellId)>;
using RebornRow = std::function<TrainerSpell(std::uint32_t spellId)>;

std::uint32_t RebornSpell(std::uint32_t spellId, SpellExists const& exists);

struct SpellLayout
{
    std::string Name;
    std::array<std::uint32_t, 3> Effects{};
    std::array<std::uint32_t, 3> Auras{};

    bool operator==(SpellLayout const&) const = default;
};

using SpellLayoutOf = std::function<std::optional<SpellLayout>(std::uint32_t spellId)>;

std::vector<std::pair<std::uint32_t, std::uint32_t>> SpellTwins(std::uint32_t spellCount, SpellLayoutOf const& layout);

std::vector<std::pair<std::uint32_t, std::uint32_t>> SpellNamesakes(std::uint32_t spellCount, SpellLayoutOf const& layout);

using LaterRanks = std::function<std::vector<std::uint32_t>(std::uint32_t spellId)>;
using FirstRank = std::function<std::uint32_t(std::uint32_t spellId)>;

std::vector<std::pair<std::uint32_t, std::uint32_t>> RankTwins(std::vector<std::pair<std::uint32_t, std::uint32_t>> const& twins,
    LaterRanks const& laterRanks, FirstRank const& firstRank, SpellLayoutOf const& layout);

std::vector<TrainerSpell> TrainerSpells(Data const& data, AscensionFreepick::Realm const& realm, std::uint32_t classId,
    std::vector<TrainerSpell> const& stock, SpellExists const& exists, RebornRow const& reborn);
}

#endif
