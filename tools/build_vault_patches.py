import argparse
import shutil
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
for folder in (HERE, HERE.parent / "modules" / "mod-subclass" / "tools"):
    if str(folder) not in sys.path:
        sys.path.append(str(folder))

import patch_handspring_client as handspring  # noqa: E402
from mkmergepatch import LISTFILE, merge, verified, written  # noqa: E402
from mpqpull import Archive, patch_order  # noqa: E402

HD_ARCHIVES = frozenset(("patch-4", "patch-5", "patch-x", "patch-u", "patch-z", "patch-q", "patch-r", "patch-v"))
MERGED, HD_PATCH = "patch-ZZZZY.MPQ", "patch-ZZZZZ-HD.MPQ"
OWN_ARCHIVES = frozenset(("patch-zzzzy", "patch-zzzzv", "patch-zzzzz", "patch-zzzzz-hd"))
LOCALE_FOLDERS = ("enUS", "enGB")
RACES = ("BloodElf", "Draenei", "Dwarf", "Gnome", "Human", "NightElf", "Orc", "Scourge", "Tauren", "Troll")
SEXES = ("Male", "Female")
TABLES = {"DBFilesClient\\AnimationData.dbc": handspring.add_animation_rows,
          "DBFilesClient\\SpellVisualKit.dbc": handspring.add_visual_kits}


def model_names():
    return [f"Character\\{race}\\{sex}\\{race}{sex}.m2" for race in RACES for sex in SEXES]


def ordered_archives(data):
    root = sorted((path for path in data.iterdir() if path.suffix.lower() == ".mpq"), key=patch_order)
    locales = []
    for locale in LOCALE_FOLDERS:
        folder = data / locale
        if folder.is_dir():
            locales += sorted((path for path in folder.iterdir() if path.suffix.lower() == ".mpq"),
                              key=lambda path: path.stem.lower())
    base = [path for path in root if patch_order(path)[0] == 0]
    return base + locales + [path for path in root if patch_order(path)[0] != 0]


def original_archives(archives):
    return [path for path in archives if path.stem.lower() not in OWN_ARCHIVES]


def opens_without_hd(path):
    stem = path.stem.lower()
    return stem not in HD_ARCHIVES and not stem.endswith("-hd")


def standard_archives(archives):
    return [path for path in original_archives(archives) if opens_without_hd(path)]


class Client:
    def __init__(self, archives):
        self.archives = archives
        self.opened = {}

    def archive(self, path):
        if path not in self.opened:
            self.opened[path] = Archive(str(path))
        return self.opened[path]

    def close(self):
        for archive in self.opened.values():
            archive.close()
        self.opened.clear()

    def newest(self, name, among):
        for path in reversed(among):
            data = self.archive(path).extract(name)
            if data is not None:
                return data, path
        return None, None


def skin_name(model):
    return model[:-len(".m2")] + "00.skin"


def vaulted_models(client, among, kind):
    models, sources = {}, {}
    for number, name in enumerate(model_names(), 1):
        print(f"{kind} {number}/{len(model_names())}: {name}", flush=True)
        data, source = client.newest(name, among)
        if data is None:
            continue
        skin, _ = client.newest(skin_name(name), among)
        stem = name.rsplit("\\", 1)[1][:-len(".m2")]
        models[name] = handspring.add_handsprings(data, handspring.style_for(stem), skin)
        sources[name] = source
    return models, sources


def vault_tables(client):
    tables = {}
    for name, add_rows in TABLES.items():
        data, _ = client.newest(name, client.archives)
        if data is None:
            raise SystemExit(f"no archive in Data has {name}")
        try:
            tables[name] = add_rows(data)
        except ValueError:
            continue
    return tables


def listed(files):
    listing = "\r\n".join(sorted(files)).encode("latin1")
    return sorted(list(files.items()) + [(LISTFILE, listing)], key=lambda entry: entry[0].lower())


def write_archive(path, entries):
    staged = path.with_name(path.name + ".building")
    try:
        staged.write_bytes(written(entries))
        verified(staged, entries)
        staged.replace(path)
    finally:
        staged.unlink(missing_ok=True)


def require_writable(paths):
    for path in paths:
        if not path.exists():
            continue
        probe = path.with_name(path.name + ".checking")
        try:
            path.replace(probe)
            probe.replace(path)
        except PermissionError:
            raise SystemExit(f"{path.name} is in use by another program (the game, MPQ-DupeChecker or a WoW tool); "
                             "close it and run this again")


def merge_into_merged(data, additions, backup):
    merged = data / MERGED
    addition = data / (MERGED + ".adding")
    write_archive(addition, listed(additions))
    inputs = [(addition, False)]
    if merged.exists():
        if not backup.exists():
            shutil.copy2(merged, backup)
        inputs.insert(0, (merged, False))
    try:
        entries = merge(inputs, set())
        write_archive(merged, entries)
    finally:
        addition.unlink(missing_ok=True)


def build(data, backup_folder):
    require_writable([data / MERGED, data / HD_PATCH])
    archives = ordered_archives(data)
    client = Client(archives)
    standard, standard_sources = vaulted_models(client, standard_archives(archives), "standard")
    hd, hd_sources = vaulted_models(client, original_archives(archives), "HD")
    hd = {name: model for name, model in hd.items() if hd_sources[name] != standard_sources.get(name)}
    additions = {**standard, **vault_tables(client)}
    client.close()
    print(f"writing {MERGED} and {HD_PATCH}", flush=True)
    merge_into_merged(data, additions, backup_folder / (MERGED + ".before-vault"))
    hd_path = data / HD_PATCH
    if hd:
        write_archive(hd_path, listed(hd))
    elif hd_path.exists():
        hd_path.unlink()
    return standard_sources, hd_sources, hd


def main():
    parser = argparse.ArgumentParser(
        description="Build the Vault for the original races from this client's own models: standard models into "
                    "patch-ZZZZY.MPQ and HD models into patch-ZZZZZ-HD.MPQ")
    parser.add_argument("data", type=Path, help="the client's Data folder")
    parser.add_argument("--backup", type=Path, help="folder for the patch-ZZZZY.MPQ backup (default: Data)")
    args = parser.parse_args()
    standard_sources, hd_sources, hd = build(args.data, args.backup or args.data)
    for name in model_names():
        line = f"{name}: standard from {standard_sources[name].name}" if name in standard_sources else f"{name}: absent"
        if name in hd:
            line += f", HD from {hd_sources[name].name}"
        print(line)
    print(f"{MERGED} and {HD_PATCH if hd else 'no HD patch (no HD archives found)'} written and read back intact")


if __name__ == "__main__":
    main()
