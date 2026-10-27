# Subclassing and two new races: handover to CoA Custom 2.0

Written for ilusixn by Stefan (stefanw1337), October 2026. It covers what to merge, how it builds next to your bot
module, which data goes where and what is still open. The branch below is the source of truth.

## What it adds

- **Subclasses** (`modules/mod-subclass`): a character keeps the main class's trees and borrows one specialization
  tree and the basic kit of a second class. The quest "Book of Origins" opens it from level 20
  (`Subclass.MinLevel`). Bots pick the least-used subclass.
- **Two races** built around it:
  - **Two-Headed Ogre**, race **85**, female, Horde. Copy of your Ogre (53) with her own model; the second head
    casts and shoots for the subclass.
  - **Thin Twins**, race **96**, male, Alliance. Copy of the Forgotten (32) with a twin brother who rides along.
- **LiveMon** (`modules/mod-livemon` and `LiveMon.exe`): a local window showing how every spec does with and without
  a subclass. It only monitors; nothing leaves the owner's PC. Optional.

## What to merge

Branch **`handover-ilusixn-2.0`** on `stefanw1337/azerothcore-wotlk-coa`: one commit on top of Corfirean's
`coa/integrate/server-20261026` (`ee7e5d26c`, the tree that integrates CoA Custom 1.9.2). Its diff is our work only:

- `modules/mod-subclass` and `modules/mod-livemon`.
- PlayerScript hooks the module uses: `OnPlayerGetQuestRaceMask`, `OnPlayerKeepsOwnLook`,
  `OnPlayerBeforeBoardedBy` and `OnPlayerLoadForeignSkill` (a subclass keeps its weapon and armor skills at login;
  `Player::_LoadSkills` otherwise deletes skills its race and class do not list).
- CoA class code asks `UsesAscensionClassMechanics(player, CLASS_X)` instead of `getClass() == CLASS_X`, so a
  subclass gets that class's resources and mechanics.
- `Unit::getRaceMask` and `ReputationMgr::ApplyStartingZoneReputation`: 85 moves from the Night Elf group to the Orc
  group (it copies 53) and 96 joins the Human group (it copies 32). Without this the Ogre starts with Alliance
  reputation and every Horde town is hostile.
- Spell, trainer and spellbook changes the borrowed kit needs (`src/server/game/Spells`, `Trainer.*`,
  `modules/mod-spellbook`).

The modules use only core APIs (`WorldSession::IsBot()` for bots), so your `mod-playerbots` (`coa-custom-1.9.1` with
v2.2.1) builds as before. Nothing depends on Corfirean's bot module or on Content Scaling.

## Race ids

- 85 and 96 are reserved for these races. Your `rev_20261025070000_custom_races_19.sql` still writes rows for 85
  (Teldrassil start, Hero kit). Our `rev_20261026180000_mod_subclass_keep_race_85.sql` must run **after** it; it
  copies the Ogre's rows from 53 again, without the Hero class.
- If you want 85 for a race of your own, our fallbacks are **117** (Ogre) and **64** (Thin Twins). They keep the same
  `(race - 1) & 31` bit, so client reputation bases stay the same. The ids appear in SQL, config, DBCs, `mkraces.py`
  and `Subclass*.cpp`.

## Models

