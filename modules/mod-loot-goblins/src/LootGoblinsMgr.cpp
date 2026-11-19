/*
 * mod-loot-goblins: configuration, loot pools, spawning, announcements and the per-player loot piles.
 */

#include "LootGoblins.h"

#include "Chat.h"
#include "Config.h"
#include "Containers.h"
#include "Creature.h"
#include "DatabaseEnv.h"
#include "QueryResult.h"
#include "DBCStores.h"
#include "GameTime.h"
#include "ItemTemplate.h"
#include "Log.h"
#include "LootMgr.h"
#include "Map.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "Random.h"
#include "StringFormat.h"
#include "TemporarySummon.h"
#include "World.h"
#include <algorithm>
#include <cmath>
#include <deque>
#include <mutex>
#include <sstream>
#include <unordered_set>

namespace LootGoblins
{
    namespace
    {
        // key, subname, default natural weight
        KindInfo const KINDS[KIND_COUNT] =
        {
            { "gold",          "Gold",                20 },
            { "uncommon",      "Uncommon loot",       25 },
            { "rare",          "Rare loot",           12 },
            { "epic",          "Epic loot",            3 },
            { "unknown",       "Unknown",              2 },
            { "worldforged",   "Worldforged loot",     2 },
            { "warspoils",     "War spoils",           0 },
            { "mining",        "Mining goods",         5 },
            { "herb",          "Herb goods",           5 },
            { "skinning",      "Skinning goods",       5 },
            { "cloth",         "Cloth goods",          5 },
            { "enchanting",    "Enchanting goods",     4 },
            { "gem",           "Gem goods",            4 },
            { "inscription",   "Inscription goods",    4 },
            { "cooking",       "Cooking goods",        4 },
            { "fishing",       "Fishing goods",        4 },
            { "engineering",   "Engineering goods",    4 },
            { "alchemy",       "Alchemy goods",        4 },
            { "blacksmithing", "Blacksmithing goods",  4 },
        };

        char const* const AREA_KEYS[size_t(Area::Count)] =
        { "ArenaRated", "ArenaSkirmish", "Battleground", "Raid", "Dungeon", "FFAArea", "SafeZone", "Cave", "OpenWorld" };
        float const AREA_DEFAULTS[size_t(Area::Count)] = { 0.0f, 5.0f, 3.0f, 1.0f, 2.0f, 2.0f, 0.0f, 0.5f, 0.5f };

        // Profession goods by tier: [0] up to level 19, [1] 20-39, [2] 40+. Ids missing in a database are skipped.
        std::vector<uint32> const GOODS[13][3] =
        {
            /* Mining */        { { 2770, 2771, 2835, 2840, 2841 }, { 2772, 3858, 2838, 3575, 3860, 7912 }, { 10620, 12365, 12359, 11370 } },
            /* Herb */          { { 2447, 765, 2449, 785, 2450, 2452, 3820 }, { 3355, 3356, 3357, 3818, 3821, 3358, 8831 }, { 8836, 8838, 8839, 8845, 8846, 13464, 13463, 13465, 13466, 13467 } },
            /* Skinning */      { { 2934, 2318, 783, 2319, 4232 }, { 4234, 4235, 4304, 8169 }, { 8170, 8171 } },
            /* Cloth */         { { 2589, 2592 }, { 4306, 4338 }, { 14047 } },
            /* Enchanting */    { { 10940, 10938, 10939, 10978 }, { 11083, 11134, 11137, 11174, 11084, 11138 }, { 16204, 16202, 16203, 14343, 14344 } },
            /* Gem */           { { 774, 818, 1210, 1206, 1705, 1529 }, { 7909, 3864, 7910, 1529 }, { 12361, 12799, 12800, 12364, 7971 } },
            /* Inscription */   { { 39151, 39469, 39334, 43103, 39774 }, { 39338, 43104, 43115, 43116, 39339 }, { 39340, 43117, 43118, 43105 } },
            /* Cooking */       { { 2672, 769, 2673, 2674, 6889, 2678 }, { 3173, 3712, 3731, 3667, 3685, 12037 }, { 12203, 12208, 12205, 12207, 12202 } },
            /* Fishing */       { { 6291, 6289, 6303, 6308 }, { 6361, 6358, 6362, 21071, 4603 }, { 13754, 13758, 13756, 13760, 13888 } },
            /* Engineering */   { { 4359, 4357, 4361, 4363, 4364, 4371 }, { 4375, 4377, 4382, 10558, 4387, 10559 }, { 10561, 15992, 16000, 10560 } },
            /* Alchemy */       { { 118, 2455, 858, 2454, 3371 }, { 929, 3827, 3372, 1710, 6149, 8925 }, { 13446, 13444, 3928, 8925, 13453 } },
            /* Blacksmithing */ { { 2840, 2841, 2862, 2863, 3478, 2880, 2842 }, { 3575, 3577, 3859, 3860, 7966, 3466, 7964 }, { 12359, 12644, 12404, 6037 } },
            /* (spare) */       { {}, {}, {} },
        };

        struct GearItem { uint32 entry; uint32 reqLevel; uint32 itemLevel; };

