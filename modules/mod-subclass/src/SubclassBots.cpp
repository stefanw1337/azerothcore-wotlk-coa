/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license.
 */

#include "AscensionCoATalentData.h"
#include "AscensionSpecialization.h"
#include "Chat.h"
#include "Config.h"
#include "PlayerScript.h"
#include "DBCStores.h"
#include "DatabaseEnv.h"
#include "Log.h"
#include "Player.h"
#include "WorldScript.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "SubclassRules.h"
#include "WorldSession.h"
#include <algorithm>
#include <map>
#include <mutex>
#include <numeric>
#include <random>
#include <unordered_map>

bool ForcedBot(Player const* player);
std::vector<Subclass::Choice> BotChoicesFor(Player const* player);
bool SubclassEligible(Player const* player);
Subclass::Choice SubclassChoiceOf(Player const* player);
void ApplySubclassChoice(Player* player, Subclass::Choice const& choice);
std::vector<AscensionCompatData::CoATalentEntry const*> OpenBorrowedEntries(Player const* player);
Subclass::Offer BorrowedOffer(AscensionCompatData::CoATalentEntry const& entry);
std::string DescribeSubclassChoice(Subclass::Choice const& choice);
std::string SubclassClassName(uint8 classId);
std::vector<uint32> KnownBorrowedActives(Player const* player);
uint8 SubclassMinLevel();
uint8 StoryGenderOf(Player const* player);
void SpendBotPoints(Player* player);

