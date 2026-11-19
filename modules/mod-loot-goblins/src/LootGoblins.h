/*
 * mod-loot-goblins - Diablo-style Loot Goblins for AzerothCore / CoA.
 * Plain module: no core changes. Released under GNU AGPL v3 (same as AzerothCore).
 */

#ifndef MOD_LOOT_GOBLINS_H
#define MOD_LOOT_GOBLINS_H

#include "Define.h"
#include "ObjectGuid.h"
#include "Position.h"
#include <array>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

class Creature;
class Player;
class Unit;
class Map;
class WorldObject;
class WorldSession;
class WorldPacket;

namespace LootGoblins
{
    // The goblin kinds. Entry = Config.TypeEntryBase + index, so this order is part of the data contract.
    enum class Kind : uint8
    {
        Gold = 0, Uncommon, Rare, Epic, Unknown, Worldforged, WarSpoils,
        Mining, Herb, Skinning, Cloth, Enchanting, Gem, Inscription, Cooking, Fishing, Engineering, Alchemy, Blacksmithing,
        Count
    };
    constexpr uint8 KIND_COUNT = uint8(Kind::Count);

    struct KindInfo
    {
        char const* key;      // command / conf name
        char const* subname;  // shown under the name
        uint32 defaultWeight; // natural spawn weight
    };
    KindInfo const& Info(Kind kind);
    bool ParseKind(std::string const& text, Kind& kind);
    bool IsGoodsKind(Kind kind);

    // Rank of a goblin: its own creature entry per type and rank (entry = TypeBase + rank * 20 + type), so the client
    // shows the elite / rare / rare-elite dragon frame.
    enum class Rank : uint8 { Normal = 0, Elite, Rare, RareElite, Count };
    constexpr uint32 RANK_STRIDE = 20;
    char const* RankName(Rank rank);
    bool ParseRank(std::string const& text, Rank& rank);

    // Where a natural spawn happens (first match wins), each with its own chance.
    enum class Area : uint8 { ArenaRated, ArenaSkirmish, Battleground, Raid, Dungeon, FFA, SafeZone, Cave, OpenWorld, Count };

    struct Config
    {
        bool enable = true;
        bool announce = true;
        bool announceEscape = true;
        bool announceChase = true;      // zone-wide call for help when the chase starts
        bool mapPins = true;            // addon: goblins on the world and zone map
        uint32 minFightSeconds = 60;    // health can't drop faster than full -> 0 over this long
        uint32 typeEntryBase = 987101;
        uint32 pileEntry = 987200;
        uint32 pileSlotFirst = 1044480, pileSlots = 100;   // one creature entry per dropped item: its name is the item's
        bool pileNameColors = true;     // try quality colour codes in those names
        bool pileNames = true;          // show the real item name / money amount over each drop
        bool pileItemModels = true;     // weapons and shields lie on the ground as their real models
        bool pilePoleModels = false;    // fishing poles and misc weapons as models too (test: they were invisible)
        bool pileRangedModels = true;   // bows, guns, crossbows, wands and thrown too (held in the main-hand slot)
        bool forcedNameplates = true;   // goblins and drops always get a nameplate (needs the patched client)
        uint32 corpseSeconds = 12;
        // ---- world boss chain: goblin -> Greed -> (kill her fast) hard mode ----
        bool bossEnable = true;
        uint32 bossEntryGreed = 987205, bossEntryHard = 987206;
        float bossCrowdPercent = 40.0f;  // of the players in the zone must be fighting the goblin
        uint32 bossCrowdMin = 8;
        uint32 bossMinLevel = 10;
        uint32 bossZoneCooldown = 3600;  // s
        uint32 greedTime = 600, greedHardModeTime = 300, hardTime = 900;   // s
        float greedHealthPerPlayer = 6.0f, hardHealthPerPlayer = 10.0f;     // x base health of her level, per fighter
        float bossMeleePct = 7.0f;       // of the target's max health per swing
        float bossAbilityPct = 28.0f;    // of max health for the big hits (shockwave, charge)
        float bossMaxHitPercent = 1.5f;  // most of her health one hit can take
        float bossLeash = 70.0f;
        uint32 greedExtraItems = 2, hardExtraItems = 4;
        float bossLootMultiplier = 1.5f;
        uint32 portalEntry = 987201;
        uint32 deathEntry = 987202;
        uint32 coinEntry = 987203;