        Config gConfig;
        std::unordered_map<ObjectGuid, GoblinState> gActive;
        std::array<std::vector<GearItem>, 5> gGear;         // by quality 0..4
        std::vector<uint32> gUnknown;                         // boss loot
        std::vector<uint32> gWorldforged;
        std::vector<uint32> gModels;                          // goblin looks, picked at spawn
        std::unordered_map<uint32, uint32> gLastNaturalSpawn; // map -> game time (s)

        bool GoodName(std::string const& name)
        {
            static char const* const bad[] = { "Test", "TEST", "Deprecated", "DEP", "[PH]", "Monster -", "OLD", "(old)", "Unused", "zz" };
            for (char const* b : bad)
                if (name.find(b) != std::string::npos)
                    return false;
            return !name.empty();
        }

        uint32 Tier(uint8 level) { return level < 20 ? 0 : (level < 40 ? 1 : 2); }

        std::vector<std::pair<uint32, float>> ParseCurve(std::string const& text)
        {
            std::vector<std::pair<uint32, float>> out;
            std::stringstream ss(text);
            std::string part;
            while (std::getline(ss, part, ','))
            {
                auto colon = part.find(':');
                if (colon == std::string::npos)
                    continue;
                try { out.emplace_back(uint32(std::stoul(part.substr(0, colon))), std::stof(part.substr(colon + 1))); }
                catch (...) { }
            }
            std::sort(out.begin(), out.end());
            return out;
        }

        ItemTemplate const* Usable(uint32 entry)
        {
            ItemTemplate const* proto = sObjectMgr->GetItemTemplate(entry);
            return (proto && GoodName(proto->Name1)) ? proto : nullptr;
        }
    }

    KindInfo const& Info(Kind kind) { return KINDS[std::min<uint8>(uint8(kind), KIND_COUNT - 1)]; }

    bool ParseKind(std::string const& text, Kind& kind)
    {
        std::string t = text;
        std::transform(t.begin(), t.end(), t.begin(), ::tolower);
        for (uint8 i = 0; i < KIND_COUNT; ++i)
            if (t == KINDS[i].key)
            {
                kind = Kind(i);
                return true;
            }
        return false;
    }

    bool IsGoodsKind(Kind kind) { return uint8(kind) >= uint8(Kind::Mining); }

    char const* RankName(Rank rank)
    {
        static char const* const names[] = { "", "elite ", "rare ", "rare elite " };
        return names[std::min<uint8>(uint8(rank), 3)];
    }

    bool ParseRank(std::string const& text, Rank& rank)
    {
        std::string t = text;
        std::transform(t.begin(), t.end(), t.begin(), ::tolower);
        if (t == "normal") { rank = Rank::Normal; return true; }
        if (t == "elite") { rank = Rank::Elite; return true; }
        if (t == "rare") { rank = Rank::Rare; return true; }
        if (t == "rareelite" || t == "silver" || t == "silverelite") { rank = Rank::RareElite; return true; }
        return false;
    }

    Rank RankFor(Map const* map)
    {
        if (map && map->IsRaid())
            return gConfig.raidRank;
        if (map && (map->IsDungeon() || map->IsBattlegroundOrArena()))
            return map->IsDungeon() ? gConfig.dungeonRank : Rank::Normal;
        float roll = frand(0.0f, 100.0f);
        if (roll < gConfig.openWorldRareEliteChance)
            return Rank::RareElite;
        if (roll < gConfig.openWorldRareEliteChance + gConfig.openWorldRareChance)
            return Rank::Rare;
        return Rank::Normal;
    }

    Config const& Conf() { return gConfig; }

    float EpicFactor(uint8 level)
    {
        float f = 1.0f;
        for (auto const& [lvl, factor] : gConfig.epicByLevel)
            if (level >= lvl)
                f = factor;
        return f;
    }