namespace
{
uint64 ChoiceKey(uint8 mainClass, Subclass::Choice const& choice)
{
    return (uint64(mainClass) << 40) | (uint64(choice.SubClass) << 32) | (uint64(choice.SubSpec) << 16) |
        choice.SecondSpec;
}

uint8 MainOfKey(uint64 key)
{
    return uint8(key >> 40);
}

Subclass::Choice ChoiceOfKey(uint64 key)
{
    return { uint8(key >> 32), uint16(key >> 16), uint16(key) };
}

struct ForcedBotRow
{
    uint64 Choice = 0;
    uint8 Race = 0;
};

struct BotState
{
    uint64 Choice = 0;
    uint8 Race = 0;
    uint8 Level = 0;
    bool Trees = false;
    uint32 Spells = 0;
    bool BuildRefused = false;
    std::string Refusal;
};

struct CastTally
{
    uint32 Ok = 0;
    uint32 Failed = 0;
    std::map<std::pair<uint32, uint32>, uint32> Failures;
};

std::mutex& botMutex = *new std::mutex;
std::atomic<uint32> rebuildMs{ 4 * HOUR * IN_MILLISECONDS };
std::unordered_map<uint32, uint32>& rebuildIn = *new std::unordered_map<uint32, uint32>;
std::unordered_map<uint32, uint32>& buildRolls = *new std::unordered_map<uint32, uint32>;
std::unordered_map<uint32, ForcedBotRow>& forcedBots = *new std::unordered_map<uint32, ForcedBotRow>;
std::unordered_map<uint32, BotState>& onlineBots = *new std::unordered_map<uint32, BotState>;
std::map<uint64, CastTally>& castsByChoice = *new std::map<uint64, CastTally>;
std::map<uint8, CastTally>& castsByRace = *new std::map<uint8, CastTally>;

void GiveTheStoryGender(Player* player)
{
    uint8 const race = player->getRace();
    if (race != Subclass::TwoHeadedOgreRace && race != Subclass::ThinHumanRace)
        return;
    uint8 const gender = race == Subclass::TwoHeadedOgreRace ? GENDER_FEMALE : GENDER_MALE;
    if (StoryGenderOf(player) == gender)
        return;
    player->SetByteValue(UNIT_FIELD_BYTES_0, 2, gender);
    player->SetByteValue(PLAYER_BYTES_3, 0, gender);
    player->SetUInt32Value(PLAYER_BYTES, 0);
    player->SetByteValue(PLAYER_BYTES_2, 0, 0);
    player->InitDisplayIds();
    LOG_INFO("module", "mod-subclass: bot {} of race {} takes the {} body of its subclass story", player->GetName(),
        uint32(race), gender == GENDER_FEMALE ? "female" : "male");
}

bool IsBot(Player const* player)
{
    WorldSession const* session = player->GetSession();
    return session && session->IsBot();
}

void LoadForcedBots()
{
    CharacterDatabase.DirectExecute("CREATE TABLE IF NOT EXISTS `mod_subclass_bot` ("
        "`guid` INT UNSIGNED NOT NULL, `race` TINYINT UNSIGNED NOT NULL, `main_class` TINYINT UNSIGNED NOT NULL, "
        "`sub_class` TINYINT UNSIGNED NOT NULL, `sub_spec` SMALLINT UNSIGNED NOT NULL, "
        "`sub_spec2` SMALLINT UNSIGNED NOT NULL, PRIMARY KEY (`guid`)) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4");
    std::unordered_map<uint32, ForcedBotRow> loaded;
    if (QueryResult result = CharacterDatabase.Query(
            "SELECT `guid`, `race`, `main_class`, `sub_class`, `sub_spec`, `sub_spec2` FROM `mod_subclass_bot`"))
        do
        {
            Field* fields = result->Fetch();
            Subclass::Choice const choice = { fields[3].Get<uint8>(), fields[4].Get<uint16>(),
                fields[5].Get<uint16>() };
            loaded[fields[0].Get<uint32>()] = { ChoiceKey(fields[2].Get<uint8>(), choice), fields[1].Get<uint8>() };
        } while (result->NextRow());
    std::lock_guard<std::mutex> lock(botMutex);
    forcedBots = std::move(loaded);
    LOG_INFO("module", "mod-subclass: {} bot(s) carry a forced subclass", forcedBots.size());
}

Subclass::Choice LeastUsedChoice(uint8 mainClass, std::vector<Subclass::Choice> const& options)
{
    std::vector<uint32> counts(options.size(), 0);
    std::lock_guard<std::mutex> lock(botMutex);
    for (auto const& [guid, row] : forcedBots)
        for (std::size_t index = 0; index < options.size(); ++index)
            if (row.Choice == ChoiceKey(mainClass, options[index]))
                ++counts[index];
    return options[Subclass::LeastUsed(counts)];
}

void NoteBotState(Player* player, Optional<std::string> refusal)
{
    Subclass::Choice const choice = SubclassChoiceOf(player);
    BotState state;
    state.Choice = ChoiceKey(player->getClass(), choice);
    state.Race = player->getRace();
    state.Level = player->GetLevel();
    state.Trees = !OpenBorrowedEntries(player).empty();
    state.Spells = uint32(KnownBorrowedActives(player).size());
    std::lock_guard<std::mutex> lock(botMutex);
    BotState& stored = onlineBots[player->GetGUID().GetCounter()];
    if (!refusal)
    {
        state.BuildRefused = stored.BuildRefused;
        state.Refusal = stored.Refusal;
    }
    else
    {
        state.BuildRefused = !refusal->empty();
        state.Refusal = *refusal;
    }
    stored = state;
}

std::vector<Subclass::Offer> OffersOf(std::vector<AscensionCompatData::CoATalentEntry const*> entries,
    uint32 seed)
{
    std::sort(entries.begin(), entries.end(), [](auto const* left, auto const* right)
    {
        return left->EntryId < right->EntryId;
    });
    std::shuffle(entries.begin(), entries.end(), std::mt19937(seed));
    std::vector<Subclass::Offer> offers;
    for (AscensionCompatData::CoATalentEntry const* entry : entries)
        offers.push_back(BorrowedOffer(*entry));
    return offers;
}

Optional<std::string> SpendPoints(Player* player)
{
    std::vector<AscensionCompatData::CoATalentEntry const*> const entries = OpenBorrowedEntries(player);
    uint32 classPoints = 0;
    uint32 specializationPoints = 0;
    if (entries.empty() || !GetAscensionTalentPointsLeft(player, classPoints, specializationPoints))
        return std::nullopt;
    std::vector<AscensionCoATalentState::KnownEntry> build = GetAscensionKnownTalentEntries(player);
    std::vector<Subclass::Rank> known;
    for (AscensionCoATalentState::KnownEntry const& entry : build)
        known.push_back({ entry.EntryId, entry.Rank });
    bool changed = false;
    uint32 roll = 0;
    {
        std::lock_guard<std::mutex> lock(botMutex);
        roll = buildRolls[player->GetGUID().GetCounter()];
    }
    uint32 const seed = player->GetGUID().GetCounter() * 2654435761u + roll;
    std::vector<Subclass::Rank> const planned = Subclass::PlanRanks(OffersOf(entries, seed), known, player->GetLevel(),
        classPoints, specializationPoints);
    for (Subclass::Rank const& rank : planned)
    {
        if (rank.Value <= Subclass::RankIn(known, rank.EntryId))
            continue;
        changed = true;
        auto held = std::find_if(build.begin(), build.end(), [&rank](auto const& entry)
        {
            return entry.EntryId == rank.EntryId;
        });
        if (held != build.end())
            held->Rank = rank.Value;
        else
            build.push_back({ rank.EntryId, rank.Value });
    }
    if (!changed)
        return std::nullopt;
    std::string refusal;
    if (!ApplyAscensionTalentBuild(player, build, refusal))
    {
        LOG_INFO("module", "mod-subclass: bot {} ({}) refused its borrowed build: {}", player->GetName(),
            DescribeSubclassChoice(SubclassChoiceOf(player)), refusal);
        return refusal.empty() ? std::string("refused") : refusal;
    }
    return std::string();
}

std::string ChoiceLabel(uint64 key)
{
    return Acore::StringFormat("{} + {}", SubclassClassName(MainOfKey(key)), DescribeSubclassChoice(ChoiceOfKey(key)));
}

std::string WorstFailure(CastTally const& tally)
{
    auto const worst = std::max_element(tally.Failures.begin(), tally.Failures.end(),
        [](auto const& left, auto const& right) { return left.second < right.second; });
    if (worst == tally.Failures.end())
        return "";
    SpellInfo const* info = sSpellMgr->GetSpellInfo(worst->first.first);
    return Acore::StringFormat(", worst: {} ({}) failed {}x with result {}", info ? info->SpellName[0] : "?",
        worst->first.first, worst->second, worst->first.second);
}

struct ChoiceSummary
{
    uint32 Bots = 0;
    uint32 Ready = 0;
    uint32 Opened = 0;
    uint32 Spells = 0;
    uint32 Refused = 0;
    std::string Refusal;
};

void ClearBorrowedBuild(Player* player)
{
    std::vector<AscensionCompatData::CoATalentEntry const*> const entries = OpenBorrowedEntries(player);
    std::vector<AscensionCoATalentState::KnownEntry> build = GetAscensionKnownTalentEntries(player);
    std::erase_if(build, [&entries](AscensionCoATalentState::KnownEntry const& known)
    {
        return std::any_of(entries.begin(), entries.end(), [&known](auto const* entry)
        {
            return entry->EntryId == known.EntryId;
        });
    });
    std::string refusal;
    ApplyAscensionTalentBuild(player, build, refusal);
}

class mod_subclass_bots_world : public WorldScript
{
public:
    mod_subclass_bots_world() : WorldScript("mod_subclass_bots_world", { WORLDHOOK_ON_STARTUP,
        WORLDHOOK_ON_AFTER_CONFIG_LOAD }) { }

