# mod-coa-custom-races

Server switch of **CoA Custom** — extra playable races for Conquest of Azeroth (AzerothCore CoA).

| Key | File | Default |
|---|---|---|
| `CoACustomRaces.Enable` | `coa_custom_races.conf` | `1` |

With `CoACustomRaces.Enable = 0` no character of an added race can be created. Existing characters keep their
data, and the original races are never affected.

The races themselves need the rest of the CoA Custom package:

* **Core patches:** branch `coa-custom-1.5-csm` (CoA Custom commits on top of `Corfirean/azerothcore-wotlk-coa`
  `coa-bots`). They handle race ids above 32, display and talent ids above 65535, racials for every race, custom
  race looks, Wardrobe incarnations on forms, and Gnoll / Saberon / Sethrak appearance.
* **World SQL:** `data/sql/db-world/`. It is idempotent, so it can run on every update and never touches player data.
* **Server DBCs:** `files/dbc/` go into the server's `Data/dbc`.
* **Client files:** custom race MPQs (`patch-T.MPQ`, `patch-Z*.MPQ`), the hook `dinput8.dll`, and the Esteria runtime
  (`EsteriaAppearance.dll` / `.bin` files) in the client root.

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
