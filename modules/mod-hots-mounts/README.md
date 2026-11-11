# mod-hots-mounts: Heroes of the Storm mounts for WoW 3.3.5a

Every rideable Heroes of the Storm mount (about 560 mounts on 185 models) as a real WoW 3.3.5a mount: one spell per
colour with the mounted aura and 100% mounted speed, a mount item, a Mounts-tab entry, and a mount and pet journal
addon with search, filters and sorting.

**No Blizzard files are in this repository.** The tools read the models, textures and names from *your own*
Heroes of the Storm install and convert them on your PC. Do not share the results publicly.

## Works with

Any one of these servers, with its matching 3.3.5a client:

- [azerothcore-wotlk-coa](https://github.com/stefanw1337/azerothcore-wotlk-coa) as it is (the CoA base), or any fork
  of it, with the CoA client;
- plain [AzerothCore](https://github.com/azerothcore/azerothcore-wotlk) 3.3.5a, with a stock 3.3.5a client.

Nothing has to be built into the server: the mounts are world-database rows plus a client patch. No other module,
race or class is needed.

## What it makes

| Part | Where | What |
| --- | --- | --- |
| Client patch | `<client>\Data\patch-?.MPQ` (see [Patch name](#patch-name)) | Converted models (`Creature\HotSMounts\...`) and textures, plus your client's own `Spell`, `CreatureModelData`, `CreatureDisplayInfo`, `SkillLineAbility` and `Item` tables with the mount rows added |
| Server rows | world database | `creaturemodeldata_dbc`, `creaturedisplayinfo_dbc`, `creature_template` (+ `_model`), `creature_model_info`, `spell_dbc`, `skilllineability_dbc`, `item_template`, `item_dbc` |
| Addons | `<client>\Interface\AddOns` | **CompanionJournal** (any 3.3.5a client) and **CompanionJournal_HotS** (the HotS mounts as a source, GM learn tools) |

Ids: models, displays, creatures, items and Mounts-tab rows use 986001-986999; spells 9860001-9860999. Before
writing, the installer checks that no row in those ranges belongs to anything else and stops if one does. Running
it again replaces its own rows.

## Requirements

- Windows, with Heroes of the Storm installed (the tools read its local files; nothing is downloaded from Blizzard).
- Python 3.10 or newer; `numpy` and `Pillow` (the bat installs them).
- **CascLib.dll**, 64-bit, from [CascLib](https://github.com/ladislav-zezula/CascLib) (MIT): build it with CMake,
  or take `CascLib.dll` from the 64-bit [CascView](http://www.zezula.net/en/casc/main.html) download. Put it next to
  `Make-HotS-Mounts.bat` or point the bat at it.
- [m3studio](https://github.com/Solstice245/m3studio) (GPL-2) reads the `.m3` model files. The tools download it
  into `tools\m3studio` on first use; it is not part of this repository.
- The server's world database running, and a `mysql` client (on PATH, or set `MYSQL` in the bat).
- About 2 GB free for the work folder.

## Use

1. Open `Make-HotS-Mounts.bat` in a text editor and set the folders at the top: your HotS folder, CascLib.dll,
   the work folder, the client's `Data` folder, the server's `worldserver.conf` and its `dbc` folder.
2. Close the game and run the bat. It shows its four steps:
   1. Python packages.
   2. Extract from your HotS install: mount models, animations, mount data, names, then only the textures those
      mounts use (skipped next time when the work folder already has them).
   3. Convert every mount and pack the patch (takes several minutes).
   4. Install: the client patch, the addons and the server rows (also saved as `work\package\hotsmounts-world.sql`).
3. Restart the worldserver (AzerothCore reads the `*_dbc` tables only at start), then start the game.

The steps can also run one by one:

```
python tools\hots_mounts.py extract --hots "<HotS folder>" --casclib CascLib.dll --work work
python tools\hots_mounts.py build --work work
python tools\hots_mounts.py install --work work --client "<client>\Data" ^
    --world-conf "<server>\worldserver.conf" --server-dbc "<server>\data\dbc" [--mysql mysql.exe] [--patch-name patch-X.MPQ]
```

### Patch name

The client tables in the patch must load after every other patch that has them. The installer picks the name:

- a patch it made before keeps its name;
- a client that loads long patch names (the CoA client) gets `patch-ZZZZZZ-HOTS.MPQ`, or `<your last patch>-HOTS.MPQ`
  when a patch already sorts after that;
- a stock client (it loads only one-character names) gets the next letter after your highest `patch-?.MPQ`.

Choose another with `--patch-name` if you need to.

### Stream mode

`--mode stream` (or `MODE=stream` in the bat) leaves the client tables out of the patch and adds the CoA
collection-window rows. Use it only on a CoA server whose build streams new spell, model and collection rows to the
client by itself; every other server uses the default `--mode tables`.

## In game

- `/cj` (also `/mounts`, `/pets`) opens the Companion Journal: search; filters for collected, not collected,
  favourites, ground, flying, source and the HotS collection, rarity and style; sorting; favourites; random
  favourite; a 3D preview; drag to the action bar. On the CoA client a Journal button also sits on the Mounts tab.
- GM character, no target: `/hotsmounts learn [name]` teaches every HotS mount (or those whose name contains the
  text), `/hotsmounts unlearn [name]` takes them away, `/hotsmounts stop` stops a running list.
- `work\package\hotsmounts-ids.txt` lists every spell id with its mount.

## How the conversion works

- Models: HotS `.m3`/`.m3a` read with m3studio's parser; turned 90 degrees to face +X, scaled 0.75; bones baked at
  30 fps into WotLK M2 v264 tracks (Stand with fidgets, Walk, Run, Jump). Textures become BLP2; colour variants
  that swap textures become the model's replaceable textures (CreatureDisplayInfo texture variations).
- Rider: the rider sits on a bone that follows the HotS mount point's position but stays level, so no mount tips its
  rider over. HotS "surf" mounts (boards, discs, clouds, cards, banners) give the rider the Flying Carpet's crouched
  stance, which WoW takes from the mount spell's visual.
- Sounds: each animal family copies the sounds and summon visual of a matching WoW mount (wolf, cat, raptor, bike,
  carpet and so on).
- Effects: HotS particle systems become WoW particle emitters (a close match, not an exact copy); ribbons become
  glowing strips hung on their bone, because a WotLK ribbon only trails while its bone moves; camera-facing parts
  become WoW billboards; flipbook faces show their own frame.

## Files

| File | Job |
| --- | --- |
| `Make-HotS-Mounts.bat` | Runs every step with the settings at its top |
| `tools/hots_mounts.py` | The steps: `extract`, `build`, `install` |
| `tools/casc_extract.py` | Reads named files from the local HotS storage through CascLib |
| `tools/hots_mount_catalog.py` | Lists the rideable mounts, their models, colour variants and names |
| `tools/setup_m3studio.py` | Downloads m3studio |
| `tools/hots_m3.py` | Reads `.m3`/`.m3a` and builds the neutral scene (bones, animations, effects) |
| `tools/build_hots_mounts_all.py` | Converts every mount: M2, skin, BLP textures, rows |
| `tools/m2_model_writer.py`, `tools/m2_writer.py`, `tools/blp.py` | WotLK M2 / skin / BLP2 writers |
| `tools/scene_preview.py`, `tools/m2_texture_preview.py` | Preview images (mesh, textures and particles) |
| `tools/make_package.py` | Packs the patch MPQ, `mounts.json` and the addons |
| `tools/install_hotsmounts.py` | Installs on the client and the server |
| `tools/mpqpull.py`, `tools/mkmergepatch.py` | Plain-Python MPQ reader and writer |
| `addons/` | CompanionJournal and CompanionJournal_HotS (its `Data.lua` is written by the build) |

## Credits

- [CascLib](https://github.com/ladislav-zezula/CascLib) by Ladislav Zezula (MIT): reading the HotS storage.
- [m3studio](https://github.com/Solstice245/m3studio) by Solstice245 (GPL-2): the `.m3` layout and parser.
- Heroes of the Storm and World of Warcraft are Blizzard Entertainment's; their files are not distributed here.
