# Skinshifter — design and status

A new CoA class. It's a thief who steals creatures' abilities and models and shifts between them. This is a
separate project from subclassing and the Vault handspring. It uses the same CoA servers, and its own module
is `mod-skinshifter`, which never goes into the subclass branch.

## Core
- **Lift** (Possessor: **Possess**): the creature's abilities go into the **Grimoire** for good, and its exact
  model goes into the **Skin Collection**. It works on hostile and friendly NPCs. Faction leaders and bosses
  are refused.
- **Abilities can be cast in any form**, with three levels:
  - **Form-bound:** needs a skin of the right type (breaths, Gore, Web, Burrow).
  - **Element-bound:** can't be cast in a skin of the opposite element (no water spells in fire form).
  - **Learned:** can be cast anywhere.
- **Skin Collection:** wear any collected skin at will, free outside combat. Every model variant counts.
- **Echoes:** shifting into a form gives that type's Echo buff for 20 s. It stays when you shift on. There is
  a cap on active Echoes, 2 at the start. Shifting into the same form again only refreshes it, so you want to
  dance between forms.
- **Talents:**
  - Lingering Skin: +5 s on the Echo from the form you left.
  - Layered Skins: +1 Echo cap, up to 4.
  - Resonance: +% per active Echo.
  - Instinctive Shift: a form-bound ability shifts you automatically into your favourite skin of that type,
    without a GCD.
  - Perfect Mimic (capstone).
- **No tank role.** Off-tank at most. Taunts and threat abilities can be stolen, and defensive Echoes (armor,
  a turtle's shell) are fine, but the kit isn't built for tanking.

## Specs (no CoA class names)
| Spec | Types | Role |
|---|---|---|
| Beastskin | Beast | Melee DPS |
| Courtier | Humanoid (healer mobs) | Healer, and humanoid disguise |
| Felstriker | Demon + Undead | Caster, necro-like |
| Elementalist | Elemental + Dragonkin (fire, frost, acid/nature only) | Caster |

- Arcane and time dragons (blue/bronze) are left out.
- Elementalist: each element (fire, water, earth, air, nature) counts as its own Echo. Its mastery gives more
  elemental damage per distinct elemental Echo.
- Specs give bonuses to their own types. Whether anyone can lift any type is still open; the recommendation
  is yes.

## Races
OG CoA races to start with, plus these from ilusixn's CoA Custom pack. We use only their race IDs and never
touch his files.

| Race | ID | Faction | Flavour |
|---|---|---|---|
| Slime Giant | 104 | Horde | Skinshifter — has a body but no fixed shape, so taking shapes is its nature (best fit) |
| Banshee | 90 | Horde | Possessor |
| Faceless One | 87 | Horde | Skinshifter/Possessor (optional) |
| Grummle | 103 | Alliance | Skinshifter and merchant (the pack) |
| Void Elf | 59 | Alliance | Skinshifter (first idea for the Alliance Possessor; Ethereal took that place) |
| Ethereal | 109 | Alliance | Possessor (energy look: Phase Running), merchant and soul in a wrapping |

Decided later: every race can be a Skinshifter. Possessor (the same class, only its name and look) is for the
Banshee and the Ethereal (Skinshifter.Class.PossessorRaces = "90,109"), one on each side; an Alliance ghost race is
coming and would be one too.

## Possessor (race variant, later)
- The same class and mechanics. It shows as "Possessor" in the UI for Banshee and Ethereal, and possibly Night
  Elf with the wisp lore.
- A worn skin gets a ghostly glow.
- Buff **Possessor, Not a Professor**: a small bonus while possessing.
- When you possess a real professor (Phizzlethorpe, Paleo), the buff becomes **Possessor and a Professor**:
  +5% quest XP, +Int/Spirit, and /say comes out as lectures.
- Hidden achievement "Tenured" (possess both) gives the title "Professor <name>".
- This needs a client patch for the class name, the buff texts and the glow.

## Merchant (later)
- **Merchant's Pack** at level 20: a bag you carry, with the items you want to sell and their prices.
- A vendor skin gives **Set Up Shop**. Others see the money cursor on you and right-click to buy. You keep
  100% of the price.
- Balance against the auction house:
  - **Open for Business** buff: 1 hour, then the shop closes by itself, even if you're AFK.
  - **Closed for Inventory** cooldown: 4 hours in real time, so logging out doesn't skip it. Closing early by
    moving or fighting gives the full cooldown.
  - The Pack is limited: 8 slots at 20 → 16 at 60.
  - Possibly dues each time you open the shop (level × 5 silver). All the numbers go in the config.
- **Own goods only:** the Pack takes only items you crafted ("Made by"), gathered or looted yourself. Items
  you bought or got in trade or by mail are refused, so the shop can't be used to resell auction house goods.
- Professions feed the shop. A later idea: some skins could give a gathering bonus.
- Still to test: whether the client accepts a vendor flag on a player. If not, an invisible NPC copy stands in.

## World data
- 8,788 creatures you can lift from. 35% have no abilities and 25% have 1, so the ability is the
  collectible.
- 9,466 unique models, 6,362 of them from mobs with abilities.
- Realm: level 60 cap, no Outland or Northrend.

## Module status
See the module's README.md for what the pre-release does and how to install it.
