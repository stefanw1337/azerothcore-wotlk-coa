# CoA Universal Content Scaling Architecture

## 1. Executive Summary

CoA Universal Content Scaling is a modular, data-driven system for AzerothCore / Conquest of Azeroth that decouples character level limits (`MaxPlayerLevel`) and group sizes from authored game content. Instead of destructive SQL migrations altering base tables (`creature_template`, `quest_template`, `item_template`), this system introduces an immutable realm-level progression layout, content-era resolvers, combat budget normalization, dynamic instance/group scaling (from solo 1-player up to 40-player), and adaptive encounter mechanics.

The architecture comprises three distinct, decoupled modules:
1. **`mod-coa-content-scaling`**: Core engine providing progression layouts, era compression algorithms, combat budget profiles, group scaling with encounter snapshot locking, spell/hazard scaling, item budget scaling, and diagnostic commands (`.coascale`).
2. **`mod-coa-tbc-content`**: The Burning Crusade content pack (Outland zones, dungeons, raids, level 58-70 curve, TBC encounter adapters).
3. **`mod-coa-wotlk-content`**: Wrath of the Lich King content pack (Northrend zones, dungeons, raids, level 68-80 curve, WotLK encounter adapters).

Classic content is the permanent base world and occupies the available level range when no expansion packs are active. When TBC, WotLK, or both are loaded, the active epochs automatically partition the level space up to `sWorld->getIntConfig(CONFIG_MAX_PLAYER_LEVEL)`.

---

## 2. Authoritative Scaling Pipeline

To avoid double-scaling and circular dependencies, every combatant, quest, access gate, and item follows one authoritative progression pipeline:

```text
AUTHORED WoW DATA (DB / DBC templates)
        ↓
CONTENT ERA & TIER RESOLUTION (Classic / TBC / WotLK / Custom; World / Dungeon / Raid)
        ↓
PROGRESSION LAYOUT COMPRESSION (Authored level range -> Effective level range)
        ↓
BASE EFFECTIVE CONTENT LEVEL (Immutable per realm configuration)
        ↓
OPTIONAL LOCAL / VIEWER SCALING (mod-destiny-weaver / LocalLevelScaling)
        ↓
INSTANCE GROUP SCALING (1..N players, frozen on encounter pull)
        ↓
ENCOUNTER ADAPTATION & BUDGET NORMALIZATION (Target counts, add waves, role assist)
        ↓
FINAL COMBAT / REWARD GAMEPLAY STATE
```

Under this hierarchy:
* Authored data remains untouched in database tables.
* Effective level is calculated from the Progression Layout.
* Local viewer scaling (`LocalLevelScaling`) is evaluated on top of the **effective** content level, never the raw authored level.
* Instance group scaling adjusts health, damage, healing, absorbs, and loot counts based on the frozen encounter snapshot.
* Old systems (like `FlexHealth` in `mod-coa-raid-difficulty`) do not multiply on top; rather, custom calibrated profiles (e.g. `coa_boss_flex`) serve as high-priority budget providers within the unified scaling engine.

---

## 3. Core Hooks & Lifecycle Audit

### 3.1 Startup & Lifecycle Sequence (`src/server/game/World/World.cpp`)
1. **`World::LoadConfigSettings()`** (Line 322):
   - Loads world configuration (`sWorld->getIntConfig(CONFIG_MAX_PLAYER_LEVEL)`).
   - Fires `sScriptMgr->OnAfterConfigLoad(reload)` at Line 301.
   - **Integration point**: `ProgressionLayout` is computed and frozen here. If structural configs (MaxPlayerLevel, enabled packs, custom boundaries) change during reload, the engine logs a warning and requires a server restart.
2. **`sScriptMgr->OnLoadCustomDatabaseTable()`** (Line 380):
   - Invoked before DBCs load. Module database tables (`coa_content_*`) can be initialized here.
3. **`sObjectMgr->LoadItemTemplates()`** (Line 529):
   - Items loaded from `item_template`.
4. **`sObjectMgr->LoadCreatureTemplates()`** (Line 541):
   - Creature templates loaded from `creature_template`.
5. **`sObjectMgr->LoadCreatureClassLevelStats()`** (Line 562):
   - Base stats per level and class loaded into `CreatureBaseStatsContainer`.
