# mod-loot-goblins

Diablo-style Loot Goblins for AzerothCore-based servers (made on CoA). A plain module: drop the folder into
`modules/`, re-run CMake and build. No core changes, so it builds next to any other modules.

- Goblins turn up now and then when players kill things (chance per area in the conf), sometimes in packs, with a
  server-wide announcement. They count their coins until someone hits them.
- First hit: the timer starts (3 min, + time per hit, never past 5 min), the goblin turns red and runs.
- Last 15 s it tries to open a portal; a hit breaks the attempt. When time runs out the portal can't be stopped.
- Hard CC shows Immune; other harmful effects last half as long. Only players (and pets) can hurt it.
- Its level drops to the lowest participant; higher levels hit like that level (plus a small edge); no hit takes
  more than 5% of its health; more health for every extra participant.
- On death every participant gets personal piles of loot for their own level; a bigger share improves the odds,
  never the right to loot. Share = damage to the goblin + healing, tanking and damage in the fights it drags the
  group into; whoever stays in the fight most of the time gets at least a fair share, whatever their role.
- It judges the group while it runs: healthy groups get dragged through packs, struggling ones don't. If everyone
  fighting it is dead, it portals away.
- Elite in dungeons and raids; rare or rare elite (silver) now and then in the open world, with more health and
  better loot.
- Loot flies out of it like a pinata and lands in piles with loot beams in the item's quality colour; each player
  only sees their own piles.
- Higher-level items can drop shrunk to your level (any quality, ~10% stronger than normal there, locked).
  Uses CoA's item resolver; switched off on cores without it.
- In dungeons and raids the headline items are rolled for (Need / Greed / Pass in the addon):
  roll = 1-100 + ceil(share % / 10); the winner may get it upgraded (base % x roll / 100 x share multiplier).
- The LootGoblins addon shows a Rift-style countdown bar (players without it get the fatigue bar).
- 19 types: Gold, Uncommon / Rare / Epic loot, Unknown (boss loot), Worldforged loot (CoA), War spoils and 12
  profession goods.
- Every drop is its own lootable corpse with a loot beam: weapons and shields as their real models, armour and
  jewellery as Diablo III item models, money as a gold heap. With a client that supports forced nameplates the
  addon shows Diablo III-style item labels in the item's quality colour, stacked so they don't overlap.
- World boss chain: a goblin enough of the zone piles onto brings Greed; kill her fast for a hard-mode boss.
- The addon also shows map pins and a zone list (arrow, distance, click to target).
- GM: `.lootgoblin` / `.lg` spawn [count] [type] [rank] | pack [small|large] | list | despawn [all] | escape | info |
  boss [hard] | drop <itemid> [count] [lift] | pools [level] | reload

Every option is explained in `conf/mod_loot_goblins.conf.dist`.

## Status

Work in progress, tested on a local CoA server. Not released yet.

## What is not in this repository

The models (Diablo III, Heroes of the Storm and retail WoW) and the client tables built from a player's own client
are Blizzard's and are never committed. The Loot Goblins extraction tool (coming as a release of this repository)
downloads them from Blizzard's CDN and builds everything on each PC: the client patch, the world rows (creature
entries 987101-987206 and the drop entries from 1044480, the `lootgoblin_models` and `lootgoblin_beams` tables), the
scaled-down item copies and their client icons, and the addon's map data.

The module creates `lootgoblin_scaled_item` in the characters database itself (never delete its rows: items use them).

## Client parts

- `client/LootGoblins/`: the addon (copy to `Interface/AddOns/`). `LootGoblins_Maps.lua` is written by the
  extraction tool from the player's own client.
- `client/coa64/`: the forced-nameplate change for the CoA64 client (creature type flag `0x40000000`), also sent
  upstream as a pull request. Only creatures a server marks with that flag are affected.

Credits: see `CREDITS.md`.
