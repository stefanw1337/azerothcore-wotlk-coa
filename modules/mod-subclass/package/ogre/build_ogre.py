"""Builds the Two-Players male Two-Headed Ogre on the player's own PC and installs it as patch-ZZZZZY-Ogre.MPQ.
    build_ogre.py --client <game folder> [--hots <Heroes of the Storm folder>]
His body comes from the player's Heroes of the Storm install and his skeleton from World of Warcraft (Blizzard's CDN),
so no Blizzard file ships with the package."""
import shutil
import subprocess
import sys
import zipfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
TOOLKIT = HERE / "toolkit"
PYDEPS = HERE / "pydeps"
WORK = HERE / "work"
EXTRACTOR = HERE / "CascExtractor.exe"
WOW_LISTFILE = HERE / "chogall-listfile.csv"
HOTS_FILES = ("storm_hero_chogall_base.m3", "storm_hero_chogall_requiredanims.m3a", "storm_hero_chogall_base_diff.dds")
HOTS_GUESSES = (r"C:\Program Files (x86)\Heroes of the Storm", r"C:\Program Files\Heroes of the Storm",
                r"D:\Heroes of the Storm", r"E:\Heroes of the Storm", r"G:\Heroes of the Storm")
PATCH = "patch-ZZZZZY-Ogre.MPQ"


def say(text=""):
    print(text, flush=True)


def option(name):
    return sys.argv[sys.argv.index(name) + 1] if name in sys.argv[:-1] else None


def is_hots(folder):
    return bool(folder) and (Path(folder) / ".build.info").exists()


def find_hots():
    given = option("--hots")
    if given:
        return Path(given) if is_hots(given) else None
    default = next((g for g in HOTS_GUESSES if is_hots(g)), None)
    while True:
        prompt = 'Heroes of the Storm folder (the one with .build.info), or "skip"'
        answer = input(prompt + (f" [{default}]" if default else "") + ": ").strip().strip('"') or default
        if not answer or answer.lower() in ("skip", "no", "none"):
            return None
        if is_hots(answer):
            return Path(answer)
        say("  .build.info is not in that folder.")


def python_packages():
    for wheel in sorted((HERE / "wheels").glob("*.whl")):
        marker = PYDEPS / (wheel.name + ".done")
        if not marker.exists():
            say(f"  unpacking {wheel.name}")
            zipfile.ZipFile(wheel).extractall(PYDEPS)
            marker.write_text("ok", encoding="utf-8")
    for folder in (str(PYDEPS), str(TOOLKIT / "scripts" / "two_headed_ogre"), str(TOOLKIT / "scripts"),
                   str(HERE.parent / "tools")):
        if folder not in sys.path:
            sys.path.insert(0, folder)


def extract(source, filter_text, out, listfile=None):
    command = [str(EXTRACTOR), "--extract", "--source", str(source), "--filter", filter_text, "--out", str(out)]
    if listfile:
        command += ["--listfile", str(listfile)]
    if subprocess.run(command).returncode != 0:
        raise RuntimeError(f"CascExtractor could not extract {filter_text} from {source}")


def gathered(folder, names, target):
    target.mkdir(parents=True, exist_ok=True)
    for name in names:
        found = next(folder.rglob(name), None)
        if not found:
            raise RuntimeError(f"{name} was not extracted")
        shutil.copy2(found, target / name)
    return target


def patch_entries(out):
    entries = []
    for path in sorted(p for p in (out / "Creature").rglob("*") if p.is_file()):
        entries.append(("\\".join(path.relative_to(out).parts), path.read_bytes()))
    listing = "\r\n".join(name for name, _ in entries).encode("latin1")
    return sorted(entries + [("(listfile)", listing)], key=lambda entry: entry[0].lower())


def build(client):
    hots = find_hots()
    if not hots:
        say("Skipped: without Heroes of the Storm the Two-Players male Ogre has no body. Run this again any time.")
        return False
    if WORK.exists():
        shutil.rmtree(WORK)
    say("[1/4] Python packages for the model builder")
    python_packages()
    say(f"[2/4] Cho'Gall's body from {hots}")
    extract(hots, ";".join(HOTS_FILES), WORK / "hots-raw")
    hots_files = gathered(WORK / "hots-raw", HOTS_FILES, WORK / "hots")
    say("[3/4] Cho'Gall's skeleton and animations from World of Warcraft (Blizzard's CDN, about 2 MB)")
    extract("online:wow:eu", "creature/chogall/chogall", WORK / "wow-raw", WOW_LISTFILE)
    wow_files = WORK / "wow-raw" / "creature" / "chogall"
    say("[4/4] Building the model (a few minutes)")
    import build as ogre_builder
    from mkmergepatch import verified, written
    ogre_builder.main(WORK / "out", hots_files, wow_files)
    entries = patch_entries(WORK / "out")
    target = Path(client) / "Data" / PATCH
    target.write_bytes(written(entries))
    verified(target, entries)
    shutil.rmtree(WORK)
    say(f"Installed {target} ({len(entries) - 1} files). Fully restart the game.")
    return True


def installed_client():
    state = HERE.parents[1] / "installed.json"
    if state.exists():
        import json
        return json.loads(state.read_text(encoding="utf-8")).get("client")
    return None


def main():
    client = option("--client") or installed_client()
    if not client or not (Path(client) / "Data").is_dir():
        sys.exit("Run Install-Subclass.bat first, or pass --client <game folder>.")
    try:
        build(client)
    except (OSError, RuntimeError, ValueError) as error:
        sys.exit(f"ERROR: {error}")


if __name__ == "__main__":
    main()
