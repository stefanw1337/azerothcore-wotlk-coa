"""Searches every .lua, .xml and .toc file in the client's MPQs (newest patch first) for a text, and copies each file
that contains it into a zip, and the same for the loose files in the client's Interface folder (addons). Nothing in the client is changed.
Several texts separated by | find files that contain any of them.
    python mpqgrep.py <client Data folder> <text>[|<text>...] <out.zip>"""
import sys
import zipfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from mpqpull import Archive, archives

SEARCHED = (".lua", ".xml", ".toc")


def main(data_folder, text, zip_path):
    texts = [part.encode("latin1").lower() for part in text.split("|") if part]
    seen, found = set(), 0
    folders = [Path(data_folder), Path(data_folder) / "enUS"]
    with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_DEFLATED) as out:
        for folder in folders:
            for path in archives(folder):
                try:
                    archive = Archive(path)
                    listing = archive.extract("(listfile)")
                except Exception as error:
                    print(f"  {path.name}: could not be read ({error}), skipped")
                    continue
                if not listing:
                    print(f"  {path.name}: no file list, skipped")
                    continue
                names = [name.strip() for name in listing.decode("latin1", "replace").splitlines()]
                names = [name for name in names if name.lower().endswith(SEARCHED) and name.lower() not in seen]
                hits = unreadable = 0
                for name in names:
                    try:
                        data = archive.extract(name)
                    except Exception:  # broken or protected entries in some CoA archives
                        unreadable += 1
                        continue
                    if data is None:
                        continue
                    seen.add(name.lower())
                    lowered = data.lower()
                    if any(wanted in lowered for wanted in texts):
                        out.writestr(name.replace("\\", "/"), data)
                        out.writestr("_from/" + name.replace("\\", "/") + ".txt", path.name)
                        hits += 1
                        found += 1
                skipped = f", {unreadable} unreadable skipped" if unreadable else ""
                print(f"  {path.name}: {len(names)} files searched, {hits} contain it{skipped}")
        # Addons installed in the client folder (Interface\AddOns) are files on disk, not in the MPQs.
        loose = Path(data_folder).parent / "Interface"
        hits = 0
        for file in sorted(loose.rglob("*")) if loose.is_dir() else []:
            try:
                match = file.suffix.lower() in SEARCHED and file.is_file() and any(
                    wanted in file.read_bytes().lower() for wanted in texts)
            except OSError:
                continue
            if match:
                name = "loose/" + file.relative_to(loose.parent).as_posix()
                out.write(file, name)
                hits += 1
                found += 1
        print(f"  {loose}: {hits} files contain it")
    print(f"Done: {found} files in {zip_path}")


if __name__ == "__main__":
    if len(sys.argv) != 4:
        raise SystemExit(__doc__)
    main(*sys.argv[1:])
