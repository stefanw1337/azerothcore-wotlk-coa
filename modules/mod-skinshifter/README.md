# mod-skinshifter (pre-release)

**Skinshifter** is a new class for Conquest of Azeroth (CoA, AzerothCore 3.3.5a): a thief who steals creatures'
abilities and bodies. It is class 33, a module of its own, and does not depend on any other module of ours.

This is a pre-release for testing. Expect rough edges, and expect saved data (skins, Grimoire) to be reset
between pre-releases.

## What is in it
- **Steal Form** takes a creature's model into your **Skin Collection** and its abilities into your
  **Grimoire**. Wear any collected skin; the skin decides which stolen abilities you can use.
- **Loadout slots** that open with your level, with the class's own kit (Shed Skin, Slip Away, Mimic Strike,
  Stolen Vigor and more) on top.
- **Specializations** at level 10: Beastskin, Courtier, Felstriker and Elementalist, with their own talent trees
  in CoA's Character Advancement window.
- **Takeover** at level 20: become a rare, an elite or a boss (Hostile Takeover, Willing Host; PvPvE).
- **Steed Shift**: become a mount and carry another player.
- **Possessor**: the same class with a ghostly look for the Banshee and the Ethereal.
- The **SkinCollection** addon: the Skin Collection window, slots, tokens, tooltips and spellbook search.

Design notes are in `docs/`. CoA's level cap is 60.

## Requirements
- A CoA worldserver built from source (this repository or a fork of it).
- **ilusixn's CoA Custom races pack**, server part and client part
  ([CustomRacesCoa](https://github.com/ilusixn/CustomRacesCoa)). The class is offered to the pack's races
  59, 87, 90, 93, 103, 104, 106 and 109 (`Skinshifter.Class.Races`). Without the pack those races do not exist.
- The **64-bit CoA client** from CustomRacesCoa (branch `races-port`), built with the patches in
  `client/coa64/coa64-patches/`. The 32-bit client is not supported.

## Server install
1. Get the source:
   - this branch (`skinshifter`) already contains the module and the core change, or
   - copy `modules/mod-skinshifter` into your own CoA source and apply the core change from its root:
     `git apply modules/mod-skinshifter/skinshifter-core.patch`
2. Build the worldserver as usual (CMake with `-DMODULES=static`).
3. Copy `conf/skinshifter.conf.dist` to your server's module config folder as `skinshifter.conf`.
4. Start the worldserver once. The module creates its character tables, adds class 33 to the world database
   (`chrclasses_dbc`, `player_class_stats`, `playercreateinfo`) and writes these into the server's `DataDir`:
   `dbc_skinshifter/`, `skinshifter_client_spells.txt` and `skinshifter_advancement.txt`.
5. Build the client patch from those files (next section) and give it to your players.

`skinshifter-core.patch` adds class 33 to the core: `CLASS_SKINSHIFTER = 33`, `MAX_CLASSES` 34, and
`ClassMaskBit()` so class 33, which has no bit in the 32-bit WotLK class masks, never picks up other classes'
spells, skills or quests. Skinshifter may use "all classes" items and Rogue items. The server also reads its
client tables from `DataDir/dbc_skinshifter/` first, where the module writes the class's rows.

## Client patch (server owners)
`client/Build-Skinshifter-Client.bat` builds `patch-ZZZZZZ-SKIN.MPQ` from your client's own files and the
files your worldserver wrote. Set the paths at its top, close the game and run it; it must end with
"Done: … every file read back intact". It needs Python 3; with Pillow (`python -m pip install pillow`) it
also adds the talent and Skin Collection pictures. The same thing by hand:

```
python client/tools/mkskinclient.py "<client>/Data" "<client>/Data/patch-ZZZZZZ-SKIN.MPQ" ^
    --server-data "<server DataDir>" --server-conf "<server>/configs/modules/skinshifter.conf"
```

Rebuild it after a module update or a client update, and after changing `Skinshifter.SteedShift.SeatOffset`.

The patch replaces whole client tables, so build it on the same client your players use. To let players build
it on their own clients instead, give them this `client` folder with your worldserver's
`skinshifter_client_spells.txt`, `skinshifter_advancement.txt` and `skinshifter.conf` in `client/server-files/`;
the batch file then uses those.

## Player install
1. Put `patch-ZZZZZZ-SKIN.MPQ` into the client's `Data` folder (or build it with
   `client/Build-Skinshifter-Client.bat` when your server gave you `server-files`).
2. Copy `client/addon/SkinCollection` into `Interface/AddOns`.
3. Build `Ascension64.exe` with `client/coa64/Build-Ascension64.bat` (set the client folder at its top; needs
   Git and Visual Studio 2022 with "Desktop development with C++"). Your old exe is kept as
   `Ascension64.exe.before-skinshifter`.

The patch must come from the same module version your server runs.

## Commands
`.skin` lists the commands. Players: `.skin list`, `.skin wear <n>`, `.skin shed`, `.skin grimoire`,
`.skin forget <spellId|all>`, `.skin fx`, `.skin leave`. Game masters: `.skin tokens`, `.skin unlock allskins`,
`.skin unlock count`.

## Uninstall
Delete `patch-ZZZZZZ-SKIN.MPQ` and the SkinCollection addon from the client. On the server, set
`Skinshifter.Class.Enable = 0` (or build without the module); the module's tables (`skinshifter_*` in the
character database) can then be dropped.

## Known open items
- Steed Shift: the passenger seat is tuned for horse-like mounts; other mount shapes may seat the rider wrong.
  Taking the reins (the passenger steering) is experimental.
- Stolen abilities still use the creature's fixed damage in places.

## License
GNU AGPL v3, like AzerothCore.