6. **`sObjectMgr->LoadQuests()`** (Line 613):
   - Quests loaded from `quest_template`.
7. **`LFGMgr::LoadDungeons()`** (via `sLFGMgr->Initialize()`):
   - Dungeon and raid access requirements loaded.
8. **`sScriptMgr->OnStartup()`**:
   - Fires after world initialization completes.
   - **Integration point**: Run full content validation report, compile encounter adapter registries, and pre-cache tier budgets.

### 3.2 Creature Level & Combat Stats Hooks
* **`AllCreatureScript::OnBeforeCreatureSelectLevel(CreatureTemplate const* cinfo, Creature* creature, uint8& level)`** (`Creature.cpp:1516`):
  - Called inside `Creature::SelectLevel` before `SetLevel(level)`.
  - Determines the effective base level for the creature based on its era and layout mapping.
* **`AllCreatureScript::OnCreatureSelectLevel(CreatureTemplate const* cinfo, Creature* creature)`** (`Creature.cpp:1565`):
  - Called at the conclusion of `Creature::SelectLevel`.
  - Applies `CreatureScaleContext`: replaces or normalizes `BaseHealth[cInfo->expansion]`, `ModHealth`, `DamageModifier`, and weapon damages to match the CoA target combat budget and content tier.
* **`UnitScript::OnUnitEnterCombat(Unit* unit, Unit* victim)`**:
  - Detects encounter pull (`JustEngagedWith`).
  - Freezes `InstanceScaleContext` for the encounter across boss, adds, and summons.
* **`UnitScript::OnUnitExitCombat(Unit* unit)`** / **`OnUnitDeath`**:
  - Resets / unfreezes `InstanceScaleContext` on wipe, reset, or boss defeat.

### 3.3 Combat & Damage Modification Hooks
* **`UnitScript::ModifyMeleeDamage(Unit* target, Unit* attacker, uint32& damage)`** (`Unit.cpp:1815`):
  - Called pre-armor in `CalculateMeleeDamage`. Scales outgoing creature melee damage by the group damage curve.
* **`UnitScript::ModifySpellDamageTaken(Unit* target, Unit* attacker, int32& damage, SpellInfo const* spellInfo)`** (`Unit.cpp:1546`):
  - Called in `CalculateSpellDamage`. Scales direct spell damage by the group damage curve and content budget.
* **`UnitScript::ModifyPeriodicDamageAurasTick(Unit* target, Unit* attacker, uint32& damage, SpellInfo const* spellInfo)`** (`SpellAuraEffects.cpp:6525`, `6646`):
  - Scales periodic DoT tick damage.
* **`UnitScript::ModifyHealReceived(Unit* target, Unit* healer, uint32& heal, SpellInfo const* spellInfo)`** (`Unit.cpp:8922`):
  - Scales healing received.
* **`UnitScript::OnAfterAuraEffectCalculateAmount(AuraEffect const* effect, Unit* caster, int32& amount)`** (`SpellAuraEffects.cpp:630`):
  - Scales absorb shields and flat numeric aura values based on group scale.

### 3.4 Quests & Progression Hooks
* **`LocalLevelScaling.h` & `PlayerQuest.cpp:54`**:
  - `Player::GetQuestLevel(Quest const* quest)`:
    Returns `LocalLevelScaling::ScaleQuestLevel(effectiveBaseLevel, playerLevel)`.
  - Core helper `GetEffectiveQuestBaseLevel(Quest const* quest)` ensures authored 70/80 quests are compressed to their layout band (e.g. 45-55 on cap 60) before any viewer scaling is applied.
* **`Player::SatisfyQuestLevel(Quest const* qInfo, bool msg)`** (`PlayerQuest.cpp:1043`):
  - Must evaluate against effective minimum level, preventing authored level 68/78 requirements from locking players out on cap 60 realms.

### 3.5 Dungeon & Map Access Hooks
* **`MapMgr::CanPlayerEnter`** (`MapMgr.cpp:180`):
  - Calls `sScriptMgr->OnPlayerCanEnterMap(player, entry, instance, mapDiff, loginCheck)`.
  - Blocks access to disabled expansion maps (e.g. Outland or Northrend when disabled).
