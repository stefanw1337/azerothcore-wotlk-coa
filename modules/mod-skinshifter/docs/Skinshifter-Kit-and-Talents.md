# Skinshifter — starter kit and talent trees (draft 1)

A draft to change and add to. Everything here can still be renamed, moved or deleted.

## Starter kit (level 1)

### Gear
- Rogue starting gear for the race: leather chest, pants and boots, plus a dagger.
- An off-hand **Thief's Satchel**: a small bag in the off-hand slot that is purely cosmetic, a nod to the
  Merchant's Pack at level 20.
- Skills: cloth, leather, daggers, one-handed swords, fist weapons, staves and unarmed. Plus the race language
  and defense; that part is fixed in the latest build.

### Abilities at level 1
These are new spells with their own icons (ImagineArt) and IDs in the 1 990 000 range.

| Ability | What it does | Notes |
|---|---|---|
| **Steal Form** | 1.5 s channel on a creature or NPC: steals its abilities into the Grimoire and its look into the Skin Collection | Replaces `.skin steal`. Friendly NPCs work too ("Study") |
| **Shed Skin** | Instant: back to your own form, drops a little threat, gives the True Form Echo | Replaces `.skin shed` |
| **Skin Collection** | Opens the collection window; click a skin to wear it | The window comes as an addon; until then it wears the last skin |
| **Mimic Strike** | Melee builder: weapon damage plus a little nature damage, costs mana | Your own baseline attack before you've stolen anything |
| **Stolen Vigor** | Small heal over 9 s on yourself | Keeps you alive at level 1–10 |
| **Slip Away** | Out-of-combat stealth: moves at 70% speed, broken by damage | The thief side; also lets you get close enough to steal |

At levels 4, 8 and 12 come **Grimoire slots 2–6** (how many stolen abilities you can have active), and the **Echo
cap** starts at 2.

### Where to steal at the start
No class trainer: the class grows by stealing. The **Book of Ascension** can be used to show "nearby creatures with
abilities you don't have yet", built on the creature map in research/. In the starting zones the first targets are
already mapped (Shadow Sprite for Shadow Bolt, Webwood Lurker for Poison, Scarlet Initiate for Fireball/Frost
Armor, Palemane Skinner for Rejuvenation, Defias Cutpurse for Backstab, and so on).

## Talent trees

The layout is a CoA-style class tree plus one spec tree. Each tree has around 10–12 talents in 5 tiers, with a
capstone at the end.

### Class tree: Skinshifter (shared by everyone)
| Tier | Talent | Effect |
|---|---|---|
| 1 | **Quick Hands** (3) | Steal Form 0.5 s faster per rank |
| 1 | **Thick Skin** (2) | +5% armor per rank while wearing a skin |
| 2 | **Lingering Skin** (2) | The Echo from the form you left lasts +2.5 s per rank |
| 2 | **Pickpocket's Eye** (1) | Creatures with abilities you don't have glitter faintly |
| 2 | **Skin Memory** (2) | Wearing a skin costs no global cooldown; rank 2: also in combat, 30 s cooldown |
| 3 | **Layered Skins** (2) | +1 Echo cap per rank (max 4) |
| 3 | **Resonance** (3) | +1% damage and healing per active Echo per rank |
| 3 | **Grimoire Binding** (1) | +1 active stolen ability |
| 4 | **Instinctive Shift** (1) | Form-bound abilities shift you into your favourite skin of that type automatically, without a GCD |
| 4 | **Clean Getaway** (2) | Shed Skin also removes snares; rank 2: also roots |
| 5 | **Perfect Mimic** (capstone) | With all Echo slots full, stolen abilities used in their own form get their bonus effect |

### Spec 1: Beastskin (melee)
- **Bleeding Edge** (3): stolen bleeds tick 10% harder per rank.
- **Pack Instinct** (2): a beast Echo also gives +5% speed per rank.
- **Pounce** (1): shifting into a beast skin makes your next attack a guaranteed crit.
- **Thick Hide** (2): a turtle/crocolisk/bear Echo gives +10% armor per rank (off-tank at most).
- **Venom Glands** (3): stolen poisons get +1 stack per rank.
- **Alpha's Howl** (1): an active ability that copies the Echo of your last beast form to your group for 10 s.
- **Feral Rhythm** (2): Mimic Strike gives energy-free procs from beast abilities.
- **Capstone, Apex Predator:** at 4 different beast Echoes, every attack bleeds.

### Spec 2: Courtier (healer and disguise)
- **Bedside Manner** (3): stolen heals +4% per rank.
- **Borrowed Faces** (2): disguise in humanoid form lasts until you heal or attack.
- **Gossip** (1): when you steal from a friendly NPC you also get a rumour (a minimap marker for a rare/chest nearby).
- **Second Opinion** (2): your heals have a 10% chance per rank to repeat on the next-lowest ally.
- **Court Etiquette** (3): mana costs of stolen heals −5% per rank.
- **Masquerade** (1): an active ability. The group gets your humanoid disguise for 8 s and enemies lose their targets.
- **Patron** (2): your HoTs give the target +3% damage reduction per rank.
- **Capstone, Possessor, Not a Professor:** heals from a possessed healer NPC's form get a free extra HoT, and if
  it is a real professor, the joke buff.

### Spec 3: Felstriker (demon and undead caster)
- **Borrowed Hellfire** (3): stolen fire and shadow spells +3% per rank.
- **Grave Chill** (2): stolen frost spells snare 10% more per rank.
- **Soul Toll** (1): Drain-type stolen spells heal 20% more.
- **Curse Weaver** (2): stolen curses last +3 s per rank.
- **Corpse Echo** (1): killing something while wearing an undead skin gives a short Echo of its type.
- **Fel Burst** (2): shifting into a demon skin does an AoE fire hit for 5% of spell power per rank.
- **Dread Pact** (3): stolen shadow spells cost health instead of mana, for −2% cost per rank.
- **Capstone, Damned Chorus:** with both a demon and an undead Echo active, every other stolen spell hits twice.

### Spec 4: Elementalist (elementals and dragons)
- **Convergence** (mastery): +1.5% elemental damage per distinct element Echo (fire, water, earth, air, nature).
- **Molten Core** (3): fire Echoes also give +1% crit per rank.
- **Tidecaller** (2): water Echoes heal you for 1% of damage done per rank.
- **Stoneform** (2): earth Echoes give +5% armor per rank.
- **Stormstep** (1): air Echoes give +15% speed and remove the cast time of your next lightning spell.
- **Dragon's Breath** (2): stolen breath attacks get a +15% wider cone per rank (fire, frost and acid only).
- **Elemental Balance** (3): with 2+ different element Echoes, mana regen is +5% per rank.
- **Capstone, Avatar of the Elements:** with 4 element Echoes, your next spell combines them all.

## What has to be built for this
1. **New spells** for the starter kit, in the client patch (Spell.dbc, from the newest client file) and on the
   server (a spell_dbc override read from the server's own Spell.dbc). The behaviour comes from the module.
2. **The Skin Collection window** (an addon).
3. **The talent trees**, in CoA's Character Advancement format (TalentTab plus CoA's own tables). This needs
   looking into how CoA builds its trees.
4. **Echo buffs**: auras per type.
