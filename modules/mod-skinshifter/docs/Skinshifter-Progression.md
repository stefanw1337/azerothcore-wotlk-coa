# Skinshifter - progression (draft 6, first iteration: build it and see if it is fun)

Something big every 10 levels. Everything else the class learns by stealing.

## The core rule: the skin decides what you can do
- **Grimoire**: every ability you have ever stolen. Your library; it only grows.
- **Loadout**: the abilities you take with you, in slots that open with your level (Marvel Heroes' Rogue).
  Only slotted abilities are in your spellbook and on your bars. Swapped out of combat, in the Grimoire window.
- **Actives need their skin**: a stolen active works only while you wear a skin of the creature type it came from
  (Beast abilities in any beast skin, humanoid spells in humanoid skins, ...). No healing cat, no casting bear.
- **Passives go anywhere**: slotted passives are skin traits you have collected and work in every form (a wolf
  with the bat's Echolocation). Unslotted traits work only while you wear their skin.
- **Your own kit** works in every form and takes no slot: Steal Form / Possess, Shed Skin / Release, Skin
  Collection, Slip Away / Slip Between Worlds, Mimic Strike, Lure, Stolen Vigor, and the spec signature.

## Slots

| Level | Active slots | Passive slots | Also |
|---|---|---|---|
| 1 | 2 | 0 | The thief's kit |
| 10 | 3 | 1 | **Specialization** (Beastskin, Courtier, Felstriker, Elementalist) |
| 20 | 4 | 2 | **Takeover**: become a rare, elite or boss (hostile and peaceful, see below) |
| 30 | 5 | 3 | **Merchant's Pack** and **Set Up Shop** |
| 40 | 6 | 4 | **Steed Skin** (a collected beast as a mount, with a passenger; flying beasts fly where CoA allows flight, by the same rules as its flying races in 1.9.9) |
| 45 | 7 | 5 | **Quick Change** (change skin in combat, the old skin stays as a decoy) |
| 50 | 8 | 5 | **Trophy Skins** (rares you have taken over, gold in the collection) and **Greater Shapes** (giant and dragon skins at full size and strength) |
| 60 | 9 | 6 | **Thousand Faces** (capstone: every slot open, instant skin change) |

CoA's level cap is 60. Passive slots end at 6, like CoA's own passive slots: the Skinshifter's build is close to a free pick, made of
the stranger, lesser-known abilities of the world's creatures. Talents can add a slot of each later.

## Skin traits
Every skin shows a row of trait icons in the Skin Collection. They come from three places, so every creature has
some without writing 7 000 by hand:
1. **Creature type**: the base buff, always on in that skin. Beast: **Borrowed Hide** (+20% health, +50% armour;
   built).
2. **Family**: the body. Traits change your own kit rather than add a copy of it.
   - Bat: **Leathery Wings** (Glide, CoA's spell 131347: jump while falling), **Echolocation** (stealth
     detection, creatures and treasure on the minimap)
   - Cat: **Prowl** (Slip Away at full speed; the first attack from stealth is a sure critical hit and a short stun)
   - Wolf: Lure pulls a second enemy; pack speed
   - Spider: **Web Leap** (jump to a spot you target: ledges, rocks, roofs) and **Strike from Above** (a leap at an
     enemy; extra damage and a stun when you start 5+ yards higher). True wall or ceiling walking is not possible
     in the 3.3.5 client.
   - Turtle: shell
3. **Name words**: the flavour. Dusk/Night/Shadow: **Duskborn** (stronger at night, by the game clock); Frost/Ice;
   Fire/Flame/Ember; Venom/Plague/Rot; Elder/Ancient/Alpha (a bigger buff).

The **Echo** (the buff a skin leaves when you change out of it) is the skin's strongest trait, lingering for a
moment; not a separate system.

## Level 10: the specializations
Each spec gets one **signature ability** at 10 (in its own spellbook tab) and an **identity passive**. Its talent
tree comes later and builds on these. Each spec lives in its skins: Beastskin in beasts, Courtier in humanoids,
Felstriker in demons and undead, Elementalist in elementals and dragons.

| Spec | Role | Signature ability (level 10) | Identity passive |
|---|---|---|---|
| **Beastskin** | Melee | **Savage Rend**: a tearing strike that leaves a bleed | **Feral Skins**: +5% physical damage |
| **Courtier** | Healer, disguise | **Borrowed Grace**: a quick heal | **Silver Tongue**: +10% healing done |
| **Felstriker** | Demon and undead caster | **Soul Siphon**: drains life from the target to you | **Fel-Touched**: +5% Shadow and Fire damage |
| **Elementalist** | Elemental and dragon caster | **Storm Theft**: a bolt of stolen lightning | **Elemental Attunement**: +5% Fire, Frost and Nature damage |

### Choosing
- In CoA's Character Advancement (N), like every CoA class: pick the spec in its spec view, then buy its signature
  in the class tree (row 0, 1 ability essence at level 10). The identity passive comes with the spec.
- `Skinshifter.Advancement.Enable = 0` brings back the module's own window (`/skinspec`) without talents.

## Level 20: Takeover
You leave your own body and go into a creature: its body, its health, its speed, its action bar. Two abilities, the
same for both flavours, with a name for each:

| | Hostile: you stay a monster, players hunt you | Peaceful: the creature fights on your side |
|---|---|---|
| Skinshifter | **Turnskin** | **Kindred Skin** |
| Possessor | **Hostile Takeover** | **Willing Host** |

Normal creatures cost nothing and are instant (1 token later if it is abused). Rares, elites and bosses cost soul
tokens (below). Peaceful takeovers of elites are allowed in dungeons too, as they are: at 10 tokens (100 kills) for one
elite, the price is the limit. Bosses (world and dungeon) can only be taken over the hostile way: never peacefully, a boss on
your side would clear everything around it.

The Skinshifter's flesh tears open and you slide in; the Possessor's spirit pours into the creature.

### Your body
- Leaves the world while you are inside: hidden, cannot be selected, attacked or hit by area damage.
- The server keeps it at the creature's position; when the takeover ends you step out where the creature stands.
  If the creature dies you are thrown out at that spot.
- Logging out, changing map or a loading screen ends the takeover the same way. The creature goes back to its own
  faction and AI.

### Hostile and peaceful
- **Peaceful**: the creature takes your faction for the time (normal possession) and fights mobs with you.
- **Hostile**: the creature keeps its monster faction. Every player can attack it without being flagged; you carry
  a temporary PvP flag and can attack players (the game normally forbids a player-controlled unit to attack an
  unflagged player, so the module allows it during a hostile takeover). CoA's bots fight it like any rare or boss.
- Not in capitals, sanctuaries or starting zones.

### Leash and reset
- The creature's home point is kept. Past the leash (start: 40 yards, in the config) you evade: control is taken
  away, the creature runs home on its own, immune and out of combat, heals to full like a normal mob reset, and you
  get control back inside the 10 yard ring.
- **Reset** on the creature's action bar does the same when you are out of combat.
- Dungeon bosses follow their own evade rule (see World bosses and dungeon bosses).

### World bosses and dungeon bosses
- **World bosses**: hostile takeover only (as every boss), you steer it.
- **Dungeon bosses: the DM.** A Possessor or Skinshifter who is not in the group goes into the boss of another
  group's run and plays it 100% against that group, like a dungeon master or an invader:
  - you steer the boss fully: its own spells plus the kit, scaled against the size of the group;
  - within the boss's own rules: its room, its evade and reset, its phases;
  - your hidden body is moved into that group's instance for the fight and back out afterwards;
  - one try per run: after a wipe, a reset or the boss's death you are out;
  - rewards both ways: you for wipes and for time survived in the fight, the group for a fast kill (more than a
    normal run, for the risk).
  - Whether groups must opt in (e.g. a "Haunted" choice when entering) is open.

### The kit
Rares, elites and bosses have too few spells for a player. A taken-over creature keeps its own and gets a kit from
the Grimoire copies of creatures of its type and level, more the higher its rank: two or three damage abilities
(one hits several targets), one crowd control (stun, fear or root), a self-heal, a short damage reduction and a
magic buff players can purge or spellsteal.

### Scaling
- Solo: the creature as it is plus the kit (a player already uses it better than the AI).
- Each extra attacker (players and bots that hit or attacked you in the last 10-15 seconds): more health, keeping
  the health percentage, and a little more damage; capped at about 5 attackers. Hard against many, not impossible.

### Rewards
- **The hunters**: a target time per fight from the creature's rank and the number of attackers. Under it: full
  bonus (e.g. a "Possessed" reward bag or bonus experience); up to twice it: half; slower: the normal loot only.
- **You**: soul tokens for every minute in combat with players, more with more attackers; player kills extra.
  Time resetting, running home or out of combat does not count; a cap per takeover.
- **Against farming**: only players outside your group and guild count, for both sides; rewards shrink for the
  same players again within a short time; the takeover has a cooldown.

### Soul tokens
The class's currency for takeovers (and later other powerful spells), so kills are worth collecting.
- 10 kills that give experience = 1 soul token (grey creatures do not count). Saved per character, kept session to
  session. **Capped at 20** (a boss).
- Cost by rank: rare 5, elite 10 (100 kills), boss 20. Normal creatures are free. The costs, the kills per token
  and the cap are config settings, to tune later. The cap must be a multiple of 20 (the counter has 20 slots, each
  slot is cap / 20 tokens); any other value is rounded up to the next multiple, with a warning in the server log.
- With the cap, a full counter is one boss or two elites: before a dungeon you choose what to save for.
- Kills made by a creature you have taken over do not fill the counter (else a free takeover could farm tokens).

### The class window: soul counter and takeover timer
- A round counter with 20 token slots (in the Skin Collection addon, near the player frame), filling as you earn.
  Option for later: 10 slots of 2 tokens each (a rare is two and a half slots, so slots can be half full); the cap
  then only has to be a multiple of 10.
- A new token: the green skull flashes over your head (Unholy Presence's skull, which Wraith Running no longer
  shows), and the slot lights up.
- A "pling" at 5, 10 and 20: the costs of a rare, an elite and a boss, so you hear when you can afford one (they
  follow the cost settings when those change).
- During a takeover the counter shows the timer: how long you have survived in combat with players, and what it is
  worth so far.

### Kill credit and loot
- When a takeover ends, the creature you were in dies and you get its kill (quests, achievements) and its loot,
  in addition to the survival rewards. In a hostile takeover the players who kill you get theirs too.

### Open
1. A level range for attacking players in a hostile takeover, or open to all.
2. The scaling numbers (health and damage per extra attacker, the cap).
3. Duration and cooldown (start: 30 seconds or more, shorter for bosses).
4. Dungeon DM: whether groups opt in, how a DM is matched to a run, and the rewards for both sides.

## Order of work
1. **Own copies of stolen abilities** (in progress): each stealable creature ability gets a Skinshifter copy with
   a global cooldown, a cooldown the client shows, a mana cost, damage that grows with your level, and a
   spellbook tab by creature type (Beast, Humanoid, Undead, Demon, Elemental, Dragonkin, Giant, Mechanical).
2. **The loadout**: slots, the Grimoire window, only slotted abilities known.
3. **Actives need their creature type's skin.**
4. **Traits** as passives, starting with the bat (Glide, Echolocation, Duskborn).

## A big thing for every spec (first iteration)
Each spec gets one thing that changes how it plays, as big as the others (a capstone or a high talent):
- **Courtier: Disguise** (decided). In a humanoid skin, NPCs react to you as to that creature's faction: the Defias
  camp lets you walk through, your own guards attack you, everything else reacts as to a Defias. Breaks when you
  attack (and maybe near an elite or a boss for too long). NPCs only: players are not affected (else it would be the
  hostile takeover). Done with the server's reaction hook, as CoA's Witch Hunter disguise does.
- **Beastskin: The Pack.** When you change out of a beast skin, the old skin stays and fights at your
  side for 20 seconds (at most two). Changing skins becomes a way to build a pack.
- **Felstriker: Corpse Puppet.** A creature you kill rises in its own skin as an undead or fel servant
  for 30 seconds, with its own abilities.
- **Elementalist: Elemental Shift.** Changing from one element's skin to another (fire, frost, earth,
  air, and dragons and giants by their element) releases a burst of the old element around you; the next spell
  of the new element is stronger.
- **Thousand Faces** (class capstone, 60): every slot open (any stolen ability in any slot; the skin rule stays) and
  changing skin is instant.

## The trees hold the loadout (draft)
The loadout moves into the Character Advancement trees: a node is a **slot**, not an ability (the trees are the
same for every Skinshifter, so a node cannot hold a different spell per player).
- Buying a slot node opens the slot; you choose what is in it from the Grimoire (any stolen ability of the slot's
  type), and swap it out of combat. The slot shows the ability you put in it.
- **Upgrades sit under a slot** and belong to the slot, not to the ability: swap in another ability and it has the
  upgrades at once. Upgrades: triggers twice (a chance), leaves a passive effect (bleed, damage over time), more
  critical strike chance, shorter cooldown, lower cost. Done by the module, which knows what is in each slot.
- **Tier 1 of every spec tree: two slots**: one **open** (any creature type) and one of the spec's type.
  Higher slots are of the spec's type: Beastskin beast, Courtier humanoid, Felstriker demon and undead,
  Elementalist elemental, dragonkin and giant (nearly every giant is an elemental giant). Mechanical and other
  creatures have no spec: open slots only. The class tree has slots of any type and the passive (trait) slots.
- The skin rule stays: an active ability works only in a skin of its type, in an open slot too.
- Slots also open with level (the Marvel Heroes idea): tier 1 at 10, the second spec slot at 30, the third at 50.

### Beastskin tree (draft 1)
Talent essence: 25 points at level 60 (CoA's level cap); about 41 points of talents, so not everything can be taken
(about the same share as CoA's own trees). Rows 4-6 open at 5
points spent in the tree, rows 7-9 at 10. The identity passive (Feral Skins: +5% physical damage, Dual Wield) sits in
the corner and comes with the spec.

| Row | Node | Ranks | What it does |
|---|---|---|---|
| 0 | **Open Slot** | 1 | A slot for any stolen ability (level 10) |
| 0 | **Beast Slot I** | 1 | A slot for a stolen beast ability (level 10) |
| 1 | Echoing Theft (under Open Slot) | 2 | The ability in the Open Slot has a 10/20% chance to trigger twice |
| 1 | Honed Theft (under Open Slot) | 2 | +5/10% critical strike chance for the ability in the Open Slot |
| 1 | Twin Bite (under Beast Slot I) | 2 | The ability in Beast Slot I has a 10/20% chance to trigger twice |
| 1 | Rending (under Beast Slot I) | 2 | It leaves a bleed: 10/20% of its damage over 6 sec |
| 1 | Primal Focus (under Beast Slot I) | 2 | Its cooldown is 15/30% shorter |
| 2 | Feral Swiftness | 2 | +4% attack speed per rank |
| 2 | Pack Instinct | 3 | +3% Agility per rank |
| 2 | Thick Hide | 2 | +5% armor per rank |
| 2 | Bleeding Edge | 3 | Savage Rend's bleed +10% per rank |
| 3 | **Beast Slot II** | 1 | A second beast slot (level 30) |
| 4 | Savage Double (under Beast Slot II) | 2 | 10/20% chance to trigger twice |
| 4 | Gnashing (under Beast Slot II) | 2 | +5/10% critical strike chance |
| 4 | Lingering Wound (under Beast Slot II) | 2 | Leaves a bleed: 10/20% of its damage over 6 sec |
| 4 | Predatory Strikes | 3 | +2% melee critical strike chance per rank |
| 5 | Pack Leader **or** Lone Hunter (choose one) | 1 | Pack Leader: Lure pulls a second enemy and your group gets +3% damage; Lone Hunter: +10% damage when no ally is within 15 yards |
| 6 | **Beast Slot III** | 1 | A third beast slot (level 50) |
| 7 | Feral Echo (under Beast Slot III) | 2 | 10/20% chance to trigger twice |
| 7 | Bloodscent (under Beast Slot III) | 2 | +10/20% damage against bleeding targets |
| 7 | Savage Endurance | 2 | +4% Stamina per rank |
| 9 | **Apex Predator** (capstone) | 1 | In a beast skin, a critical hit with an ability in a Beast slot resets that slot's cooldown (once per 10 sec per slot) |

Open for this draft: the names, the numbers, whether the choice row should use CoA's own "choose one" groups, and the
other three specs on the same plan once this one is right.

## Talent trees (Character Advancement)
The class tree uses ability essence (AE), each spec tree talent essence (TE), from the Reaper's budget: AE 1 at
level 10, 11 at 30 and 26 at 60; TE 1 at 11, 10 at 30 and 25 at 60 (CoA's level cap). Talents that change the Skinshifter's own spells use spell family 16
(each spell has a bit of its own), so the tooltips in the window and on the bars show the changed numbers.

### Class tree (ability essence; rows 4 and 8 open at 8 and 16 points spent)

| Row | Talent | Ranks | Effect (per rank) |
|---|---|---|---|
| 0 | Savage Rend / Borrowed Grace / Soul Siphon / Storm Theft | 1 each | The signatures: buying one chooses that spec |
| 1 | Quick Hands | 2 | Reduces the cast time of Steal Form and Possess by X sec. |
| 1 | Thick Skin | 3 | Increases your armor by X%. |
| 1 | Nimble Fingers | 2 | Increases your chance to dodge by X%. |
| 2 | Mimicry | 3 | Increases the damage of Mimic Strike by X%. |
| 2 | Stolen Stamina | 3 | Increases your Stamina by X%. |
| 2 | Long Reach | 2 | Increases the range of Lure by X yards. |
| 3 | Skin Memory | 1 | Reduces the global cooldown of Shed Skin, Release and Skin Collection by X sec. |
| 3 | Fleet Feet | 2 | Increases your movement speed by X%. |
| 3 | Hardened Hide | 1 | Borrowed Hide gives twice as much health and armor. |
| 4 | Resonance | 3 | Increases all damage you deal by X%. |
| 4 | Vigorous Theft | 2 | Increases the healing of Stolen Vigor by X%. |
| 4 | Clean Getaway | 1 | Shed Skin and Release also free you from all movement impairing effects. |
| 5 | Opportunist | 3 | Increases your chance to get a critical strike with all attacks and spells by X%. |
| 5 | Mana Thief | 2 | Increases your maximum mana by X%. |
| 5 | Sticky Fingers | 2 | Reduces the cooldown of Lure by X%. |
| 8 | Perfect Mimic | 1 | Mimic Strike's echo strikes at full strength instead of half. |
| 8 | Thousand Skins | 1 | Every skin you wear gives you Borrowed Hide, not only beasts. |

### Beastskin (talent essence; rows 4 and 7 open at 5 and 10 points spent)

| Row | Talent | Ranks | Effect (per rank) |
|---|---|---|---|
| corner | Feral Skins (+5% physical damage; brings Dual Wield) | free | Identity passive, comes with the spec |
| 0 | Bleeding Edge | 3 | Increases the bleed damage of Savage Rend by X%. |
| 0 | Thick Hide | 2 | Increases your armor by X%. |
| 1 | Feral Swiftness | 2 | Increases your attack speed by X%. |
| 2 | Predatory Strikes | 3 | Increases your chance to get a critical strike with melee attacks by X%. |
| 2 | Pack Instinct | 3 | Increases your Agility by X%. |
| 4 | Rending Echo | 2 | Increases the damage of Mimic Strike by X%. |
| 4 | Savage Endurance | 2 | Increases your Stamina by X%. |
| 7 | Apex Predator | 1 | Increases your physical damage by X%. |

### Courtier (talent essence; rows 4 and 7 open at 5 and 10 points spent)

| Row | Talent | Ranks | Effect (per rank) |
|---|---|---|---|
| corner | Silver Tongue (+10% healing) | free | Identity passive, comes with the spec |
| 0 | Bedside Manner | 3 | Increases the healing of Borrowed Grace by X%. |
| 0 | Court Etiquette | 2 | Reduces the mana cost of Borrowed Grace by X%. |
| 1 | Quick Study | 2 | Reduces the cast time of Borrowed Grace by X sec. |
| 2 | Graceful | 3 | Increases your Spirit by X%. |
| 2 | Patron's Favor | 3 | Increases your healing by X%. |
| 4 | Renewing Vigor | 2 | Increases the healing of Stolen Vigor by X%. |
| 4 | Meditation | 2 | Allows X% of your mana regeneration to continue while casting. |
| 7 | Masquerade | 1 | Increases your healing by X%. |

### Felstriker (talent essence; rows 4 and 7 open at 5 and 10 points spent)

| Row | Talent | Ranks | Effect (per rank) |
|---|---|---|---|
| corner | Fel-Touched (+5% Shadow and Fire) | free | Identity passive, comes with the spec |
| 0 | Borrowed Hellfire | 3 | Increases your Shadow and Fire damage by X%. |
| 0 | Soul Toll | 2 | Increases the damage of Soul Siphon by X%. |
| 1 | Dark Pact | 2 | Reduces the mana cost of Soul Siphon by X%. |
| 2 | Fel Intellect | 3 | Increases your Intellect by X%. |
| 2 | Grave Chill | 3 | Increases your chance to get a critical strike with spells by X%. |
| 4 | Lingering Torment | 2 | Increases the duration of Soul Siphon by X sec. |
| 4 | Demonic Resilience | 2 | Reduces all damage you take by X%. |
| 7 | Damned Chorus | 1 | Increases your Shadow and Fire damage by X%. |

### Elementalist (talent essence; rows 4 and 7 open at 5 and 10 points spent)

| Row | Talent | Ranks | Effect (per rank) |
|---|---|---|---|
| corner | Elemental Attunement (+5% Fire, Frost and Nature) | free | Identity passive, comes with the spec |
| 0 | Convergence | 3 | Increases your Fire, Frost and Nature damage by X%. |
| 0 | Storm Caller | 2 | Reduces the cast time of Storm Theft by X sec. |
| 1 | Static Charge | 3 | Increases the damage of Storm Theft by X%. |
| 2 | Elemental Focus | 2 | Reduces the mana cost of Storm Theft by X%. |
| 2 | Molten Core | 3 | Increases your chance to get a critical strike with spells by X%. |
| 4 | Stoneskin | 2 | Increases your armor by X%. |
| 4 | Tidecaller | 2 | Increases your Intellect by X%. |
| 7 | Avatar of the Elements | 1 | Increases your Fire, Frost and Nature damage by X%. |

## How it is built
- Server: skinshifter-core.patch lets CoA's class code take class 33 (12-33 instead of 12-32 where CA needs it) and
  reads `Data/dbc_skinshifter/` before `Data/dbc/`. The module writes its rows of the CA tables there at start-up,
  each a copy of a Reaper row with our values over it.
- Client: the same rows go into the client patch from `Data/skinshifter_advancement.txt`, plus the class names in
  CoA's `CharacterAdvancementUtil.lua` and `IsCustomClass` true for a Skinshifter (so N opens CoA's window).
- Not done yet: our own tree backgrounds (CoA shows its fallback picture), the loadout and traits.


## Talent rework (November 2026)
Built in the module; the tables above are the older drafts.

### Weapons by specialization
Every Skinshifter: cloth, leather, daggers, one-handed swords, fist weapons, staves. Off-hand items ("held in
off-hand") need no skill, so every spec can hold one.

| Spec | Brings |
|---|---|
| Beastskin | Dual Wield |
| Courtier | Shield (and Block), one-handed maces |
| Felstriker | Two-handed swords and axes, one-handed axes |
| Elementalist | Shield (and Block), one- and two-handed maces |

Changing spec puts weapons you can no longer use back in your bags (or by mail when the bags are full).

### Slots grow in different ways
Not every slot gets the whole menu of upgrades:

| Slot | Upgrade |
|---|---|
| Thief's Slot (class) | one kind: Double Take (strike twice) |
| Second Thief's Slot (class) | a set of two: Light Fingers (shorter cooldown and crit) |
| Open Slot (every spec) | one kind: Honed Theft (crit) |
| Slot I (spec type) | the spec's own kind: Rending (bleed) / Gentle Hands, Malice, Surge (damage and healing) |
| Slot II | two choices, stronger: strike twice **or** crit, and cooldown **or** damage |
| Slot III | a set of three, stronger still: strike twice, crit and damage (Primal Trinity, Grand Masquerade, Unholy Trinity, Primal Convergence) |

### New talents
- **Beastskin** (dual wield): Ambidexterity (+10% off-hand damage per rank), Twin Fangs (3% per rank: an auto
  attack strikes again at once), **Hydra Head** (ability: three bolts, each a random Poison Bolt, Arcane Missile or
  Sand Speckle).
- **Courtier** (shield healer): Courtly Guard (+2% block per rank), Gilded Bulwark (+15% shield block value per rank).
- **Felstriker** (spellblade: caster and blade in one): Fel Conduit (spell damage +10% of attack power per rank),
  Blade of the Mind (attack power +10% of Intellect per rank), **Cerberus** (ability: three bites, Physical, Fire and
  Shadow, 60% weapon damage each).
- **Elementalist** (shield caster): Earthen Guard (+2% block per rank), Elemental Ward (-2% magic damage taken per
  rank).

### Ideas next
- **Purloined Art**: a deep class-tree slot for an ability from another spec's tree (the thief who steals another
  Skinshifter's craft).