* **`MapMgr.cpp:187`**:
  - Raid group requirement check: `(!group || !group->isRaidGroup()) && !sWorld->getBoolConfig(CONFIG_INSTANCE_IGNORE_RAID)`.
  - Dynamic content scaling allows configurable solo raid entry when solo scaling is enabled.
* **`Player::Satisfy(DungeonProgressionRequirements const* ar, ...)`** (`PlayerStorage.cpp:6883`):
  - Evaluates `ar->levelMin` and `ar->levelMax`. Effective access level replaces raw authored gate levels.

---

## 4. Integration with Existing Systems

### 4.1 `LocalLevelScaling` Integration
`LocalLevelScaling` (used by `mod-destiny-weaver`) provides per-viewer open world leveling. The universal content scaling engine integrates cleanly:
* **Principle**: `LocalLevelScaling` operates on **effective base levels**, not authored levels.
* Authored WotLK Quest 78 -> Effective Base Quest Level 58 (on cap 60 layout).
* If viewer scaling is active for a Level 50 player: `ScaleQuestLevel(58, 50)` = 58. For a Level 60 player: `ScaleQuestLevel(58, 60)` = 60. Neither exceeds `MaxPlayerLevel`.
* Similarly, for creatures: `ScaleCreatureLevelForViewer(effectiveLevel, playerLevel, offset)`.

### 4.2 `mod-coa-raid-difficulty` / `FlexHealth` Integration
`FlexHealth.cpp` previously scaled raid bosses directly if found in `coa_boss_flex`.
* **Conflict Prevention**: If both `mod-coa-content-scaling` and `mod-coa-raid-difficulty` are active, `mod-coa-content-scaling` acts as the single authoritative scaler.
* **Data Reuse**: `coa_boss_flex` is preserved as a calibrated HP-per-player database. When a boss has an entry in `coa_boss_flex`, the content scaling engine uses that calibrated budget rather than generic heuristics.
* `ApplyFlex` in `FlexHealth.cpp` delegates to or is subsumed by the unified pipeline, preventing the fatal `old_flex * new_flex` double-scaling error.

### 4.3 Creature Health Ownership (`CoA.CreatureScaling` in the core)
Three systems touch creature health. Each owns a different place, and there is exactly one authority per creature:

| Creature | Authority | Formula |
|---|---|---|
| Open world | `LocalLevelScaling` (per viewer) + core `CoA.CreatureScaling.World.*` | unchanged |
| Dungeon / raid, module enabled | this module | `ceil(base HP at effective level * ModHealth * tier * group scale * Difficulty.Health.*) * rank rate` |
| Dungeon / raid, module disabled or absent | core `CoA.CreatureScaling.Dungeon/Raid/Map*` | unchanged |

* **Contract**: at script load the module registers `CoAContentScaling::OwnsCombatScaling` with the core (`CreatureScaling::SetCombatAuthority`). The core asks it at the moment of scaling; for an owned dungeon or raid creature the core's health **and** damage multiplier is `1.0`. Because the answer is looked up when needed, nothing depends on script registration order, and `coa.conf` never has to be edited. Open world creatures are never handed over, and with `CoAContentScaling.Enable = 0` the module answers `false`. A second registration (`SetHealthBudgetAuthority`) makes the core's own health hook skip every creature the module budgets, because the module already folds `CoA.CreatureScaling.World.Health` into that budget once; the open world multiplier is therefore applied exactly once whichever level selection hook runs first.
* **No stacked rank factors**: `ModHealth` already carries the rank (CoA data: elite trash median 3.0, encounter bosses 5 to 20, world bosses in the hundreds), so the budget has no extra elite `x2` or world boss `x4`. The normal dungeon tier is `1.0`; heroic and raid tiers keep their explicit progression.
* **Boss identity** (`CombatBudgetProfile::IsBossTemplate`): `CREATURE_FLAG_EXTRA_DUNGEON_BOSS` (set at load from `instance_encounters`) or rank `WORLDBOSS`. The same test locks encounters and classifies the budget; ordinary elite trash is trash.
* **Old versus new at a full party** (authored HP `A = base HP * ModHealth`, core dungeon multiplier 2.5): elite trash `A * 1.35 * 2 * 2.5 = 6.75 A`, now `A`; dungeon boss `A * 1.35 * 2 * 2.5`, now `A`. The party size curve (0.217 / 0.419 / 0.616 / 0.809 / 1.0 for 1 to 5 players) is unchanged.
* **Diagnostics**: with `CoAContentScaling.Debug = 1` every combat budget logs one line with the full breakdown; `.coascale creature` prints the same factors for the selected creature.

