"""Installs the subclass package on a repack that already runs CoA Custom.
    install.py [--client <Ascension folder>] [--no-client] [--no-backup] [--check] [--any-races-version] [--no-ogre]
Run through Install-Subclass.bat, which uses the repack's own Python. The CoA-Subclass folder belongs in the repack
folder, next to CoA-Custom and Scripts."""
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

from apply_coa_sql import Log, Manifest, Server, Update, apply, plan  # noqa: E402

PACKAGE = json.loads((HERE / "package.json").read_text(encoding="utf-8"))
ADDONS = ("Ascension_Subclass", "CoAC_BotPlan", "SpellActivationOverlay-for-CoA")
GAMES = ("Ascension.exe", "Ascension64.exe", "CoA64.exe")
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
    for needed in ("Scripts/manage.py", "mysql/admin-client.ini", "Runtime/python/python.exe"):
        if not (root / needed).exists():
            fail(f"{root / needed} is missing. Extract CoA-Subclass into the repack folder (next to Scripts).")
    return root


def server_core(root):
    for core in (root / "CoA-Bots" / "Core", root / "Core"):
        if (core / "worldserver.exe").exists():
            return core
    fail("No worldserver.exe in CoA-Bots\\Core or Core. Is CoA Custom installed in this repack?")


def races_pack(root):
    state_file = root / "CoA-Custom" / "installed.json"
    state = json.loads(state_file.read_text(encoding="utf-8")) if state_file.exists() else {}
    wanted = PACKAGE["coaCustom"]
    found = state.get("version")
    if found != wanted and "--any-races-version" not in sys.argv:
        fail(f"This package needs CoA Custom {wanted}, installed with CoA-Custom\\Install-Custom.bat first. "
             f"This repack has {found or 'no CoA Custom install'}. Install CoA Custom {wanted}, or wait for the "
             f"subclass package made for your version.")
    return state


def is_game_folder(folder):
    return folder and Path(folder, "Data").is_dir() and any(Path(folder, game).exists() for game in GAMES)


def find_client(previous):
    if "--no-client" in sys.argv:
        return None
    guesses = [option("--client"), previous, r"C:\Ascension Local",
               r"C:\Program Files\Ascension Launcher\resources\client", r"C:\Ascension\resources\client"]
    default = next((g for g in guesses if is_game_folder(g)), None)
    while True:
        folder = ask('Game folder (the one with Ascension.exe), or "skip" if the game runs on another PC', default)
        if folder and folder.lower() in ("skip", "no", "none"):
            return None
        if is_game_folder(folder):
            return Path(folder).resolve()
        say("  Ascension.exe and its Data folder are not in that folder.")


def game_running():
    output = subprocess.run(["tasklist"], capture_output=True, text=True).stdout.lower()
    return any(game.lower() in output for game in GAMES)


def tree(source, target):
    return {item: target / item.relative_to(source) for item in sorted(source.rglob("*")) if item.is_file()}


def file_targets(root, core, client):
    targets = {FILES / "worldserver.exe": core / "worldserver.exe"}
    for dist in sorted((FILES / "configs").glob("*.conf.dist")):
        targets[dist] = core / "configs" / "modules" / dist.name
    for folder in ("dbc_races", "dbc_clientset"):
        targets.update(tree(FILES / folder, root / "Data" / folder))
    targets.update(tree(FILES / "LiveMon", root / "LiveMon"))
    if client:
        for patch in sorted((FILES / "client").glob("*.MPQ")):
            targets[patch] = client / "Data" / patch.name
        targets.update(tree(FILES / "client_root", client))
        for addon in ADDONS:
            targets.update(tree(FILES / "addons" / addon, client / "Interface" / "AddOns" / addon))
    return {source: target for source, target in targets.items() if source.exists()}


def back_up_files(targets, folder, root, client):
    for target in targets.values():
        if target.exists():
            if target.is_relative_to(root):
                copy = folder / "repack" / target.relative_to(root)
            else:
                copy = folder / "client" / target.relative_to(client)
            copy.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(target, copy)


def build_ogre(client):
    builder = FILES / "ogre" / "build_ogre.py"
    if not client or "--no-ogre" in sys.argv or not builder.exists():
        return
    say()
    say("The Two-Players male Two-Headed Ogre is built on this PC from your own Heroes of the Storm install and")
    say("World of Warcraft's Cho'Gall (downloaded from Blizzard). Without it, his body stays invisible.")
    if not (ask("Build him now? (y/n)", "y") or "y").lower().startswith("y"):
        say("Skipped. Run Install-Subclass.bat again, or files\\ogre\\Build-Ogre.bat, any time.")
        return
    subprocess.run([sys.executable, "-B", str(builder), "--client", str(client)])


def enable_custom_races(core):
    conf = core / "configs" / "modules" / "coa_custom_races.conf"
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
    world = sorted((FILES / "sql" / "world").glob("*.sql"))
    for path in [p for p in world if not p.name.startswith("rev_")] + [p for p in world if p.name.startswith("rev_")]:
        code, error = server.run_file("acore_world", path)
        if code:
            log(f"{path.name} FAILED")
            log(error)
            return False
    log(f"World data of the subclass module is in place ({len(world)} files).")
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
    core = server_core(root)
    races = races_pack(root)
    server = Server(root)
    log_path = core / "Logs" / "install-subclass.log"
    log = Log(log_path)
    state_file = HERE / "installed.json"
    state = json.loads(state_file.read_text(encoding="utf-8")) if state_file.exists() else {}
    if server.world_running():
        fail("The world server is running. Stop the servers first.")
    client = find_client(state.get("client") or races.get("client"))
    if client and game_running():
        fail("The game is running. Close it first.")
    targets = file_targets(root, core, client)
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
        fail(f"A CoA database update failed; nothing was copied. See {log_path}. "
             f"The database backup is in {folder / 'databases'}.")
    if not module_sql(server, log):
        fail(f"A subclass database update failed; nothing was copied. See {log_path}.")
    for source, target in targets.items():
        if target.name == "livemon.ini" and target.exists():
            continue
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, target)
        if target.name.endswith(".conf.dist") and not target.with_suffix("").exists():
            shutil.copy2(source, target.with_suffix(""))
    enable_custom_races(core)
    build_ogre(client)
    state.update({"client": str(client) if client else state.get("client"),
                  "installed": time.strftime("%Y-%m-%d %H:%M"),
                  "package": PACKAGE})
    state_file.write_text(json.dumps(state, indent=1), encoding="utf-8")
    say()
    say(f"The subclass package {PACKAGE.get('version', '')} is installed on CoA Custom {PACKAGE['coaCustom']}.")
    say("Start the servers as usual. Fully restart the game (not /reload) so it loads the new patches.")
    say("Run Install-Subclass.bat again after a CoA Custom reinstall: it replaces the worldserver and the world")
    say("tables this package changes. A newer CoA Custom needs the subclass package made for it.")


if __name__ == "__main__":
    for stream in (sys.stdout, sys.stderr):
        stream.reconfigure(errors="replace")
    try:
        install()
    except (OSError, RuntimeError, ValueError, KeyError) as error:
        fail(str(error))