    void LoadConfig()
    {
        Config c;
        c.enable = sConfigMgr->GetOption<bool>("LootGoblins.Enable", true);
        c.announce = sConfigMgr->GetOption<bool>("LootGoblins.Announce", true);
        c.announceEscape = sConfigMgr->GetOption<bool>("LootGoblins.AnnounceEscape", true);
        c.typeEntryBase = sConfigMgr->GetOption<uint32>("LootGoblins.Entry.TypeBase", 987101);
        c.pileEntry = sConfigMgr->GetOption<uint32>("LootGoblins.Entry.LootPile", 987200);
        c.bossEnable = sConfigMgr->GetOption<bool>("LootGoblins.Boss.Enable", true);
        c.bossEntryGreed = sConfigMgr->GetOption<uint32>("LootGoblins.Entry.Greed", 987205);
        c.bossEntryHard = sConfigMgr->GetOption<uint32>("LootGoblins.Entry.HardBoss", 987206);
        c.bossCrowdPercent = sConfigMgr->GetOption<float>("LootGoblins.Boss.CrowdPercent", 40.0f);
        c.bossCrowdMin = sConfigMgr->GetOption<uint32>("LootGoblins.Boss.CrowdMin", 8);
        c.bossMinLevel = sConfigMgr->GetOption<uint32>("LootGoblins.Boss.MinLevel", 10);
        c.bossZoneCooldown = sConfigMgr->GetOption<uint32>("LootGoblins.Boss.ZoneCooldown", 3600);
        c.greedTime = sConfigMgr->GetOption<uint32>("LootGoblins.Boss.GreedTime", 600);
        c.greedHardModeTime = sConfigMgr->GetOption<uint32>("LootGoblins.Boss.HardModeWithin", 300);
        c.hardTime = sConfigMgr->GetOption<uint32>("LootGoblins.Boss.HardTime", 900);
        c.greedHealthPerPlayer = sConfigMgr->GetOption<float>("LootGoblins.Boss.GreedHealthPerPlayer", 6.0f);
        c.hardHealthPerPlayer = sConfigMgr->GetOption<float>("LootGoblins.Boss.HardHealthPerPlayer", 10.0f);
        c.bossMeleePct = sConfigMgr->GetOption<float>("LootGoblins.Boss.MeleePercent", 7.0f);
        c.bossAbilityPct = sConfigMgr->GetOption<float>("LootGoblins.Boss.AbilityPercent", 28.0f);
        c.bossMaxHitPercent = sConfigMgr->GetOption<float>("LootGoblins.Boss.MaxHitPercent", 1.5f);
        c.bossLeash = sConfigMgr->GetOption<float>("LootGoblins.Boss.Leash", 70.0f);
        c.greedExtraItems = sConfigMgr->GetOption<uint32>("LootGoblins.Boss.GreedExtraItems", 2);
        c.hardExtraItems = sConfigMgr->GetOption<uint32>("LootGoblins.Boss.HardExtraItems", 4);
        c.bossLootMultiplier = sConfigMgr->GetOption<float>("LootGoblins.Boss.LootMultiplier", 1.5f);
        c.pileSlotFirst = sConfigMgr->GetOption<uint32>("LootGoblins.Entry.PileSlotFirst", 1044480);
        c.pileSlots = sConfigMgr->GetOption<uint32>("LootGoblins.PileSlots", 100);
        c.pileNameColors = sConfigMgr->GetOption<bool>("LootGoblins.LootNameColors", true);
        c.pileNames = sConfigMgr->GetOption<bool>("LootGoblins.LootNames", true);
        c.pileItemModels = sConfigMgr->GetOption<bool>("LootGoblins.LootItemModels", true);
        c.pileRangedModels = sConfigMgr->GetOption<bool>("LootGoblins.LootRangedModels", true);
        c.pilePoleModels = sConfigMgr->GetOption<bool>("LootGoblins.LootPoleModels", false);
        c.forcedNameplates = sConfigMgr->GetOption<bool>("LootGoblins.ForcedNameplates", true);
        c.corpseSeconds = sConfigMgr->GetOption<uint32>("LootGoblins.CorpseSeconds", 12);
        c.portalEntry = sConfigMgr->GetOption<uint32>("LootGoblins.Entry.Portal", 987201);
        c.deathEntry = sConfigMgr->GetOption<uint32>("LootGoblins.Entry.DeathBurst", 987202);
        c.coinEntry = sConfigMgr->GetOption<uint32>("LootGoblins.Entry.CoinBurst", 987203);
        c.announceChase = sConfigMgr->GetOption<bool>("LootGoblins.AnnounceChase", true);
        c.mapPins = sConfigMgr->GetOption<bool>("LootGoblins.MapPins", true);
        c.minFightSeconds = sConfigMgr->GetOption<uint32>("LootGoblins.MinFightSeconds", 60);
        c.unawareTime = sConfigMgr->GetOption<uint32>("LootGoblins.UnawareTime", 600);
        c.startTime = sConfigMgr->GetOption<uint32>("LootGoblins.StartTime", 180);
        c.maxLifetime = sConfigMgr->GetOption<uint32>("LootGoblins.MaxLifetime", 300);
        c.timePerHit = sConfigMgr->GetOption<float>("LootGoblins.TimePerHit", 0.5f);
        c.extendCooldown = sConfigMgr->GetOption<float>("LootGoblins.TimeExtendPerPlayerCooldown", 1.0f);
        c.headForEscapeAt = sConfigMgr->GetOption<uint32>("LootGoblins.HeadForEscapeAt", 30);
        c.portalWindow = sConfigMgr->GetOption<uint32>("LootGoblins.PortalWindow", 15);
        c.portalCastTime = sConfigMgr->GetOption<uint32>("LootGoblins.PortalCastTime", 2500);
        c.portalRetryDelay = sConfigMgr->GetOption<uint32>("LootGoblins.PortalRetryDelay", 3);
        c.hostileFaction = sConfigMgr->GetOption<uint32>("LootGoblins.Faction.Awake", 14);
        c.neutralFaction = sConfigMgr->GetOption<uint32>("LootGoblins.Faction.Unaware", 7);
        c.runSpeedRate = sConfigMgr->GetOption<float>("LootGoblins.RunSpeedRate", 1.15f);
        c.fleeDistance = sConfigMgr->GetOption<float>("LootGoblins.FleeDistance", 18.0f);
        c.mirrorTimer = sConfigMgr->GetOption<bool>("LootGoblins.TimerBar.MirrorFallback", true);
        c.timerRange = sConfigMgr->GetOption<float>("LootGoblins.TimerBar.Range", 80.0f);
        c.followLowestLevel = sConfigMgr->GetOption<bool>("LootGoblins.Level.FollowLowestParticipant", true);
        c.normaliseDamage = sConfigMgr->GetOption<bool>("LootGoblins.Level.NormaliseDamage", true);
        c.higherLevelEdge = sConfigMgr->GetOption<float>("LootGoblins.Level.HigherLevelEdge", 1.25f);
        c.healthMultiplier = sConfigMgr->GetOption<float>("LootGoblins.GoblinHealthMultiplier", 8.0f);
        c.healthPerAttacker = sConfigMgr->GetOption<float>("LootGoblins.HealthPerAttacker", 50.0f);
        c.maxHitPercent = sConfigMgr->GetOption<float>("LootGoblins.MaxHitPercent", 5.0f);
        c.playersOnly = sConfigMgr->GetOption<bool>("LootGoblins.OnlyPlayersHurtIt", true);
        c.ccDurationFactor = sConfigMgr->GetOption<float>("LootGoblins.CC.Reduction", 50.0f);
        c.ccDurationFactor = std::clamp(1.0f - c.ccDurationFactor / 100.0f, 0.0f, 1.0f);
        c.itemsMin = sConfigMgr->GetOption<uint32>("LootGoblins.Loot.ItemsMin", 4);
        c.itemsMax = std::max(c.itemsMin, sConfigMgr->GetOption<uint32>("LootGoblins.Loot.ItemsMax", 8));
        c.qualityGreen = sConfigMgr->GetOption<uint32>("LootGoblins.Loot.QualityWeight.Green", 80);
        c.qualityBlue = sConfigMgr->GetOption<uint32>("LootGoblins.Loot.QualityWeight.Blue", 18);
        c.qualityEpic = sConfigMgr->GetOption<uint32>("LootGoblins.Loot.QualityWeight.Epic", 2);
        c.epicByLevel = ParseCurve(sConfigMgr->GetOption<std::string>("LootGoblins.Loot.EpicChanceByLevel", "1:0.1,20:0.4,40:0.75,60:1.0"));
        c.upgradeGreenToBlue = sConfigMgr->GetOption<uint32>("LootGoblins.Upgrade.GreenToBlue", 15);
        c.upgradeBlueToEpic = sConfigMgr->GetOption<uint32>("LootGoblins.Upgrade.BlueToEpic", 10);
        c.minContribution = sConfigMgr->GetOption<float>("LootGoblins.MinContribution", 1.0f);
        c.contributionCountsHealing = sConfigMgr->GetOption<bool>("LootGoblins.ContributionCountsHealing", true);
        c.contributionBonus = sConfigMgr->GetOption<float>("LootGoblins.ContributionBonus", 0.5f);
        c.healWeight = sConfigMgr->GetOption<float>("LootGoblins.Score.HealingWeight", 0.75f);
        c.tankWeight = sConfigMgr->GetOption<float>("LootGoblins.Score.TankingWeight", 0.75f);
        c.addWeight = sConfigMgr->GetOption<float>("LootGoblins.Score.AddDamageWeight", 0.5f);
        c.presenceRange = sConfigMgr->GetOption<float>("LootGoblins.Score.PresenceRange", 40.0f);
        c.presenceFloor = sConfigMgr->GetOption<float>("LootGoblins.Score.PresenceForFairShare", 50.0f);
        c.creditRange = sConfigMgr->GetOption<float>("LootGoblins.Score.AddFightRange", 60.0f);
        c.pullEnable = sConfigMgr->GetOption<bool>("LootGoblins.Pull.Enable", true);
        c.pullHealthHigh = sConfigMgr->GetOption<float>("LootGoblins.Pull.HealthHigh", 70.0f);
        c.pullHealthLow = sConfigMgr->GetOption<float>("LootGoblins.Pull.HealthLow", 45.0f);
        c.pullMaxEngagedBase = sConfigMgr->GetOption<uint32>("LootGoblins.Pull.MaxEngaged", 3);
        c.allDeadEscape = sConfigMgr->GetOption<uint32>("LootGoblins.AllDeadEscapeSeconds", 20);
        c.contributionMin = sConfigMgr->GetOption<float>("LootGoblins.ContributionMin", 0.5f);
        c.contributionMax = sConfigMgr->GetOption<float>("LootGoblins.ContributionMax", 2.0f);
        c.lootLifetime = sConfigMgr->GetOption<uint32>("LootGoblins.LootLifetime", 300);
        c.unknownItems = sConfigMgr->GetOption<uint32>("LootGoblins.Unknown.ItemCount", 3);
        c.worldforgedItems = sConfigMgr->GetOption<uint32>("LootGoblins.Worldforged.ItemCount", 1);
        c.goldPerLevel = sConfigMgr->GetOption<uint32>("LootGoblins.Gold.CopperPerLevel", 30);
        c.goldGoblinMultiplier = sConfigMgr->GetOption<float>("LootGoblins.Gold.GoldGoblinMultiplier", 20.0f);
        for (size_t i = 0; i < size_t(Area::Count); ++i)
            c.spawnChance[i] = sConfigMgr->GetOption<float>(std::string("LootGoblins.SpawnChance.") + AREA_KEYS[i], AREA_DEFAULTS[i]);
        c.spawnCooldown = sConfigMgr->GetOption<uint32>("LootGoblins.SpawnCooldown", 120);
        c.maxActive = sConfigMgr->GetOption<uint32>("LootGoblins.MaxActive", 10);
        c.packSmallChance = sConfigMgr->GetOption<float>("LootGoblins.Pack.SmallChance", 3.0f);
        c.packLargeChance = sConfigMgr->GetOption<float>("LootGoblins.Pack.LargeChance", 0.5f);
        c.packSameType = sConfigMgr->GetOption<bool>("LootGoblins.Pack.SameType", false);
        for (uint8 i = 0; i < KIND_COUNT; ++i)
        {
            std::string key = KINDS[i].key;
            key[0] = char(::toupper(key[0]));
            c.weights[i] = sConfigMgr->GetOption<uint32>("LootGoblins.TypeWeight." + key, KINDS[i].defaultWeight);
        }
        c.commandMaxSpawn = sConfigMgr->GetOption<uint32>("LootGoblins.Command.MaxSpawn", 25);
        c.dungeonRank = Rank(std::min<uint32>(sConfigMgr->GetOption<uint32>("LootGoblins.Rank.Dungeon", 1), 3));
        c.raidRank = Rank(std::min<uint32>(sConfigMgr->GetOption<uint32>("LootGoblins.Rank.Raid", 1), 3));
        c.openWorldRareChance = sConfigMgr->GetOption<float>("LootGoblins.Rank.OpenWorldRareChance", 15.0f);
        c.openWorldRareEliteChance = sConfigMgr->GetOption<float>("LootGoblins.Rank.OpenWorldRareEliteChance", 5.0f);
        char const* const rk[] = { "Normal", "Elite", "Rare", "RareElite" };
        float const hp[] = { 1.0f, 1.5f, 1.25f, 2.0f }, lt[] = { 1.0f, 1.15f, 1.3f, 1.5f };
        uint32 const ex[] = { 0, 1, 2, 3 };
        for (int i = 0; i < 4; ++i)
        {
            c.rankHealth[i] = sConfigMgr->GetOption<float>(std::string("LootGoblins.Rank.") + rk[i] + ".Health", hp[i]);
            c.rankLoot[i] = sConfigMgr->GetOption<float>(std::string("LootGoblins.Rank.") + rk[i] + ".LootBonus", lt[i]);
            c.rankExtraItems[i] = sConfigMgr->GetOption<uint32>(std::string("LootGoblins.Rank.") + rk[i] + ".ExtraItems", ex[i]);
        }
        c.commandAnnounce = sConfigMgr->GetOption<bool>("LootGoblins.Command.Announce", true);
        c.scaledEnable = sConfigMgr->GetOption<bool>("LootGoblins.Scaled.Enable", true);
        c.scaledChance = sConfigMgr->GetOption<float>("LootGoblins.Scaled.Chance", 30.0f);
        c.scaledPowerBonus = sConfigMgr->GetOption<float>("LootGoblins.Scaled.PowerBonus", 1.10f);
        c.scaledBracket = sConfigMgr->GetOption<uint32>("LootGoblins.Scaled.BracketSize", 5);
        c.scaledSpellMode = sConfigMgr->GetOption<uint32>("LootGoblins.Scaled.SpellMode", 1);
        c.scaledSuffix = sConfigMgr->GetOption<std::string>("LootGoblins.Scaled.NameSuffix", "");
        c.scaledFirstEntry = sConfigMgr->GetOption<uint32>("LootGoblins.Scaled.FirstEntry", 9000001);
        c.scaledRuntimeCopies = sConfigMgr->GetOption<bool>("LootGoblins.Scaled.RuntimeCopies", false);
        c.pinata = sConfigMgr->GetOption<bool>("LootGoblins.Loot.Pinata", true);
        c.beams = sConfigMgr->GetOption<bool>("LootGoblins.LootBeams.Enable", true);
        c.beamEntry = sConfigMgr->GetOption<uint32>("LootGoblins.Entry.LootBeam", 987204);
        c.beamMinQuality = sConfigMgr->GetOption<uint32>("LootGoblins.LootBeams.MinQuality", 0);
        c.personalVisibility = sConfigMgr->GetOption<bool>("LootGoblins.Loot.OnlyOwnerSeesPiles", true);
        c.groupRollMode = sConfigMgr->GetOption<uint32>("LootGoblins.GroupRoll.Mode", 1);
        c.rollTimeout = sConfigMgr->GetOption<uint32>("LootGoblins.GroupRoll.Timeout", 40);
        c.rollBonusDivisor = std::max(1.0f, sConfigMgr->GetOption<float>("LootGoblins.GroupRoll.BonusDivisor", 10.0f));
        c.rollHealerFloor = sConfigMgr->GetOption<bool>("LootGoblins.GroupRoll.HealerFairShare", true);
        c.addonTimer = sConfigMgr->GetOption<bool>("LootGoblins.TimerBar.Addon", true);
        gConfig = c;
    }