---

## 5. Group Scaling & Anti-Exploit Architecture

### 5.1 Participant Count
```text
Real Player:               1.0 weight
Playerbot (Player object): 1.0 weight
GameMaster:                0.0 weight
Spectator:                 0.0 weight
Pet / Guardian / Minion:   0.0 weight
Vehicle / Passenger NPC:   0.0 weight
```

### 5.2 Encounter Snapshot Freezing
1. **Pull Trigger**: When any boss or instance combat begins (`OnUnitEnterCombat` / `JustEngagedWith`), the instance records a frozen snapshot:
   - `snapshotPlayers = CalculateEffectivePlayers(map)`
   - `snapshotContext = ComputeInstanceScaleContext(map, snapshotPlayers)`
2. **Combat Invariance**:
   - If a player zones in, leaves, dies, or disconnects, `snapshotContext` remains unchanged.
   - Any adds, minions, or secondary bosses spawned during the encounter inherit the exact same `snapshotContext`.
3. **Reset / Wipe / Victory**:
   - On encounter reset or wipe, the snapshot is unlocked.
   - A subsequent pull recalculates the snapshot based on the new participant count.

### 5.3 Non-linear Scaling Curves
Direct linear division (`1 / 25` = 4% damage) results in bosses dealing trivial damage to solo players, while full damage wipes them instantly.
* **Health Scale**: Scales near-linearly to match group DPS throughput:
  $$H(r) = r^{\alpha}, \quad \text{where } \alpha \approx 0.95 \text{ to } 1.0, \quad r = \frac{\text{effectivePlayers}}{\text{intendedPlayers}}$$
* **Damage Scale**: Scales sub-linearly with a floor to keep mechanics lethal while survivable:
  $$D(r) = D_{\text{min}} + (1 - D_{\text{min}}) \cdot r^{\beta}, \quad \text{where } \beta \approx 0.6 \text{ to } 0.75, \quad D_{\text{min}} \approx 0.20 \text{ to } 0.35$$

---

## 6. Item Budget & Tooltip Strategy

### 6.1 Server-Client Synchronization
In WoW 3.3.5, client item tooltips (name, stats, armor, damage, required level) are requested dynamically via `CMSG_ITEM_QUERY_SINGLE` and answered via `SMSG_ITEM_QUERY_SINGLE_RESPONSE` (`ItemHandler.cpp:450-542`).
* When `ItemBudgetScaler` adjusts effective stats and required level on `ItemTemplate`, the client receives and displays these exact effective stats in the tooltip automatically.
* Only DBC-bound client-side spell strings (for certain proc descriptions) remain static. Spells with calculated combat points scale via `SpellInfo` adjustments at startup.

### 6.2 Combat Budget Normalization
A level 80 item compressed to level 60 must not retain level 80 combat ratings:
* Ratings (crit, haste, hit, mastery, defense) are converted to effective stat budgets using standard Blizzard stat allocation tables adapted for CoA.
* Weapon DPS and armor values are mapped from source item level to target item level.

---

## 7. Diagnostics & Administration

Admin commands under `.coascale`:
* `.coascale status`: Shows active modules, `MaxPlayerLevel`, active eras, and global settings.
* `.coascale layout`: Displays calculated ProgressionLayout (Classic, TBC, WotLK bands).
* `.coascale creature [target]`: Displays authored level, effective level, era, tier, stat budget, group scale multiplier, and final HP/damage.
* `.coascale instance`: Displays current map ID, intended players, effective players, frozen status, and active scaling multipliers.
* `.coascale quest <id>`: Displays authored quest level/minlevel vs effective quest level/minlevel.
* `.coascale item <id>`: Displays authored item budget vs effective scaled budget.
* `.coascale validate`: Runs consistency and compatibility checks across all enabled content.
