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
2. Build and start the worldserver. With a source tree, the updater applies `data/sql/db-world` and
   `data/sql/db-characters`. Without one (a prebuilt install), import the files by hand, in file-name order: the
   `db-world` files into the world database and the `db-characters` files into the characters database.
3. Copy `conf/mod_subclass.conf.dist` to `mod_subclass.conf` next to the worldserver config and adjust it.

## The story

1. When an eligible character reaches `Subclass.MinLevel` (also by a GM level change, and at login for characters
   already past it), her second head speaks up in a menu of her own: the quest **Two Heads, One Book**.
2. Archmage Arugal in Shadowfang Keep (normal, heroic and mythic) drops the **Book of Origins** for characters on
   the quest.
3. Reading the Book completes the quest (no quest giver: it is from herself, for herself), consumes the Book and
   gives one **insight**.
4. Any class trainer then offers "[Second head] We want to learn the way of another class": pick a class, then
   one of its specializations. That spends the insight and opens the borrowed trees.
5. After the first quest, every 5-player dungeon boss has a `Subclass.BookDropChance` chance to give an eligible
   character another Book, for changing the choice later.

### Race stories

Each eligible race can tell the story its own way. The world table `mod_subclass_story` (race, gender mask) names
the quest, the relic item that completes it, the three menu texts and the lines of the voice. A race without a row
uses the Ogre story above (`Subclass.QuestId` and `Subclass.BookItemId`).

The **Thin Twins** (male Thin Humans, race 32) have a story of their own. At the same level the quieter twin steps
out from behind his brother: **One From the Same**. Their father, a mason of the Stonemasons' Guild, earned a
**Bravery Badge** while Stormwind was rebuilt, then followed Edwin VanCleef when the nobles refused to pay the
guild. VanCleef in the Deadmines (normal, heroic and mythic) drops the badge; the twin pins it on, gains the insight
and, at a trainer, chooses his own way. Later dungeon bosses can give another Bravery Badge instead of a Book.

## The second twin (prototype)

With two units, each twin wears his own clothes: the character is one normal Thin Human, and his twin is a companion
the server dresses like a mirror image. He follows at his brother's right side and cannot be targeted. His look
(item appearances per visible slot) is stored in `mod_subclass_twin`; until a wardrobe tab exists, GM commands dress
him:

| Command | Effect |
|---|---|
| `.twin` | Calls the twin out, or sends him away. |
| `.twin wear <slot> <item>` | Shows an item's appearance on one slot (head, shoulders, shirt, chest, waist, legs, feet, wrists, hands, back, tabard). |
| `.twin strip [slot]` | Takes off one slot, or everything. |
| `.twin copy` | Copies your own visible outfit. |
| `.twin info` | Shows what he wears. |

He travels with his brother: after a second of running he hops onto his back (piggyback), when mounted he sits right
behind him on any mount, and he steps off beside him when he stands still, swims, flies or fights. Both seats are
extra vehicle seats (`vehicle_dbc` 9530 and 9531 with seats 9530 and 9531); the client needs the same rows in its
`Vehicle.dbc` and `VehicleSeat.dbc` (`patch-ZZX.MPQ`). On a real two-seat mount, which already has seats of its own,
he walks beside it instead.

For this test, remove the client's two-twin model patch (`patch-ZZZ.MPQ`), or every Thin Human shows two twins.

## GM commands

| Command | What it does |
|---|---|
| `.subclass info` | Selected player (or you): race, gender, class, active specialization, eligibility, choice. |
| `.subclass specs <classId>` | Lists the specialization ids of a class. |
| `.subclass set <class> <spec> [spec2]` | CoA class: Class tree and `spec`. Standard class (1-11): `spec` and `spec2`. |
| `.subclass clear` | Removes the subclass and the talents learned in its trees. |
| `.subclass spec <specId>` | Switches the own active specialization (testing). |
| `.subclass learn <entryId> <rank>` | Sets a talent rank server-side (testing; no budget check). |
| `.subclass quest` | Shows the second head's quest prompt (if the character qualifies). |
| `.subclass insight <count>` | Sets how many unspent insights the character holds (testing the trainer menu). |
| `.subclass reload` | Reloads `mod_subclass_pair` and `mod_subclass_story`. |

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

## Standard classes

The ten standard classes (Warrior 1 ... Druid 11) have no Class tree. Their Warcraft Reborn trees from
`CharacterAdvancement.dbc` load into the CoA catalog under borrow-only class ids (100 + class, e.g. Mage = 108), which
no character has, so only a granted extra tree opens them. They are offered at trainers only with
`Subclass.StandardClasses = 1`. Borrowing one opens two of its three specializations, and the bonus class points
become specialization points. In `mod_subclass_pair.sub_class` use the normal class id (8 for Mage).

## The subclass tab (client addon)

`addon/Ascension_Subclass` goes into the client's `Interface/AddOns`. It adds a tab named for the race (**Two-Head**
for Ogres, **Two-Thins** for the Thin Twins): with a CoA main class next to the Character Advancement window's
tabs. With a standard main class, the talent key opens a wide Cataclysm-style window instead: a Character
Advancement page with the class's three trees side by side, and the subclass tab next to it (the classic window
stays one click away). `/subclass` opens either. It draws the borrowed trees from the client's own
`C_CharacterAdvancement` data, with the borrowed class's purchasable abilities under each tree. The addon and the
server talk through addon whispers to oneself with the prefix `SUBCLASS`: `SYNC` asks for the state (`RESET`, `TREE`,
`IDS`, `RANKS`, `POINTS`, `DONE`), and `LEARN <entry> <rank>` changes a borrowed rank through the same validation as
the native window's uploads (points, prerequisites, level and removal prices), answered with `RESULT`.

## Known limits

- The game's own talent window shows only the character's own trees; the subclass tab shows the borrowed ones.
  Saving from the native window keeps borrowed talents it does not show.
- A borrowed CoA class's base kit (`ClassSpells`) and its class skill lines (the spellbook tabs) are granted while its
  trees are open and removed when the choice changes. Its resources follow the core's resource rules through
  `UsesAscensionClassMechanics` (a borrowed Ranger also gets a focus pool); the addon shows them in a small movable
  frame under the player frame. Other class scripts that test the player's own class do not apply yet.

- The CoA talent window draws the own class tree and active specialization only. Showing the borrowed trees needs
  client work (an addon or a DBC patch); until then use the GM commands to test.
- The Book of Origins (item 9530010) and the Bravery Badge (item 9530020) are not in the client's item cache, so
  their icons may show as a question mark until the client has asked the server about it once.

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