    // ---- pools --------------------------------------------------------------------------------------
    void BuildPools()
    {
        for (auto& v : gGear)
            v.clear();
        gUnknown.clear();
        gWorldforged.clear();

        // CoA keeps many copies of one item (other difficulties, Mythic+ levels): one entry per name and quality, so
        // an item with twenty copies isn't twenty times as likely
        std::unordered_set<std::string> seen[ITEM_QUALITY_EPIC + 1];
        for (auto const& [entry, proto] : *sObjectMgr->GetItemTemplateStore())
        {
            if (proto.Quality < ITEM_QUALITY_UNCOMMON || proto.Quality > ITEM_QUALITY_EPIC)
                continue;
            if (proto.Class != ITEM_CLASS_WEAPON && proto.Class != ITEM_CLASS_ARMOR)
                continue;
            if (proto.Bonding == BIND_QUEST_ITEM || proto.InventoryType == INVTYPE_NON_EQUIP || proto.ItemLevel == 0)
                continue;
            if (!GoodName(proto.Name1) || !seen[proto.Quality].insert(proto.Name1).second)
                continue;
            gGear[proto.Quality].push_back({ entry, proto.RequiredLevel, proto.ItemLevel });
        }
        // CoA marks Worldforged items with a description starting "@Worldforged@".
        for (auto const& [entry, proto] : *sObjectMgr->GetItemTemplateStore())
            if (proto.Description.rfind("@Worldforged@", 0) == 0 && proto.Quality >= ITEM_QUALITY_UNCOMMON)
                gWorldforged.push_back(entry);

        // <Unknown>: everything bosses drop (rank 3), straight and through one reference level.
        if (QueryResult result = WorldDatabase.Query(
                "SELECT DISTINCT l.Item FROM creature_loot_template l JOIN creature_template c ON c.lootid = l.Entry "
                "WHERE c.`rank` = 3 AND l.Reference = 0 "
                "UNION SELECT DISTINCT r.Item FROM creature_loot_template l JOIN creature_template c ON c.lootid = l.Entry "
                "JOIN reference_loot_template r ON r.Entry = l.Reference WHERE c.`rank` = 3 AND l.Reference > 0 AND r.Reference = 0"))
        {
            do
            {
                uint32 entry = (*result)[0].Get<uint32>();
                if (ItemTemplate const* proto = Usable(entry))
                    if (proto->Quality >= ITEM_QUALITY_UNCOMMON && proto->Bonding != BIND_QUEST_ITEM && !proto->StartQuest)
                        gUnknown.push_back(entry);
            } while (result->NextRow());
        }

        // <Worldforged loot>: what CoA's Worldforged pickups hold (mod-worldforged-pickups marks them by ScriptName).
        if (QueryResult result = WorldDatabase.Query(
                "SELECT DISTINCT g.Item FROM gameobject_loot_template g JOIN gameobject_template t ON t.Data1 = g.Entry "
                "WHERE t.ScriptName = 'worldforged_pickup' AND t.type = 3 AND g.Reference = 0"))
        {
            do
            {
                uint32 entry = (*result)[0].Get<uint32>();
                if (Usable(entry) && std::find(gWorldforged.begin(), gWorldforged.end(), entry) == gWorldforged.end())
                    gWorldforged.push_back(entry);
            } while (result->NextRow());
        }

        gModels.clear();
        if (QueryResult result = WorldDatabase.Query("SELECT DisplayID, Weight FROM lootgoblin_models"))
        {
            do
            {
                uint32 display = (*result)[0].Get<uint32>();
                uint32 weight = std::min<uint32>((*result)[1].Get<uint32>(), 20);
                if (sCreatureDisplayInfoStore.LookupEntry(display))
                    for (uint32 i = 0; i < weight; ++i)
                        gModels.push_back(display);
            } while (result->NextRow());
        }

        ResetLootVisuals();
        LOG_INFO("module", "Loot Goblins: {}", PoolSummary());
    }