    void OnAfterConfigLoad(bool /*reload*/) override
    {
        rebuildMs = sConfigMgr->GetOption<uint32>("Subclass.Bots.RebuildHours", 4) * HOUR * IN_MILLISECONDS;
    }

    void OnStartup() override
    {
        LoadForcedBots();
    }
};

class mod_subclass_bots_player : public PlayerScript
{
public:
    mod_subclass_bots_player() : PlayerScript("mod_subclass_bots_player", { PLAYERHOOK_ON_UPDATE }) { }

    void OnPlayerUpdate(Player* player, uint32 diff) override
    {
        uint32 const every = rebuildMs;
        if (!every || !ForcedBot(player))
            return;
        {
            std::lock_guard<std::mutex> lock(botMutex);
            auto [itr, fresh] = rebuildIn.try_emplace(player->GetGUID().GetCounter(), every);
            if (fresh || itr->second > diff)
            {
                if (!fresh)
                    itr->second -= diff;
                return;
            }
            itr->second = every;
            ++buildRolls[player->GetGUID().GetCounter()];
        }
        if (!SubclassChoiceOf(player).Complete() || player->IsInCombat())
            return;
        ClearBorrowedBuild(player);
        SpendBotPoints(player);
        LOG_INFO("module", "mod-subclass: bot {} tries a new borrowed build", player->GetName());
    }
};
}

void AssignBotSubclass(Player* player)
{
    if (!IsBot(player))
        return;
    uint32 const guid = player->GetGUID().GetCounter();
    bool wasForced = false;
    {
        std::lock_guard<std::mutex> lock(botMutex);
        wasForced = forcedBots.contains(guid);
    }
    if (!ForcedBot(player))
    {
        if (!wasForced)
            return;
        ApplySubclassChoice(player, {});
        CharacterDatabase.Execute("DELETE FROM `mod_subclass_bot` WHERE `guid` = {}", guid);
        std::lock_guard<std::mutex> lock(botMutex);
        forcedBots.erase(guid);
        return;
    }
    GiveTheStoryGender(player);
    if (!SubclassEligible(player))
        return;
    if (!SubclassChoiceOf(player).Complete())
    {
        std::vector<Subclass::Choice> const options = BotChoicesFor(player);
        if (options.empty())
            return;
        Subclass::Choice const choice = LeastUsedChoice(player->getClass(), options);
        ApplySubclassChoice(player, choice);
        CharacterDatabase.Execute("REPLACE INTO `mod_subclass_bot` (`guid`, `race`, `main_class`, `sub_class`, "
            "`sub_spec`, `sub_spec2`) VALUES ({}, {}, {}, {}, {}, {})", guid, uint32(player->getRace()),
            uint32(player->getClass()), uint32(choice.SubClass), choice.SubSpec, choice.SecondSpec);
        {
            std::lock_guard<std::mutex> lock(botMutex);
            forcedBots[guid] = { ChoiceKey(player->getClass(), choice), player->getRace() };
        }
        LOG_INFO("module", "mod-subclass: bot {} ({} {}) takes {}", player->GetName(),
            uint32(player->getRace()), SubclassClassName(player->getClass()), DescribeSubclassChoice(choice));
    }
    SpendBotPoints(player);
}

