# mod-subclass

Lets selected CoA characters merge two classes. A character keeps her **main class** (its Class tree, active
specialization, basic kit and resources) and **adds** the **Class tree** and **one specialization** of a second class
(the subclass). That gives four open trees, like WotLK's three plus the class tree. Who may do this is data, not code:
the world table `mod_subclass_pair` lists the allowed race, gender, main class, subclass and subclass specialization.
The default row allows two-headed **Ogres**, which in this CoA build means female Ogres (race 53).

## Requirements

The CoA core must have the talent hooks `AddAscensionTalentTreeProvider` (extra trees) and
`AddAscensionTalentBudgetProvider` in `src/server/coa/AscensionSpecialization.h`. Without them the module does not
compile.

## Install

1. Copy this folder to `modules/mod-subclass` and re-run CMake.
2. Build and start the worldserver. With a source tree, the updater applies `data/sql/db-world`
   (`mod_subclass_pair`) and `data/sql/db-characters` (`mod_subclass_character`). Without one (a prebuilt install),
   import those two files by hand into the world and characters databases.
3. Copy `conf/mod_subclass.conf.dist` to `mod_subclass.conf` next to the worldserver config and adjust it.

## GM commands

| Command | What it does |
|---|---|
| `.subclass info` | Selected player (or you): race, gender, class, active specialization, eligibility, choice. |
| `.subclass specs <classId>` | Lists the specialization ids of a class. |
| `.subclass set <subClass> <subSpec>` | Opens the Class tree of `subClass` and its specialization `subSpec`. |
| `.subclass clear` | Removes the subclass and the talents learned in its trees. |
| `.subclass spec <specId>` | Switches the own active specialization (testing). |
| `.subclass learn <entryId> <rank>` | Sets a talent rank server-side (testing; no budget check). |
| `.subclass reload` | Reloads `mod_subclass_pair`. |

Example, a female Ogre Barbarian: `.subclass set 23 34` adds the Necromancer Class tree and Death. Relog afterwards
so the talent window reloads.

## Rules

- Eligibility is checked whenever the server asks for the player's talent access, so a race or gender change closes
  the borrowed trees at once. At login, a character that no longer qualifies loses the choice and its talents.
- While the borrowed trees are open, the character gets `Subclass.BorrowedPointsPercent` (default 100 = double) extra
  class and specialization points. For now they join the normal pools; a separate pool is planned.
- Borrowed talents stay through own specialization switches and saved-slot (Tome) switches.
- Resources, proficiencies, class spells and the starter kit stay those of the main class. Borrowed abilities that
  depend on the other class's resource or class scripts may not work yet; that is the balancing phase.

## Known limits

- The CoA talent window draws the own class tree and active specialization only. Showing the borrowed trees needs
  client work (an addon or a DBC patch); until then use the GM commands to test.
- Planned: the second head's level-20 quest, the *Book of Origins* (Shadowfang Keep, later random dungeons) and a
  class-trainer menu to make the choice.

## Class and specialization ids

| Class id | Class | Specializations (id = name) |
|---|---|---|
| 12 | Barbarian | 1 = Headhunting, 2 = Brutality, 3 = Ancestry |
| 13 | Witch Doctor | 4 = Shadowhunting, 5 = Voodoo, 6 = Brewing |
| 14 | Felsworn | 7 = Infernal, 8 = Slayer, 9 = Tyrant |
| 15 | Witch Hunter | 10 = Boltslinger, 11 = Houndmaster, 12 = Inquisition, 97 = Black Knight |
| 16 | Stormbringer | 13 = Wind, 14 = Maelstrom, 15 = Lightning |
| 17 | Knight of Xoroth | 16 = Hellfire, 17 = Defiance, 18 = War |
| 18 | Guardian | 19 = Gladiator, 20 = Inspiration, 21 = Vanguard |
| 19 | Templar | 22 = Oathkeeper, 23 = Zealot, 24 = Crusader |
| 20 | Bloodmage | 25 = Fleshweaver, 26 = Sanguine, 27 = Accursed, 99 = Eternal |
| 21 | Ranger | 28 = Archery, 29 = Farstrider, 30 = Brigand |
| 22 | Chronomancer | 31 = Time, 32 = Infinite, 33 = Artificer |
| 23 | Necromancer | 34 = Death, 35 = Animation, 36 = Rime |
| 24 | Pyromancer | 37 = Flameweaving, 38 = Incineration, 39 = Draconic |
| 25 | Cultist | 40 = Heretic, 41 = Corruption, 42 = Godblade, 96 = Dreadnought |
| 26 | Starcaller | 43 = Moon Priest, 44 = Sentinel, 45 = Warden, 100 = Moon Guard |
| 27 | Sun Cleric | 46 = Piety, 47 = Valkyrie, 48 = Seraphim, 98 = Blessings |
| 28 | Tinker | 49 = Demolition, 50 = Mechanics, 51 = Invention |
| 29 | Venomancer | 52 = Fortitude, 53 = Stalking, 54 = Rot, 101 = Vizier |
| 30 | Reaper | 55 = Soul, 56 = Harvest, 57 = Domination |
| 31 | Primalist | 58 = Grovekeeper, 59 = Wildwalker, 60 = Mountain King, 95 = Geomancy |
| 32 | Runemaster | 61 = Engravement, 62 = Glyphic, 63 = Riftblade |