    std::string PoolSummary()
    {
        return Acore::StringFormat("pools: green {} / blue {} / epic {}, unknown (boss loot) {}, worldforged {}, goblin looks {}",
            gGear[2].size(), gGear[3].size(), gGear[4].size(), gUnknown.size(), gWorldforged.size(), gModels.size());
    }

    uint32 PickModel()
    {
        return gModels.empty() ? 0 : Acore::Containers::SelectRandomContainerElement(gModels);
    }

    // ---- registry ---------------------------------------------------------------------------------
    std::unordered_map<ObjectGuid, GoblinState>& Active() { return gActive; }

    GoblinState* Find(ObjectGuid const& guid)
    {
        auto itr = gActive.find(guid);
        return itr == gActive.end() ? nullptr : &itr->second;
    }

    bool IsGoblin(Unit const* unit)
    {
        return unit && unit->GetTypeId() == TYPEID_UNIT && gActive.count(unit->GetGUID());
    }

    bool IsBoss(Unit const* unit)
    {
        if (!unit || unit->GetTypeId() != TYPEID_UNIT)
            return false;
        auto itr = gActive.find(unit->GetGUID());
        return itr != gActive.end() && itr->second.boss;
    }

    void Announce(std::string const& text)
    {
        if (!text.empty())
            ChatHandler(nullptr).SendWorldText("|cffffd000[Loot Goblin]|r {}", text);
    }

