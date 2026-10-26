"""Installs the subclass test package on a CoA Manager repack.
    install.py [--client <Ascension folder>] [--no-client] [--no-backup] [--check]
Run through Install-Subclass.bat, which uses the repack's own Python. The CoA-Subclass folder belongs next to the
repack's Scripts folder."""
import json
import re
import shutil
import subprocess
import sys
import time
from datetime import datetime
from pathlib import Path

HERE = Path(__file__).resolve().parent
FILES = HERE / "files"
sys.path.insert(0, str(FILES / "tools"))

from apply_coa_sql import SCHEMAS, Log, Manifest, Server, Update, apply, plan  # noqa: E402

PATCHES = ("patch-ZZZZS.MPQ", "patch-ZZZZC.MPQ", "patch-ZZZZW.MPQ")
ADDONS = ("Ascension_Subclass", "SpellActivationOverlay-for-CoA")
ALREADY_THERE = re.compile(r"ERROR (1050|1060|1061) ")


def say(text=""):
    print(text, flush=True)


def fail(text):
    say()
    say("ERROR: " + text)
    sys.exit(1)


def option(name):
    return sys.argv[sys.argv.index(name) + 1] if name in sys.argv[:-1] else None


def ask(question, default=None):
    answer = input(question + (f" [{default}]" if default else "") + ": ").strip().strip('"')
    return answer or default


def find_repack():
    root = HERE.parent
    if (root / "CoA-Bots" / "Core" / "worldserver.exe").exists():
        fail("This is a Jealous-Sound repack with CoA-Bots. Its worldserver uses a different bot module, so this test "
             "package does not replace it yet. Use a CoA Manager repack, or wait for the CoA-Bots build.")
    for needed in ("Scripts/manage.py", "mysql/admin-client.ini", "Core/worldserver.exe", "Runtime/python/python.exe"):
        if not (root / needed).exists():
            fail(f"{root / needed} is missing. Extract CoA-Subclass into the repack folder (next to Scripts).")
    return root


def find_client(previous):
    if "--no-client" in sys.argv:
        return None
    guesses = [option("--client"), previous, r"C:\Ascension Local",
               r"C:\Program Files\Ascension Launcher\resources\client", r"C:\Ascension\resources\client"]
    default = next((g for g in guesses if g and (Path(g) / "Ascension.exe").exists() and (Path(g) / "Data").is_dir()),
                   None)
    while True:
        folder = ask('Game folder (the one with Ascension.exe), or "skip" if the game runs on another PC', default)
        if folder and folder.lower() in ("skip", "no", "none"):
            return None
        if folder and (Path(folder) / "Ascension.exe").exists() and (Path(folder) / "Data").is_dir():
            return Path(folder).resolve()
        say("  Ascension.exe and its Data folder are not in that folder.")


def game_running():
    output = subprocess.run(["tasklist", "/FI", "IMAGENAME eq Ascension.exe"], capture_output=True, text=True).stdout
    return "ascension.exe" in output.lower()


def file_targets(root, client):
    core = root / "Core"
    targets = {FILES / "worldserver.exe": core / "worldserver.exe"}
    for dist in sorted((FILES / "configs").glob("*.conf.dist")):
        targets[dist] = core / "configs" / "modules" / dist.name
    for table in sorted((FILES / "dbc_races").glob("*.dbc")):
        targets[table] = root / "Data" / "dbc_races" / table.name
    for item in sorted(p for p in (FILES / "LiveMon").rglob("*") if p.is_file()):
        targets[item] = root / "LiveMon" / item.relative_to(FILES / "LiveMon")
    if client:
        for patch in PATCHES:
            targets[FILES / "client" / patch] = client / "Data" / patch
        for addon in ADDONS:
            for item in sorted(p for p in (FILES / "addons" / addon).rglob("*") if p.is_file()):
                targets[item] = client / "Interface" / "AddOns" / addon / item.relative_to(FILES / "addons" / addon)
    return targets


def back_up_files(targets, folder, root, client):
    for target in targets.values():
        if target.exists():
            if target.is_relative_to(root):
                copy = folder / "repack" / target.relative_to(root)
            else:
                copy = folder / "client" / target.relative_to(client)
            copy.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(target, copy)


