"""Lists every file in the client's MPQs whose name contains one of the given words, and copies the newest copy of
the spell visual tables (SpellVisual, SpellVisualKit, SpellVisualEffectName, SpellVisualKitModelAttach) into a zip,
so a visual can be found by the model it shows. Nothing in the client is changed.
    python mpqnames.py <client Data folder> <out.zip> <word> [<word> ...]"""
import sys
import zipfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from mpqpull import Archive, archives

TABLES = ("DBFilesClient\\SpellVisual.dbc", "DBFilesClient\\SpellVisualKit.dbc",
          "DBFilesClient\\SpellVisualEffectName.dbc", "DBFilesClient\\SpellVisualKitModelAttach.dbc")


def main(data_folder, zip_path, *words):
    wanted = [word.lower() for word in words]
    lines, tables = [], {}
    for path in archives(Path(data_folder)):
        try:
            archive = Archive(path)
            listing = archive.extract("(listfile)")
        except Exception as error:
            print(f"  {path.name}: could not be read ({error}), skipped")
            continue
        names = [name.strip() for name in (listing or b"").decode("latin1", "replace").splitlines() if name.strip()]
        hits = [name for name in names if any(word in name.lower() for word in wanted)]
        lines += [f"{path.name}\t{name}" for name in hits]
        for table in TABLES:
            if table not in tables:
                try:
                    data = archive.extract(table)
                except Exception:
                    data = None
                if data:
                    tables[table] = (path.name, data)
        print(f"  {path.name}: {len(names)} files, {len(hits)} match")
    with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_DEFLATED) as out:
        out.writestr("names.txt", "\r\n".join(lines))
        for table, (source, data) in tables.items():
            out.writestr(table.replace("\\", "/"), data)
            out.writestr("_from/" + table.replace("\\", "/") + ".txt", source)
    print(f"Done: {len(lines)} matching files and {len(tables)} visual tables in {zip_path}")


if __name__ == "__main__":
    if len(sys.argv) < 4:
        raise SystemExit(__doc__)
    main(*sys.argv[1:])
