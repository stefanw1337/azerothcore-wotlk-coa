import argparse
import json
import re
import shutil
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

MODELS = Path("mods/heroes.stormmod/base.stormassets/assets/units/mounts")
TEXTURES = Path("mods/heroes.stormmod/base.stormassets/assets/textures")
GAMEDATA = Path("mods/heroesdata.stormmod/base.stormdata/gamedata")
STRINGS = Path("mods/heroesdata.stormmod/enus.stormdata/localizeddata/gamestrings.txt")
FIRST_PASS = (r"mods/heroes\.stormmod/base\.stormassets/assets/units/mounts/"
              r"|mods/heroesdata\.stormmod/base\.stormdata/gamedata/[^/]*\.xml$"
              r"|mods/heroesdata\.stormmod/base\.stormdata/gamedata/mounts/"
              r"|mods/heroesdata\.stormmod/enus\.stormdata/localizeddata/gamestrings\.txt$")
PATCH = "patch-ZZZZZZ-HOTS.MPQ"
PACKAGE_FILES = ("install_hotsmounts.py", "mpqpull.py", "mkmergepatch.py")


def log(text):
    print(text, flush=True)


def run(*command):
    log("  > " + " ".join(str(c) for c in command))
    result = subprocess.run([sys.executable, *map(str, command)])
    if result.returncode:
        raise SystemExit(f"step failed: {command[0]}")


def find_dir(root, relative):
    path = root / relative
    if path.is_dir() or path.is_file():
        return path
    current = root
    for part in relative.parts:
        match = next((p for p in current.iterdir() if p.name.lower() == part.lower()), None) if current.is_dir() else None
        if match is None:
            raise SystemExit(f"{relative} not found under {root}")
        current = match
    return current


def casc(storage, dll, pattern, output):
    if output.exists():
        shutil.rmtree(output)
    run(HERE / "casc_extract.py", "--dll", dll, "--storage", storage, "--pattern", pattern, "--output", output,
        "--extract")


def needed_textures(catalog, models):
    import hots_m3
    names = set()
    for group in catalog["models"]:
        m3 = hots_m3.read(models / group["model"])
        for mat in m3["materials"]:
            for path in [mat.get("diffuse"), mat.get("emissive"), mat.get("alpha")] + list(mat.get("paths") or []):
                if path:
                    names.add(path.replace("\\", "/").split("/")[-1].lower())
        for mount in group["mounts"]:
            for swap in mount["swaps"]:
                for value in swap if isinstance(swap, (list, tuple)) else [swap]:
                    if isinstance(value, str) and value:
                        names.add(value.split("/")[-1].lower())
    return sorted(n if n.endswith(".dds") else n + ".dds" for n in names)


def extract(args):
    work = args.work.resolve()
    work.mkdir(parents=True, exist_ok=True)
    log("=== [1/4] HotS: mount models, mount data and names ===")
    casc(args.hots, args.casclib, FIRST_PASS, work / "extract-1")
    log("=== [2/4] mount list ===")
    data = work / "data"
    if data.exists():
        shutil.rmtree(data)
    (data / "gamedata").mkdir(parents=True)
    root = work / "extract-1"
    shutil.copytree(find_dir(root, GAMEDATA / "mounts"), data / "mountdata")
    for xml in find_dir(root, GAMEDATA).glob("*.xml"):
        shutil.copy2(xml, data / "gamedata" / xml.name.lower())
    shutil.copy2(find_dir(root, STRINGS), data / "gamestrings.txt")
    run(HERE / "hots_mount_catalog.py", data, work / "catalog.json")
    log("=== [3/4] m3studio (reads .m3 files) ===")
    run(HERE / "setup_m3studio.py")
    log("=== [4/4] HotS: the textures those mounts use ===")
    catalog = json.loads((work / "catalog.json").read_text())
    names = needed_textures(catalog, find_dir(root, MODELS))
    (work / "textures.txt").write_text("\n".join(names) + "\n")
    exact = "|".join(re.escape(n) for n in names)
    pattern = (r"mods/heroes\.stormmod/base\.stormassets/assets/textures/"
               rf"(storm_mount_(?!.*_(norm|spec)\.dds$)[^/]*\.dds|{exact})$")
    casc(args.hots, args.casclib, pattern, work / "extract-2")
    log(f"done: {len(catalog['models'])} models, {len(names)} textures listed; next: build")


def lower_copy(folder, target):
    if target.exists():
        shutil.rmtree(target)
    target.mkdir(parents=True)
    for path in folder.iterdir():
        if path.is_file():
            shutil.copy2(path, target / path.name.lower())


def build(args):
    work = args.work.resolve()
    import hots_m3
    hots_m3._io_m3()
    log("=== [1/3] textures under lower-case names ===")
    textures = work / "textures"
    lower_copy(find_dir(work / "extract-2", TEXTURES), textures)
    log("=== [2/3] converting every mount (takes a while) ===")
    out = work / "build"
    if out.exists():
        shutil.rmtree(out)
    run(HERE / "build_hots_mounts_all.py", work / "catalog.json", "--models", find_dir(work / "extract-1", MODELS),
        "--textures", textures, "--out", out)
    report = json.loads((out / "build-report.json").read_text())
    if report["failed"] or not report["built"]:
        names = ", ".join(f["model_id"] for f in report["failed"][:5])
        raise SystemExit(f"{len(report['failed'])} model(s) failed ({names}); see {out / 'build-report.json'}")
    log("=== [3/3] package ===")
    run(HERE / "make_package.py", out, work / "package")
    for name in PACKAGE_FILES:
        shutil.copy2(HERE / name, work / "package" / name)
    log(f"done: {work / 'package'}; next: install")


def install(args):
    package = args.work.resolve() / "package"
    command = [package / "install_hotsmounts.py", "--target", args.target, "--client", args.client,
               "--world-conf", args.world_conf, "--server-dbc", args.server_dbc, "--locale", args.locale]
    if args.mysql:
        command += ["--mysql", args.mysql]
    if args.patch_name:
        command += ["--patch-name", args.patch_name]
    run(*command)


def main():
    parser = argparse.ArgumentParser(description="Heroes of the Storm mounts for WoW 3.3.5a, from your own HotS install")
    sub = parser.add_subparsers(dest="step", required=True)
    one = sub.add_parser("extract")
    one.add_argument("--hots", type=Path, required=True)
    one.add_argument("--casclib", type=Path, required=True)
    one.add_argument("--work", type=Path, required=True)
    two = sub.add_parser("build")
    two.add_argument("--work", type=Path, required=True)
    three = sub.add_parser("install")
    three.add_argument("--work", type=Path, required=True)
    three.add_argument("--target", choices=("coa", "335"), required=True)
    three.add_argument("--client", type=Path, required=True)
    three.add_argument("--world-conf", type=Path, required=True)
    three.add_argument("--server-dbc", type=Path, required=True)
    three.add_argument("--mysql", type=Path)
    three.add_argument("--patch-name")
    three.add_argument("--locale", default="enUS")
    args = parser.parse_args()
    {"extract": extract, "build": build, "install": install}[args.step](args)


if __name__ == "__main__":
    main()
