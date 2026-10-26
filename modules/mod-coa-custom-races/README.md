# mod-coa-custom-races

Server switch of **CoA Custom** — extra playable races for Conquest of Azeroth (AzerothCore CoA).

| Key | File | Default |
|---|---|---|
| `CoACustomRaces.Enable` | `coa_custom_races.conf` | `0` |

With `CoACustomRaces.Enable = 0` no character of an added race can be created. Existing characters keep their
data, and the original races are never affected.

The races themselves need the rest of the CoA Custom package:

* **Core patches:** release tag `coa-custom-1.8` (CoA Custom commits on top of `Corfirean/azerothcore-wotlk-coa`
  `coa-bots`). They handle race ids above 32, display and talent ids above 65535, racials for every race, custom
  race looks, Wardrobe incarnations on forms, and Gnoll / Saberon / Sethrak appearance.
* **SQL:** reviewed pending world and character migrations in `data/sql/updates/pending_db_*/`.
* **Server DBCs:** The packaged `Data/dbc_races/` overlay is loaded while the module is enabled.
* **Client files:** custom race MPQs (`patch-T.MPQ`, `patch-Z*.MPQ`), the hook `dinput8.dll`, and the Esteria runtime
  (`EsteriaAppearance.dll` / `.bin` files) in the client root.

## Integrated release

The race content is adapted from `coa-custom-1.8`, commit
`0cd699377c7ddc923e63bd00a1ce8c8a77a6bc25` in `ilusixn/azerothcore-wotlk-coa`.
The server overlay comes from `CoA-Custom-1.8-Server.zip`
(SHA-256 `d00a50b89dca96eb39697c68b892481119b2ba6c3c9dce0c6efb3e2226287273`).
Existing overlay-only files remain present. Unchanged standard DBCs, including Spell.dbc, remain in the base data;
the overlay adds the new custom achievement tables without replacing unrelated standard data. New racial spells, dragon races, display choices, weapon skills and
achievement criteria use reviewed pending migrations. Existing character skills are only inserted when missing.
The racial crafting ability uses a separate cooking object and leaves ordinary world flames unchanged.

Manager requires support for the new client overlay, `CoA-Custom-1.8-Client.zip`
(SHA-256 `b0ee9275a373e4024dda7744321d02b686fb1c703603ad635b32bfb6ed9b48aa`),
on top of the pinned base client assets. It includes `patch-ZZZ.MPQ`, updated appearance runtime and cursor addons.
The supplied upstream server executable, installer and whole-table replacement scripts are not used.

The `custom-races-grummle-start` gameplay scenario requires `CoACustomRaces.Enable = 1`
and this checkout's `data/dbc/` files staged as the test DataDir's `dbc_races/` overlay.
It checks fresh Grummle racials and the action bar alongside an unchanged stock racial.

## Manager catalog entry (CoA Server Manager)

```json
{
  "id": "coa-custom-races",
  "name": "CoA Custom Races",
  "description": { "en": "Extra playable races: Jinyu, Sethrak, Gnoll, Saberon, Esteria and Eunoia races and more." },
  "repo": "https://github.com/ilusixn/azerothcore-wotlk-coa",
  "conf": "coa_custom_races.conf",
  "enable_key": "CoACustomRaces.Enable",
  "status": "beta",
  "icon": "races"
}
```

## Credits

* **Kalibros** — Esteria races and the Esteria appearance runtime.
* **Uncle Snowzo** (SnowzoCraft) — Jinyu.
* **Dex** — Drakkin (female).
* **Kalibros** — also Drakkin (male) and Drakonid.
* **Westen** — race lore texts.
* **romaindnd89-cmd** — Prof-bard, the Bard profession module and addon (https://github.com/romaindnd89-cmd/Prof-bard).
* **Furioz, Corruption and the Eunoia team** — Eunoia races.
* **SQUID / Zyth45** — CoA playerbots.
* **Corfirean** — CoA Server Manager, CoA Companions and the `coa-bots` core fork.
* **jealous-sound** — the Conquest of Azeroth AzerothCore core.
* **ilusixn** — CoA Custom.