def enable_custom_races(root):
    conf = root / "Core" / "configs" / "modules" / "coa_custom_races.conf"
    text = conf.read_text(encoding="utf-8") if conf.exists() else ""
    if re.search(r"(?m)^\s*CoACustomRaces\.Enable\s*=\s*1\s*$", text):
        return
    if re.search(r"(?m)^\s*CoACustomRaces\.Enable\s*=", text):
        text = re.sub(r"(?m)^(\s*CoACustomRaces\.Enable\s*=\s*)\d+", r"\g<1>1", text)
    else:
        text += "\n[worldserver]\nCoACustomRaces.Enable = 1\n"
    conf.parent.mkdir(parents=True, exist_ok=True)
    conf.write_text(text, encoding="utf-8")
    say("Custom races are switched on (CoACustomRaces.Enable = 1): the subclass races need them.")


def module_sql(server, log):
    for path in sorted((FILES / "sql" / "world").glob("*.sql")):
        code, error = server.run_file("acore_world", path)
        if code:
            log(f"{path.name} FAILED")
            log(error)
            return False
    log(f"World data of the subclass module is in place ({len(list((FILES / 'sql' / 'world').glob('*.sql')))} files).")
    applied, _ = server.history()
    for path in sorted((FILES / "sql" / "characters").glob("*.sql")):
        update = Update("characters", path.stem, path.name, path)
        if update.key in applied:
            continue
        code, error = server.run_file("acore_characters", path)
        if code and not ALREADY_THERE.search(error):
            log(f"{path.name} FAILED")
            log(error)
            return False
        server.query(server.entry(update, "applied"))
    return True


def install():
    root = find_repack()
    server = Server(root)
    log = Log(root / "Core" / "Logs" / "install-subclass.log")
    state_file = HERE / "installed.json"
    state = json.loads(state_file.read_text(encoding="utf-8")) if state_file.exists() else {}
    if server.world_running():
        fail("The world server is running. Stop the servers first.")
    client = find_client(state.get("client"))
    if client and game_running():
        fail("The game is running. Close Ascension.exe first.")
    targets = file_targets(root, client)
    if "--check" in sys.argv:
        for source, target in targets.items():
            say(f"{source.relative_to(HERE)} -> {target}")
        return
    server.start_mysql()
    folder = HERE / "backups" / datetime.now().strftime("%Y%m%d-%H%M%S")
    if "--no-backup" not in sys.argv:
        log(f"Backing up the databases and the files this package replaces to {folder} ...")
        server.backup(folder / "databases", log)
        back_up_files(targets, folder / "files", root, client)
    server.prepare_ledger()
    todo = plan(server, Manifest(FILES / "sql" / "core"), None, log)
    if todo and not apply(server, todo, log):
        fail("A CoA database update failed; nothing was copied. See Core\\Logs\\install-subclass.log. "
             f"The database backup is in {folder / 'databases'}.")
    if not module_sql(server, log):
        fail("A subclass database update failed; nothing was copied. See Core\\Logs\\install-subclass.log.")
    for source, target in targets.items():
        if target.name == "livemon.ini" and target.exists():
            continue
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, target)
        if target.name.endswith(".conf.dist") and not target.with_suffix("").exists():
            shutil.copy2(source, target.with_suffix(""))
    enable_custom_races(root)
    state.update({"client": str(client) if client else state.get("client"),
                  "installed": time.strftime("%Y-%m-%d %H:%M"),
                  "package": json.loads((HERE / "package.json").read_text(encoding="utf-8"))})
    state_file.write_text(json.dumps(state, indent=1), encoding="utf-8")
    say()
    say("The subclass test package is installed.")
    say("Start the servers as usual. Fully restart the game (not /reload) so it loads the new patches.")
    say("Run Install-Subclass.bat again after any CoA Manager update or CoA Custom reinstall: both replace")
    say("the worldserver and the world tables this package changes.")


if __name__ == "__main__":
    for stream in (sys.stdout, sys.stderr):
        stream.reconfigure(errors="replace")
    try:
        install()
    except (OSError, RuntimeError, ValueError, KeyError) as error:
        fail(str(error))