- Two-Headed Ogre: display **977600**, model **901400**, `Character\Ogre\Female\TwoHeadedOgre.mdx`.
- Thin Twins: display **977601**, model **901401**, on the CoA Custom 1.8 path
  `custom\thinhuman\native\male\thinhumanmale.mdx`. Our patch puts the 1.8 model there with the cape graft and the
  helmet attachment raised; `EsteriaAppearanceGeometry.bin` carries our skin under that path (the DLL substitutes it,
  and the original one crashed with a cloak). 1.9.2 moved the Forgotten to `Character\thinhuman\male\`, so nothing of
  yours uses the old path any more. Their looks are the 1.8 race 32 rows in `client/thin_twins_sections.tsv`; the
  textures are still in your `patch-ZE7`/`patch-ZE8`. Build order from the clean `patch-ZE7` model: `tools/mkcape.py`
  (cape, collar behind the shoulders, helmet seat), `tools/mkhathair.py` (geoset 1, a short crop on the scalp), then
  `tools/embedskin.py` into `EsteriaAppearanceGeometry.bin`.
- Thin Twins hair: their hairstyles are geosets 2, 3 and 4 (short, medium, long), then bald (geoset 5, which the
  model leaves empty). Geoset 1 is the short crop the client shows under hats that hide hair: it forces the hair group
  to geoset 1 (`0x4EF1DD`) by a race bit that 96 shares with 32, so that cannot be switched off for 96 alone.
- The Ogre's capes are reshaped by `tools/mkogrecape.py` so they drape over her hump and stay clear of her body while
  she stands, shuffles or readies a weapon; `--check` reports how far her body reaches through them per animation.

## Data

`modules/mod-subclass/tools/mkraces.py <source folder> <output folder>` clones 53 → 85 and 32 → 96 in every race
table it finds (ChrRaces, CharSections with the Ogre's eye colours and the Thin Twins' frozen looks, CharHairGeosets,
CharacterFacialHairStyles, NameGen, EmotesTextSound, HDEmotesTextSound, UICameraAppearanceChrRaces,
CharacterCreation{Class,Archetype,Shapeshift}Details, CharStartOutfit, CharBaseInfo, CreatureDisplayInfo,
CreatureModelData). Run it on your server `files/dbc` and on your client tables whenever you change a race we copy.

- Server, `Data/dbc_races/` (with `CoACustomRaces.Enable = 1`): your 1.9.2 `files/dbc` with our rows.
- Server, `Data/dbc_clientset/CreatureDisplayInfo.dbc`: the client's table **with our patch**, so the display stream
  does not resend our displays without textures.
- Client tables we also change, carried onto yours with `tools/carrydbc.py <WoWDBDefs definitions> <old base> <ours>
  <new base> <out>`: Spell (our spells), ItemDisplayInfo, SkillLineAbility (we only set ClassMask 0 on 13 931 rows,
  so a subclass can learn them), SkillRaceClassInfo (ClassMask on 135 rows), SpellRange, SpellShapeshiftForm,
  SpellVisual, SpellVisualEffectName, SpellVisualKit, SpellVisualKitModelAttach, Vehicle, VehicleSeat.
- Character creation glue: your 1.9.2 `CharacterCreate.lua` with `client/GlueXML/subclass_races.lua` appended.

## SQL

- World: `modules/mod-subclass/data/sql/db-world/*.sql`, after your numbered files, with
  `rev_20261026180000_mod_subclass_keep_race_85.sql` last. They are re-runnable.
- Characters: `modules/mod-subclass/data/sql/db-characters/` and `modules/mod-livemon/data/sql/db-characters/`, once.

## Client

Our archives sort after your `patch-ZZZZ.MPQ`:

- `patch-ZZZZS.MPQ`: the tables above, the glue, the Ogre and Thin Twins models.
- `patch-ZZZZC.MPQ`: second-head models. `patch-ZZZZW.MPQ`: held-weapon models.
- `EsteriaAppearanceGeometry.bin`: yours, with our skin under the old Thin Human path.
- Addons: `Ascension_Subclass` (required), `SpellActivationOverlay-for-CoA` (optional).

## Config

`mod_subclass.conf.dist` holds the defaults we test with. `Subclass.ThinHuman.Enable` and
`Subclass.TwoHeadedOgre.Enable` switch the races; `Subclass.StandardClasses` and `Subclass.StandardMains` open the
feature to the standard classes.

## Known issues

- Under hats that hide hair the Thin Twins show the short crop whatever their hairstyle (see Thin Twins hair).
- When Auto Shot starts, the Ogre's gun jumps to the other hand for a moment.

Seen in your code and data while merging:

- `ApplyStartingZoneReputation` runs at every login and resets the home-city standings of every custom race, so
  reputation earned with those cities is lost on relog.
- On the 1.9.2 Forgotten model, hats float above and behind the head in character creation.
- `CharacterCreationClassDetails` (client `patch-ZZS`) has rows for race 85 from your planned race, so its class
  preview gear works by accident.