void SpendBotPoints(Player* player)
{
    if (!ForcedBot(player) || !SubclassChoiceOf(player).Complete())
        return;
    NoteBotState(player, SpendPoints(player));
}

void ForgetBotPlayer(Player const* player)
{
    std::lock_guard<std::mutex> lock(botMutex);
    onlineBots.erase(player->GetGUID().GetCounter());
    rebuildIn.erase(player->GetGUID().GetCounter());
}

void ForgetDeletedBot(uint32 guid)
{
    CharacterDatabase.DirectExecute("DELETE FROM `mod_subclass_bot` WHERE `guid` = {}", guid);
    std::lock_guard<std::mutex> lock(botMutex);
    forcedBots.erase(guid);
    buildRolls.erase(guid);
}

void NoteBotCast(Player const* player, uint32 spellId, uint32 result)
{
    if (!ForcedBot(player))
        return;
    uint64 const key = ChoiceKey(player->getClass(), SubclassChoiceOf(player));
    bool const ok = result == SPELL_CAST_OK;
    std::lock_guard<std::mutex> lock(botMutex);
    for (CastTally* tally : { &castsByChoice[key], &castsByRace[player->getRace()] })
    {
        ++(ok ? tally->Ok : tally->Failed);
        if (!ok)
            ++tally->Failures[{ spellId, result }];
    }
}

bool HandleSubclassBots(ChatHandler* handler, Optional<std::string> mode)
{
    bool const all = mode && *mode == "all";
    std::map<uint64, ChoiceSummary> choices;
    std::map<uint8, uint32> races;
    std::map<uint64, CastTally> casts;
    std::map<uint8, CastTally> raceCasts;
    std::size_t forced = 0;
    uint8 const minLevel = SubclassMinLevel();
    {
        std::lock_guard<std::mutex> lock(botMutex);
        forced = forcedBots.size();
        for (auto const& [guid, row] : forcedBots)
            ++races[row.Race];
        for (auto const& [guid, state] : onlineBots)
        {
            ChoiceSummary& summary = choices[state.Choice];
            ++summary.Bots;
            summary.Ready += state.Level >= minLevel;
            summary.Opened += state.Trees;
            summary.Spells += state.Spells;
            if (state.BuildRefused)
            {
                ++summary.Refused;
                summary.Refusal = state.Refusal;
            }
        }
        casts = castsByChoice;
        raceCasts = castsByRace;
    }
    handler->PSendSysMessage("Subclassed bots: {} in all, {} online in {} pairs.", forced,
        std::accumulate(choices.begin(), choices.end(), 0u, [](uint32 sum, auto const& item)
        {
            return sum + item.second.Bots;
        }), choices.size());
    std::string byRace;
    for (auto const& [race, bots] : races)
        byRace += Acore::StringFormat(" race {}: {}", uint32(race), bots);
    handler->PSendSysMessage("Subclassed bots by race:{}", byRace.empty() ? " none yet" : byRace);
    for (auto const& [race, tally] : raceCasts)
        handler->PSendSysMessage("Race {}: {} bot(s), other-half casts {} ok, {} failed{}", uint32(race),
            races[race], tally.Ok, tally.Failed, WorstFailure(tally));
    uint32 problems = 0;
    for (auto const& [key, summary] : choices)
    {
        CastTally const& tally = casts[key];
        bool const broken = summary.Ready && (summary.Opened < summary.Ready || !summary.Spells ||
            summary.Refused || tally.Failed > tally.Ok || (!tally.Ok && !tally.Failed));
        if (!all && !broken)
            continue;
        problems += broken;
        handler->PSendSysMessage("{}: {} online ({} at level {}+), trees open {}, borrowed spells {:.1f} avg, "
            "build refused {}{}; casts {} ok, {} failed{}", ChoiceLabel(key), summary.Bots, summary.Ready,
            uint32(minLevel), summary.Opened, summary.Bots ? float(summary.Spells) / summary.Bots : 0.0f,
            summary.Refused,
            summary.Refusal.empty() ? "" : " (" + summary.Refusal + ")", tally.Ok, tally.Failed, WorstFailure(tally));
    }
    if (!all)
        handler->PSendSysMessage("{} pair(s) need a look. .subclass bots all lists every pair.", problems);
    return true;
}

void AddSC_mod_subclass_bots()
{
    new mod_subclass_bots_world();
    new mod_subclass_bots_player();
}