        uint32 unawareTime = 600;   // s
        uint32 startTime = 180;     // s, at the first hit
        uint32 maxLifetime = 300;   // s, hard limit from the first hit
        float timePerHit = 0.5f;
        float extendCooldown = 1.0f;
        uint32 headForEscapeAt = 30;
        uint32 portalWindow = 15;
        uint32 portalCastTime = 2500;  // ms
        uint32 portalRetryDelay = 3;   // s
        uint32 hostileFaction = 14;
        uint32 neutralFaction = 7;
        float runSpeedRate = 1.15f;
        float fleeDistance = 18.0f;
        bool mirrorTimer = true;
        float timerRange = 80.0f;

        bool followLowestLevel = true;
        bool normaliseDamage = true;
        float higherLevelEdge = 1.25f;
        float healthMultiplier = 8.0f;
        float healthPerAttacker = 50.0f; // % per extra participant
        float maxHitPercent = 5.0f;
        bool playersOnly = true;
        float ccDurationFactor = 0.5f;

        uint32 itemsMin = 4, itemsMax = 8;
        uint32 qualityGreen = 80, qualityBlue = 18, qualityEpic = 2;
        std::vector<std::pair<uint32, float>> epicByLevel; // level -> factor
        uint32 upgradeGreenToBlue = 15, upgradeBlueToEpic = 10;
        float minContribution = 1.0f;
        bool contributionCountsHealing = true;
        float healWeight = 0.75f, tankWeight = 0.75f, addWeight = 0.5f;   // per point vs damage to the goblin
        float presenceRange = 40.0f;
        float presenceFloor = 50.0f;     // % of the fight present -> at least a fair share
        float creditRange = 60.0f;       // add fights within this range of an awake goblin count

        // the goblin's choices while running
        bool pullEnable = true;
        float pullHealthHigh = 70.0f, pullHealthLow = 45.0f;
        uint32 pullMaxEngagedBase = 3;   // + half the participants
        uint32 allDeadEscape = 20;       // s with every participant dead -> it leaves
        float contributionBonus = 0.5f, contributionMin = 0.5f, contributionMax = 2.0f;
        uint32 lootLifetime = 300;
        uint32 unknownItems = 3;
        uint32 worldforgedItems = 1;
        uint32 goldPerLevel = 30;   // copper per player level, ordinary goblin
        float goldGoblinMultiplier = 20.0f;

        std::array<float, size_t(Area::Count)> spawnChance{};
        uint32 spawnCooldown = 120;   // s between natural spawns per map
        uint32 maxActive = 10;
        float packSmallChance = 3.0f, packLargeChance = 0.5f;
        bool packSameType = false;
        std::array<uint32, KIND_COUNT> weights{};

        // ranks: dungeons/raids use a fixed rank, the open world rolls for rare / rare elite
        Rank dungeonRank = Rank::Elite, raidRank = Rank::Elite;
        float openWorldRareChance = 15.0f, openWorldRareEliteChance = 5.0f;
        std::array<float, 4> rankHealth{ 1.0f, 1.5f, 1.25f, 2.0f };
        std::array<float, 4> rankLoot{ 1.0f, 1.15f, 1.3f, 1.5f };      // quality odds multiplier
        std::array<uint32, 4> rankExtraItems{ 0, 1, 2, 3 };

        uint32 commandMaxSpawn = 25;
        bool commandAnnounce = true;

        // scaled-down copies of higher-level items
        bool scaledEnable = true;
        float scaledChance = 30.0f;      // % of gear drops that are a shrunk higher-level item
        float scaledPowerBonus = 1.10f;  // item level of a copy vs normal gear of that quality at that level
        uint32 scaledBracket = 5;
        uint32 scaledSpellMode = 1;      // 0 none, 1 on-equip only, 2 all
        std::string scaledSuffix;
        uint32 scaledFirstEntry = 9000001;
        bool scaledRuntimeCopies = false;  // make copies the installer didn't (they have no icon in the client)

        // piles: pinata and beams
        bool pinata = true;
        bool beams = true;
        uint32 beamEntry = 987204;
        uint32 beamMinQuality = 0;
        bool personalVisibility = true;  // only the owner sees their piles and beams

        // group rolls (needs the addon; players without it greed automatically)
        uint32 groupRollMode = 1;        // 0 off (personal piles), 1 dungeons and raids, 2 any group
        uint32 rollTimeout = 40;         // s
        float rollBonusDivisor = 10.0f;  // bonus = ceil(share % / divisor)
        bool rollHealerFloor = true;
        bool addonTimer = true;
    };

    Config const& Conf();
    void LoadConfig();

    // ---- loot pools -----------------------------------------------------------------------------
    void BuildPools();
    uint32 PickGear(uint8 quality, uint8 level);          // may be a scaled-down copy
    uint32 PickGoods(uint8 profession, uint8 level);
    uint32 PickUnknown(uint8 level);
    uint32 PickWorldforged(uint8 level);
    float EpicFactor(uint8 level);

