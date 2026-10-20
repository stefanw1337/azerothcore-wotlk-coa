# mod-livemon

Live statistics from the worldserver, for balancing (subclass combos above all) and for watching a local server.

## What it records

Per character, for the server session and per minute:

- damage done and taken, healing done and taken (pets and guardians count for their owner)
- damage and healing per ability (ability damage is counted before armor and resistances)
- crowd control given and taken: count and seconds of stun, fear, confuse, root, silence, pacify, charm and possess
- deaths, kills, time in combat, yards moved
- race, class, level, the talent tree with the most points and the mod-subclass choice (`mod_subclass_character`)

The minute totals go to the characters table `livemon_minute` (created on startup). The window groups them into
combos and compares a subclassed combo's damage with the same main class without a subclass.

## Live feed

`http://127.0.0.1:8089/live` (characters) and `/history?minutes=60` (combos), JSON. Settings are in
`mod_livemon.conf.dist`; the feed only listens on this computer unless `LiveMon.Bind` is changed.

## The window

`gui/` is the LiveMon window (Go, standard library only). Build it for Windows:

    cd gui && GOOS=windows GOARCH=amd64 go build -ldflags "-H windowsgui -s -w" -o LiveMon.exe .

`LiveMon.exe` opens in its own Edge (or Chrome) window. Its troll face is read from the WoW client's own MPQ archives
(set `WowFolder` in `livemon.ini` next to the exe, or place the exe inside the client folder); no game art ships
with the module. `livemon.ini` can also set `Server` (default `http://127.0.0.1:8089`) and `Browser`.