    void AnnounceZone(Unit const* where, std::string const& text)
    {
        if (!where || !where->GetMap())
            return;
        uint32 zone = where->GetZoneId();
        std::string line = "|cffffd000[Loot Goblin]|r " + text;
        where->GetMap()->DoForAllPlayers([&](Player* player)
        {
            if (player->GetZoneId() == zone && player->GetSession())
                ChatHandler(player->GetSession()).SendSysMessage(line);
        });
    }

    std::string Clock(uint32 ms)
    {
        uint32 s = (ms + 500) / 1000;
        return Acore::StringFormat("{}:{:02}", s / 60, s % 60);
    }

    std::string ZoneName(Unit const* unit)
    {
        if (!unit)
            return "somewhere";
        if (AreaTableEntry const* zone = sAreaTableStore.LookupEntry(unit->GetZoneId()))
            return zone->area_name[0];
        return "somewhere";
    }

    // A type whose loot doesn't exist on this realm (no Worldforged items outside CoA, say) never spawns.
    uint32 EffectiveWeight(uint8 i)
    {
        if (Kind(i) == Kind::Worldforged && gWorldforged.empty())
            return 0;
        if (Kind(i) == Kind::Unknown && gUnknown.empty())
            return 0;
        return gConfig.weights[i];
    }