    // ---- scaled-down items ----------------------------------------------------------------------
    void InitScaledItems();
    bool ScalingAvailable();
    uint32 ScaledCopy(uint32 baseEntry, uint8 level);   // 0 = can't; baseEntry = fits already
    uint32 BaseOfCopy(uint32 entry);                     // the original of a copy (or entry itself)

    // ---- addon (Rift-style timer, roll window) ----------------------------------------------------
    constexpr char const* ADDON_PREFIX = "LGOB";
    void AddonHello(Player* player);
    bool HasAddon(Player const* player);
    void AddonForget(ObjectGuid const& guid);
    void SendAddon(Player* player, std::string const& message);
    void HandleAddonMessage(Player* player, std::string const& message);

    // ---- loot -------------------------------------------------------------------------------------
    void UpdateLoot(uint32 diff);
    void UpdateLootFor(Player* owner);
    void ResetLootVisuals();
    std::string ItemLink(uint32 entry);
    std::string PoolSummary();
    std::string PoolReport(uint8 level);                  // .lg pools: how many items can drop at a level
    uint32 PickModel();   // 0 = keep the template's model

    // ---- active goblins -------------------------------------------------------------------------
    struct Participant
    {
        float contribution = 0.0f;  // weighted score: damage + healing/tanking/add fights (see Score weights)
        float damage = 0.0f;        // to the goblin (normalised to its level)
        float healing = 0.0f;       // on anyone fighting it or the mobs it dragged in
        float tanking = 0.0f;       // damage taken from those mobs
        float adds = 0.0f;          // damage done to those mobs
        uint32 presenceMs = 0;      // alive and within range of the awake goblin
        uint8 level = 0;
        uint32 lastExtendMs = 0;
    };

    struct GoblinState
    {
        ObjectGuid guid;
        uint32 mapId = 0;
        Kind kind = Kind::Gold;
        Rank rank = Rank::Normal;
        bool awake = false;
        uint32 spawnedMs = 0;
        uint32 firstHitMs = 0;
        uint32 deadlineMs = 0;
        std::unordered_map<ObjectGuid, Participant> participants;
        uint8 boss = 0;            // 0 a goblin, 1 Greed, 2 the hard-mode boss
    };

    std::unordered_map<ObjectGuid, GoblinState>& Active();
    GoblinState* Find(ObjectGuid const& guid);
    bool IsGoblin(Unit const* unit);
    bool IsBoss(Unit const* unit);
    bool BossWanted(Creature const* goblin, GoblinState const& state);   // crowd big enough to call Greed?
    Creature* SummonBoss(WorldObject* from, Position const& at, uint8 tier, GoblinState const* fighters);
    void AddLootGoblinsBossScripts();
    // the names of dropped items: the server answers creature queries for our pile slots per player
    bool PileNameFor(WorldSession* session, uint32 entry, std::string& name, std::string& sub);
    std::string MoneyText(uint32 copper);
    void SendPileNameTo(WorldSession* session, uint32 entry);
    bool SendingPileName();

    Creature* SpawnGoblin(Map* map, Position const& pos, Kind kind, Rank rank, uint32 phaseMask, uint8 level, bool announce, std::string const& where);
    uint32 SpawnPack(Unit* around, uint32 count, Kind const* forced, bool announce, bool packMessage, Rank const* forcedRank = nullptr);
    Rank RankFor(Map const* map);
    bool RandomKind(Kind& kind);

    uint32 ItemForLevel(uint32 reqLevel, uint32 itemLevel);
    void Announce(std::string const& text);
    void AnnounceZone(Unit const* where, std::string const& text);
    std::string Clock(uint32 ms);
    std::string ZoneName(Unit const* unit);

    // Shares (%), with the presence floor: everyone who stayed for PresenceFloor % of the fight gets >= 1/n.
    std::unordered_map<ObjectGuid, float> Shares(GoblinState const& state, uint32 nowMs);

    // Loot for every participant as their own piles around `goblin` (and group rolls for the headline items).
    void DropLoot(Creature* goblin, GoblinState const& state);

    // Creature type flag the patched clients read as "always show a nameplate" (dead too, nameplate settings ignored).
    constexpr uint32 FORCED_NAMEPLATE_FLAG = 0x40000000;
    bool SendGoblinQueryWithPlate(WorldSession* session, WorldPacket const& packet);

    // GM test: one drop of `item` 4 yards in front of `owner` (lift: raise it by that many yards).
    bool DropTestItem(Player* owner, uint32 item, uint32 count, float lift);

    float LevelDamageRatio(uint8 goblinLevel, uint8 attackerLevel);
}

#endif
