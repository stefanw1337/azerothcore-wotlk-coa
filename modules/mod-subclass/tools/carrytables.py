"""Carry the subclass's client table rows onto the client's own newest tables and merge them into patch-ZZZZY.MPQ.
    python carrytables.py <Data folder> <archive with our tables> [<backup folder>]
Spell.dbc, SkillLineAbility.dbc and SkillRaceClassInfo.dbc are taken from the archives that load before
patch-ZZZZY.MPQ. Rows only our archive has are added, and fields where our row differs are set to ours, so the
release's other changes stay."""
import struct
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
for folder in (HERE, HERE.parents[2] / "tools" if len(HERE.parents) > 2 else HERE):
    if str(folder) not in sys.path:
        sys.path.append(str(folder))

import build_vault_patches as patches  # noqa: E402
from mpqpull import Archive  # noqa: E402

SPELL_TEXTS = frozenset(index for start in (136, 153, 170, 187) for index in range(start, start + 16))
TABLES = {"DBFilesClient\\Spell.dbc": SPELL_TEXTS,
          "DBFilesClient\\SkillLineAbility.dbc": frozenset(),
          "DBFilesClient\\SkillRaceClassInfo.dbc": frozenset()}


def parsed(dbc):
    magic, count, fields, size, text_size = struct.unpack_from("<4s4I", dbc, 0)
    if magic != b"WDBC":
        raise ValueError("not a DBC table")
    rows = [list(struct.unpack_from(f"<{fields}I", dbc, 20 + index * size)) for index in range(count)]
    return rows, fields, size, bytes(dbc[20 + count * size:20 + count * size + text_size])


def text_at(texts, offset):
    end = texts.find(b"\0", offset) if offset < len(texts) else -1
    return texts[offset:end] if end >= 0 else b""


def carried(base, ours, text_fields):
    rows, fields, size, texts = parsed(base)
    our_rows, our_fields, _, our_texts = parsed(ours)
    if our_fields != fields:
        raise ValueError(f"our table has {our_fields} fields, the client's {fields}")
    texts = bytearray(texts)
    known = {}

    def placed(value):
        if not value:
            return 0
        if value not in known:
            known[value] = len(texts)
            texts.extend(value + b"\0")
        return known[value]

    by_id = {row[0]: row for row in rows}
    added = changed = 0
    for row in our_rows:
        target = by_id.get(row[0])
        existed = target is not None
        if not existed:
            target = [0] * fields
            target[0] = row[0]
            rows.append(target)
            by_id[row[0]] = target
            added += 1
        touched = False
        for index in range(1, fields):
            if index in text_fields:
                value = text_at(our_texts, row[index])
                if value != text_at(texts, target[index]):
                    target[index] = placed(value)
                    touched = True
            elif row[index] != target[index]:
                target[index] = row[index]
                touched = True
        changed += existed and touched
    body = b"".join(struct.pack(f"<{fields}I", *row) for row in rows)
    table = struct.pack("<4s4I", b"WDBC", len(rows), fields, size, len(texts)) + body + bytes(texts)
    return table, added, changed


def carry(data, source, backup_folder):
    patches.require_writable([data / patches.MERGED])
    ours = Archive(str(source))
    client = patches.Client(patches.ordered_archives(data))
    below = patches.below_merged(client.archives)
    additions = {}
    for name, text_fields in TABLES.items():
        our_table = ours.extract(name)
        if our_table is None:
            print(f"{name}: not in {source.name}, left as the client has it", flush=True)
            continue
        base, origin = client.newest(name, below)
        if base is None:
            raise SystemExit(f"no archive in Data has {name}")
        additions[name], added, changed = carried(base, our_table, text_fields)
        print(f"{name}: {origin.name} + {added} of our rows, {changed} rows changed to ours", flush=True)
    client.close()
    ours.close()
    if additions:
        print(f"writing {patches.MERGED}", flush=True)
        patches.merge_into_merged(data, additions, backup_folder / (patches.MERGED + ".before-tables"))
    print("done: our client tables are in patch-ZZZZY.MPQ", flush=True)


def main(arguments):
    data, source = Path(arguments[0]), Path(arguments[1])
    carry(data, source, Path(arguments[2]) if len(arguments) > 2 else data)


if __name__ == "__main__":
    main(sys.argv[1:])