    bool RandomKind(Kind& kind)
    {
        uint32 total = 0;
        for (uint8 i = 0; i < KIND_COUNT; ++i)
            total += EffectiveWeight(i);
        if (!total)
            return false;
        uint32 roll = urand(1, total);
        for (uint8 i = 0; i < KIND_COUNT; ++i)
        {
            uint32 w = EffectiveWeight(i);
            if (roll <= w)
            {
                kind = Kind(i);
                return true;
            }
            roll -= w;
        }
        return false;
    }

    Creature* SpawnGoblin(Map* map, Position const& pos, Kind kind, Rank rank, uint32 phaseMask, uint8 level, bool announce, std::string const& where)
    {
        if (!map || gActive.size() >= gConfig.maxActive)
            return nullptr;
        TempSummon* goblin = map->SummonCreature(gConfig.typeEntryBase + uint32(rank) * RANK_STRIDE + uint8(kind), pos);
        if (!goblin)
            return nullptr;
        goblin->SetPhaseMask(phaseMask, true);
        if (level)
            goblin->SetLevel(level);   // starts at the finder's level; drops to the lowest participant once hit
        // The AI registers it (and owns its state) in InitializeAI.
        if (announce && gConfig.announce)
            Announce(Acore::StringFormat("A {}Loot Goblin <{}> has been spotted in {}! It's on your map: hit it before it wanders off ({}). "
                "Once hit it runs, and you have {} to bring it down - every hit buys more time.", RankName(rank), Info(kind).subname, where,
                Clock(gConfig.unawareTime * IN_MILLISECONDS), Clock(gConfig.startTime * IN_MILLISECONDS)));
        return goblin;
    }

    uint32 SpawnPack(Unit* around, uint32 count, Kind const* forced, bool announce, bool packMessage, Rank const* forcedRank)
    {
        if (!around)
            return 0;
        Kind shared = Kind::Gold;
        bool haveShared = forced ? (shared = *forced, true) : (gConfig.packSameType && RandomKind(shared));
        std::string where = ZoneName(around);
        if (announce && packMessage && gConfig.announce)
            Announce(Acore::StringFormat(count >= 5 ? "A Loot Goblin horde ({}) has been spotted in {}! They're on your map - come help!"
                : "A pack of {} Loot Goblins has been spotted in {}! They're on your map - come help!", count, where));
        uint32 made = 0;
        for (uint32 i = 0; i < count; ++i)
        {
            Kind kind = shared;
            if (!haveShared && !RandomKind(kind))
                kind = Kind::Gold;
            float angle = float(rand_norm()) * 2.0f * float(M_PI);
            float dist = count == 1 ? 6.0f : frand(4.0f, 10.0f);
            Position pos = around->GetFirstCollisionPosition(dist, angle);
            pos.SetOrientation(frand(0.0f, 2.0f * float(M_PI)));
            Rank rank = forcedRank ? *forcedRank : RankFor(around->GetMap());
            if (SpawnGoblin(around->GetMap(), pos, kind, rank, around->GetPhaseMask(), uint8(around->GetLevel()), announce && !packMessage, where))
                ++made;
        }
        return made;
    }

    float LevelDamageRatio(uint8 goblinLevel, uint8 attackerLevel)
    {
        if (!gConfig.normaliseDamage || attackerLevel <= goblinLevel)
            return 1.0f;
        // Base health per level tracks how hard everyone hits at that level, so it scales damage down well.
        CreatureBaseStats const* low = sObjectMgr->GetCreatureBaseStats(goblinLevel, CLASS_WARRIOR);
        CreatureBaseStats const* high = sObjectMgr->GetCreatureBaseStats(attackerLevel, CLASS_WARRIOR);
        if (!low || !high || !high->BaseHealth[0])
            return 1.0f;
        float ratio = float(low->BaseHealth[0]) / float(high->BaseHealth[0]);
        return std::min(1.0f, ratio * gConfig.higherLevelEdge);
    }

    // ---- picking items -------------------------------------------------------------------------------
    // The level an item is really for. Many items say "no level required" yet carry a high item level
    // (an epic at item level 60 with no requirement), so for those the item level decides.
    uint32 ItemForLevel(uint32 reqLevel, uint32 itemLevel)
    {
        if (reqLevel)
            return reqLevel;
        return itemLevel > 5 ? std::min<uint32>(80, itemLevel - 5) : 1;
    }

    namespace
    {
        uint32 FitFrom(std::vector<GearItem> const& pool, uint8 level)
        {
            for (uint32 spread : { 5u, 12u, 30u })
            {
                std::vector<uint32> fit;
                for (GearItem const& g : pool)
                    if (uint32 l = ItemForLevel(g.reqLevel, g.itemLevel); l <= level && l + spread >= level)
                        fit.push_back(g.entry);
                if (!fit.empty())
                    return Acore::Containers::SelectRandomContainerElement(fit);
            }
            return 0;
        }

