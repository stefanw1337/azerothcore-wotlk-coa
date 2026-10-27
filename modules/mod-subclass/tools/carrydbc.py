"""Carry our rows of client or server DBC tables onto a newer base table.
    python carrydbc.py <dbd folder> <old base dbc> <ours dbc> <new base dbc> <output dbc>
Rows we added or changed against the old base are written over the new base. Strings are compared and rewritten by
value, so their offsets may differ freely. Added rows whose id the new base took get fresh ids in tables nothing
refers to by id; elsewhere such a clash is reported and ours is kept."""
import struct
import sys
from pathlib import Path

BUILD = (3, 3, 5, 12340)
LOCALES = 16
FORMATS = {1: "B", 2: "H", 4: "I"}
RENUMBERED = {"CharSections", "EmotesTextSound", "HDEmotesTextSound", "NameGen", "CharHairGeosets",
              "CharStartOutfit", "UICameraAppearanceChrRaces"}


def version(text):
    return tuple(int(part) for part in text.strip().split("."))


def covers(build_line):
    for item in build_line[len("BUILD "):].split(","):
        item = item.strip()
        if "-" in item:
            low, high = item.split("-")
            if version(low) <= BUILD <= version(high):
                return True
        elif item and version(item) == BUILD:
            return True
    return False


def layout(dbd_path):
    lines = Path(dbd_path).read_text(encoding="utf-8").splitlines()
    types = {}
    index = 1
    while index < len(lines) and lines[index].strip():
        parts = lines[index].split("//")[0].split()
        if len(parts) >= 2:
            types[parts[1].rstrip("?")] = parts[0].split("<")[0]
        index += 1
    blocks, block = [], []
    for line in lines[index:] + [""]:
        if line.strip():
            block.append(line.strip())
        elif block:
            blocks.append(block)
            block = []
    for block in blocks:
        if not any(covers(line) for line in block if line.startswith("BUILD ")):
            continue
        strings, widths, id_slot = set(), [], None
        for line in block:
            if line.startswith(("BUILD ", "LAYOUT ", "COMMENT ")):
                continue
            name = line.split("//")[0].strip()
            if name.startswith("$"):
                annotations, name = name[1:].split("$", 1)
                if "id" in annotations.split(","):
                    id_slot = len(widths)
            count = 1
            if "[" in name:
                name, rest = name.split("[", 1)
                count = int(rest.split("]")[0])
            width = 4
            if "<" in name:
                name, rest = name.split("<", 1)
                width = int(rest.split(">")[0].lstrip("u")) // 8
            kind = types.get(name, "int")
            for _ in range(count):
                if kind == "locstring":
                    strings.update(range(len(widths), len(widths) + LOCALES))
                    widths += [4] * (LOCALES + 1)
                elif kind == "string":
                    strings.add(len(widths))
                    widths.append(4)
                else:
                    widths.append(width)
        return strings, widths, id_slot
    raise ValueError(f"{dbd_path} has no layout for 3.3.5.12340")


def header(path):
    with open(path, "rb") as table:
        return struct.unpack_from("<4I", table.read(20), 4)


class Table:
    def __init__(self, path, strings, widths, id_slot):
        data = Path(path).read_bytes()
        if data[:4] != b"WDBC":
            raise ValueError(f"{path} is not a WDBC table")
        count, fields, size, string_size = struct.unpack_from("<4I", data, 4)
        if size == fields * 4:
            widths = [4] * fields
        elif len(widths) != fields or sum(widths) != size:
            raise ValueError(f"{path}: record size {size} does not match the definition of {fields} fields")
        self.fields, self.size = fields, size
        self.offsets = [sum(widths[:slot]) for slot in range(fields)]
        self.string_slots = sorted(slot for slot in strings if slot < fields)
        self.id_format = "<" + FORMATS[widths[id_slot]] if id_slot is not None else None
        self.id_offset = self.offsets[id_slot] if id_slot is not None else None
        block = data[20 + count * size:20 + count * size + string_size]
        self.rows = []
        for row in range(count):
            record = bytearray(data[20 + row * size:20 + (row + 1) * size])
            texts = []
            for slot in self.string_slots:
                offset = struct.unpack_from("<I", record, self.offsets[slot])[0]
                texts.append(block[offset:block.index(b"\0", offset)] if offset < len(block) else b"")
                struct.pack_into("<I", record, self.offsets[slot], 0)
            self.rows.append((bytes(record), tuple(texts)))

    def key(self, row):
        return struct.unpack_from(self.id_format, row[0], self.id_offset)[0] if self.id_format else row

    def keyed(self):
        return {self.key(row): row for row in self.rows}

    def with_id(self, row, new_id):
        record = bytearray(row[0])
        struct.pack_into(self.id_format, record, self.id_offset, new_id)
        return bytes(record), row[1]

    def write(self, path, rows):
        block = bytearray(b"\0")
        offsets = {b"": 0}
        packed = bytearray()
        for record, texts in rows:
            record = bytearray(record)
            for slot, text in zip(self.string_slots, texts):
                if text not in offsets:
                    offsets[text] = len(block)
                    block += text + b"\0"
                struct.pack_into("<I", record, self.offsets[slot], offsets[text])
            packed += record
        head = struct.pack("<4s4I", b"WDBC", len(rows), self.fields, self.size, len(block))
        Path(path).write_bytes(head + packed + bytes(block))


def carry(dbd_folder, old_path, ours_path, new_path, out_path):
    name = Path(new_path).stem
    definition = Path(dbd_folder) / f"{name}.dbd"
    if definition.exists():
        strings, widths, id_slot = layout(definition)
    else:
        _, fields, _, string_size = header(new_path)
        if string_size > 1:
            raise ValueError(f"{name} has strings but no definition in {dbd_folder}")
        strings, widths, id_slot = set(), [4] * fields, 0
    old, ours, new = (Table(path, strings, widths, id_slot) for path in (old_path, ours_path, new_path))
    if not old.fields == ours.fields == new.fields:
        raise ValueError(f"{name}: field counts differ: old {old.fields}, ours {ours.fields}, new {new.fields}")
    old_rows, our_rows, new_rows = old.keyed(), ours.keyed(), new.keyed()
    changed = {item: row for item, row in our_rows.items() if old_rows.get(item) != row}
    taken = [item for item in changed if item not in old_rows and item in new_rows and new_rows[item] != changed[item]]
    if taken and new.id_format and name in RENUMBERED:
        free = max(max(new_rows), max(our_rows)) + 1
        for item in taken:
            changed[free] = new.with_id(changed.pop(item), free)
            free += 1
        print(f"{name}: {len(taken)} added rows took ids the new base uses; they now start at {free - len(taken)}")
    removed = [item for item in old_rows if item not in our_rows]
    clashes = [item for item in changed if item in new_rows and new_rows[item] != old_rows.get(item)
               and new_rows[item] != changed[item]]
    merged = dict(new_rows)
    merged.update(changed)
    for item in removed:
        merged.pop(item, None)
    order = [item for item in new_rows if item in merged]
    order += [item for item in changed if item not in new_rows]
    new.write(out_path, [merged[item] for item in order])
    print(f"{name}: {len(changed)} rows carried ({sum(item not in old_rows for item in changed)} added), "
          f"{len(removed)} removed, {len(clashes)} clashes, {len(order)} rows written")
    for item in clashes[:20]:
        print(f"  clash on {item if new.id_format else 'a row'}: the new base changed a row we also changed;"
              " ours is kept")
    return clashes


if __name__ == "__main__":
    carry(*sys.argv[1:6])
