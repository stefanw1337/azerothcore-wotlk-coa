"""Zip a folder with forward slashes in every entry name, as the zip format requires.
    python mkzip.py <folder> <out.zip>
The folder itself becomes the top entry of the zip. Windows PowerShell's Compress-Archive writes backslashes,
which several unzip tools take for part of the file name."""
import sys
import zipfile
from pathlib import Path


def main(folder, out):
    folder, out = Path(folder).resolve(), Path(out)
    if out.exists():
        out.unlink()
    files = sorted(p for p in folder.rglob("*") if p.is_file())
    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED, compresslevel=6) as archive:
        for path in files:
            archive.write(path, (Path(folder.name) / path.relative_to(folder)).as_posix())
    with zipfile.ZipFile(out) as archive:
        bad = [name for name in archive.namelist() if "\\" in name]
        broken = archive.testzip()
    if bad or broken:
        sys.exit(f"{out} is not right: {bad[:3] or broken}")
    print(f"{out}: {len(files)} files, {out.stat().st_size / 1e6:.1f} MB, every entry checked")


if __name__ == "__main__":
    main(*sys.argv[1:3])