        // A higher-level item of this quality, shrunk to `level`.
        uint32 ShrunkFrom(std::vector<GearItem> const& pool, uint8 level)
        {
            if (!ScalingAvailable() || pool.empty())
                return 0;
            std::vector<uint32> higher;
            for (GearItem const& g : pool)
                if (ItemForLevel(g.reqLevel, g.itemLevel) > uint32(level) + 5)
                    higher.push_back(g.entry);
            std::vector<uint32> copies;
            for (uint32 entry : higher)
                if (uint32 copy = ScaledCopy(entry, level))
                    copies.push_back(copy);
            return copies.empty() ? 0 : Acore::Containers::SelectRandomContainerElement(copies);
        }

        uint32 PickFrom(std::vector<uint32> const& pool, uint8 level)
        {
            std::vector<uint32> fit, higher;
            for (uint32 entry : pool)
                if (ItemTemplate const* proto = sObjectMgr->GetItemTemplate(entry))
                    (ItemForLevel(proto->RequiredLevel, proto->ItemLevel) <= level ? fit : higher).push_back(entry);
            // Above the player's level: shrunk to it when we can, otherwise only what fits.
            if (!higher.empty() && ScalingAvailable() && (fit.empty() || roll_chance_i(50)))
            {
                std::vector<uint32> copies;
                for (uint32 entry : higher)
                    if (uint32 copy = ScaledCopy(entry, level))
                        copies.push_back(copy);
                if (!copies.empty())
                    return Acore::Containers::SelectRandomContainerElement(copies);
            }
            return fit.empty() ? 0 : Acore::Containers::SelectRandomContainerElement(fit);
        }
    }

    namespace
    {
        // the last gear picks (by name): a new pick that repeats one is rolled again, so the same few items at a
        // level don't keep coming back
        std::mutex gRecentMutex;
        std::deque<std::string> gRecent;

        bool Recent(uint32 entry)
        {
            ItemTemplate const* p = sObjectMgr->GetItemTemplate(entry);
            if (!p)
                return false;
            std::lock_guard lock(gRecentMutex);
            return std::find(gRecent.begin(), gRecent.end(), p->Name1) != gRecent.end();
        }

        void Remember(uint32 entry)
        {
            ItemTemplate const* p = sObjectMgr->GetItemTemplate(entry);
            if (!p)
                return;
            std::lock_guard lock(gRecentMutex);
            gRecent.push_back(p->Name1);
            while (gRecent.size() > 40)
                gRecent.pop_front();
        }

        uint32 PickGearOnce(uint8 quality, uint8 level);
    }

    uint32 PickGear(uint8 quality, uint8 level)
    {
        uint32 pick = 0;
        for (int tries = 0; tries < 5; ++tries)
        {
            pick = PickGearOnce(quality, level);
            if (!pick || !Recent(pick))
                break;
        }
        if (pick)
            Remember(pick);
        return pick;
    }

    std::string PoolReport(uint8 level)
    {
        std::string out;
        static char const* const names[] = { "", "", "green", "blue", "epic" };
        for (uint8 q = ITEM_QUALITY_UNCOMMON; q <= ITEM_QUALITY_EPIC; ++q)
        {
            uint32 fit5 = 0, fit12 = 0, fit30 = 0, higher = 0, copies = 0;
            for (GearItem const& g : gGear[q])
            {
                uint32 l = ItemForLevel(g.reqLevel, g.itemLevel);
                if (l <= level && l + 5 >= level) ++fit5;
                if (l <= level && l + 12 >= level) ++fit12;
                if (l <= level && l + 30 >= level) ++fit30;
                if (l > uint32(level) + 5)
                {
                    ++higher;
                    if (ScaledCopy(g.entry, level))
                        ++copies;
                }
            }
            out += Acore::StringFormat("{}{}: {} in all, at level {}: {} within 5 levels, {} within 12, {} within 30; {} higher ({} can be shrunk)",
                out.empty() ? "" : " | ", names[q], gGear[q].size(), level, fit5, fit12, fit30, higher, copies);
        }
        return out;
    }

    namespace
    {
    uint32 PickGearOnce(uint8 quality, uint8 level)
    {
        quality = std::clamp<uint8>(quality, ITEM_QUALITY_UNCOMMON, ITEM_QUALITY_EPIC);
        if (roll_chance_f(gConfig.scaledChance))
            if (uint32 copy = ShrunkFrom(gGear[quality], level))
                return copy;
        if (uint32 fit = FitFrom(gGear[quality], level))
            return fit;
        if (uint32 copy = ShrunkFrom(gGear[quality], level))    // nothing of this quality at this level (low epics)
            return copy;
        for (int q = quality - 1; q >= ITEM_QUALITY_UNCOMMON; --q)
            if (uint32 fit = FitFrom(gGear[q], level))
                return fit;
        return 0;
    }
    }

    uint32 PickGoods(uint8 profession, uint8 level)
    {
        std::vector<uint32> fit;
        for (uint32 entry : GOODS[std::min<uint8>(profession, 11)][Tier(level)])
            if (Usable(entry))
                fit.push_back(entry);
        return fit.empty() ? 0 : Acore::Containers::SelectRandomContainerElement(fit);
    }

    uint32 PickUnknown(uint8 level) { return PickFrom(gUnknown, level); }
    uint32 PickWorldforged(uint8 level) { return PickFrom(gWorldforged, level); }
}
