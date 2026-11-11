"""Copy a client patch for a public release, leaving out files that must stay local.
    python mkreleasepatch.py <in.MPQ> <out.MPQ> <left-out prefix> [<left-out prefix> ...]
A prefix is a path inside the archive, compared without case and with either slash. Every file is checked after
writing, and the copy stops if nothing matched a prefix, so a renamed local file cannot slip through unnoticed."""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from mkmergepatch import LISTFILE, merge, verified, written  # noqa: E402


def normalized(path):
    return path.replace("/", "\\").lower()


def kept(entries, prefixes):
    keep, dropped = [], {prefix: [] for prefix in prefixes}
    for name, data in entries:
        if name == LISTFILE:
            continue
        match = next((prefix for prefix in prefixes if normalized(name).startswith(prefix)), None)
        if match:
            dropped[match].append(name)
        else:
            keep.append((name, data))
    return keep, dropped


def main(source, target, *prefixes):
    if not prefixes:
        raise SystemExit("name at least one prefix to leave out")
    prefixes = [normalized(prefix) for prefix in prefixes]
    keep, dropped = kept(merge([(Path(source), False)], set()), prefixes)
    for prefix, names in dropped.items():
        if not names:
            raise SystemExit(f"nothing in {source} starts with {prefix}: check the prefix")
        print(f"left out {len(names)} files under {prefix}")
    listing = "\r\n".join(sorted(name for name, _ in keep)).encode("latin1")
    entries = sorted(keep + [(LISTFILE, listing)], key=lambda entry: entry[0].lower())
    output = Path(target)
    output.write_bytes(written(entries))
    verified(output, entries)
    print(f"{output}: {len(entries)} files, {output.stat().st_size} bytes, every file read back intact")


if __name__ == "__main__":
    main(*sys.argv[1:])
