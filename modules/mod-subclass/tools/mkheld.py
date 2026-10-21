import struct
import sys
import zipfile
from pathlib import Path, PureWindowsPath

from mkcape import Blob, arr
from mkcyclops import EFFECT_SCALES, HARDCODED, OBJECT_SKIN, TEXTURE_SIZE, TEXTURES, Table, bounded, kit_row, \
    visual_row

FIRST_ID = 9540000
TEMPLATE_AURA = 9531000
HAND_LEFT = 2
WEAPON = "Item\\ObjectComponents\\Weapon\\"
FOLDER = WEAPON + "Held\\"
WEAPON_CLASS = 2
HELD_SUBCLASSES = (0, 1, 2, 3, 4, 5, 6, 7, 8, 10, 13, 15, 18, 20)
SPELL_NAME = 9 + 127
SPELL_VISUAL = 131
AURA_NAME = "Held Weapon"


def dbc_rows(data):
    count, fields, size, _ = struct.unpack_from("<4I", data, 4)
    return [data[20 + index * size:20 + (index + 1) * size] for index in range(count)], data[20 + count * size:], fields


def dbc_text(strings, offset):
    return strings[offset:strings.index(b"\0", offset)].decode("latin1")


def held_pairs(item_dbc, display_dbc):
    displays, strings, fields = dbc_rows(display_dbc)
    looks = {}
    for row in displays:
        values = struct.unpack_from(f"<{fields}I", row)
        model = PureWindowsPath(dbc_text(strings, values[1])).stem.lower()
        texture = dbc_text(strings, values[3]).lower()
        if model and texture:
            looks[values[0]] = (model, texture)
    items, _, _ = dbc_rows(item_dbc)
    pairs = {}
    for row in items:
        _, item_class, subclass, _, _, display, _, _ = struct.unpack_from("<8i", row)
        if item_class == WEAPON_CLASS and subclass in HELD_SUBCLASSES and display in looks:
            pairs.setdefault(looks[display], set()).add(display)
    return pairs


def textured(m2, texture_path):
    blob = Blob(m2)
    name = blob.append(texture_path.encode("latin1") + b"\0", 4)
    count, offset = arr(m2, TEXTURES)
    for index in range(count):
        record = offset + index * TEXTURE_SIZE
        if struct.unpack_from("<I", blob.data, record)[0] == OBJECT_SKIN:
            struct.pack_into("<IIII", blob.data, record, HARDCODED, 0, len(texture_path) + 1, name)
    return bytes(blob.data)


def sql_values(text):
    values, current, quoted, index = [], "", False, 0
    while index < len(text):
        char = text[index]
        if quoted:
            current += char
            if char == "\\":
                current += text[index + 1]
                index += 1
            elif char == "'":
                quoted = False
        elif char == "'":
            quoted, current = True, current + char
        elif char == ",":
            values.append(current.strip())
            current = ""
        else:
            current += char
        index += 1
    values.append(current.strip())
    return values


def template_sql(path):
    text = Path(path).read_text()
    insert = text.index("INSERT INTO")
    values_at = text.index(") VALUES", insert) + len(") VALUES")
    body = text[values_at:].lstrip()
    return text[insert:values_at], sql_values(body[1:body.index("),\n") if "),\n" in body else body.index(");")])


def spell_rows(spell_path, ids):
    data = Path(spell_path).read_bytes()
    rows, strings, fields = dbc_rows(data)
    size = struct.unpack_from("<I", data, 12)[0]
    template = next(row for row in rows if struct.unpack_from("<I", row)[0] == TEMPLATE_AURA)
    strings = bytearray(strings)
    name = len(strings)
    strings += AURA_NAME.encode("utf-8") + b"\0"
    wanted = set(ids)
    kept = [row for row in rows if struct.unpack_from("<I", row)[0] not in wanted]
    for spell in ids:
        row = bytearray(template)
        struct.pack_into("<I", row, 0, spell)
        struct.pack_into("<I", row, SPELL_VISUAL * 4, spell)
        struct.pack_into("<I", row, SPELL_NAME * 4, name)
        kept.append(bytes(row))
    with open(spell_path, "wb") as out:
        out.write(struct.pack("<4s4I", b"WDBC", len(kept), fields, size, len(strings)))
        out.write(b"".join(kept))
        out.write(strings)


def write_auras_sql(path, template_path, ids):
    header, values = template_sql(template_path)
    rows = []
    for spell in ids:
        row = list(values)
        row[0] = row[SPELL_VISUAL] = str(spell)
        row[SPELL_NAME] = "'" + AURA_NAME + "'"
        rows.append("(" + ", ".join(row) + ")")
    with open(path, "w") as out:
        out.write("-- mod-subclass: hidden auras that show a weapon in the Two-Headed Ogre's hand while she casts.\n\n")
        out.write(f"DELETE FROM `spell_dbc` WHERE `ID` BETWEEN {FIRST_ID} AND {FIRST_ID + 9999};\n")
        for start in range(0, len(rows), 200):
            out.write(header + "\n" + ",\n".join(rows[start:start + 200]) + ";\n")


def write_mapping_sql(path, mapping):
    with open(path, "w") as out:
        out.write("-- mod-subclass: which aura shows a weapon display in the Two-Headed Ogre's hand.\n\n")
        out.write("DROP TABLE IF EXISTS `mod_subclass_held_weapon`;\n")
        out.write("CREATE TABLE `mod_subclass_held_weapon` (\n    `DisplayId` INT UNSIGNED NOT NULL,\n"
                  "    `SpellId` INT UNSIGNED NOT NULL,\n    PRIMARY KEY (`DisplayId`)\n) ENGINE=InnoDB "
                  "DEFAULT CHARSET=utf8mb4;\n")
        for start in range(0, len(mapping), 500):
            chunk = mapping[start:start + 500]
            out.write("INSERT INTO `mod_subclass_held_weapon` (`DisplayId`, `SpellId`) VALUES\n")
            out.write(",\n".join("(%d, %d)" % pair for pair in chunk) + ";\n")


def main(weapons_zip, item_dbc, display_dbc, client_root, model_root, auras_template, auras_sql, mapping_sql):
    bundle = zipfile.ZipFile(weapons_zip)
    names = {name.lower(): name for name in bundle.namelist()}
    pairs = held_pairs(Path(item_dbc).read_bytes(), Path(display_dbc).read_bytes())
    client_root, model_root = Path(client_root), Path(model_root)
    tables = {name: Table(client_root / "DBFilesClient" / (name + ".dbc")) for name in
              ("SpellVisualEffectName", "SpellVisualKit", "SpellVisualKitModelAttach", "SpellVisual")}
    effects, kits, attachments, visuals, ids, mapping, skipped = [], [], [], [], [], [], 0
    for (model, texture), shown_by in sorted(pairs.items()):
        source = names.get((WEAPON + model + ".m2").replace("\\", "/").lower())
        skin = names.get((WEAPON + model + "00.skin").replace("\\", "/").lower())
        if not source or not skin:
            skipped += 1
            continue
        record_id = FIRST_ID + len(ids)
        texture_path = WEAPON + texture + ".blp"
        model_path = FOLDER + "%s__%s.m2" % (model, PureWindowsPath(texture).name)
        target = model_root / Path(*PureWindowsPath(model_path).parts)
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(bounded(textured(bundle.read(source), texture_path)))
        target.with_name(target.stem + "00.skin").write_bytes(bundle.read(skin))
        effect_table = tables["SpellVisualEffectName"]
        effects.append(struct.pack("<3I4f", record_id, effect_table.text(target.stem), effect_table.text(model_path),
                                   *EFFECT_SCALES))
        kits.append(kit_row(record_id))
        attachments.append(struct.pack("<4I6f", record_id, record_id, record_id, HAND_LEFT, 0, 0, 0, 0, 0, 0))
        visuals.append(visual_row(record_id, record_id))
        ids.append(record_id)
        mapping += [(display, record_id) for display in sorted(shown_by)]
    for name, rows in (("SpellVisualEffectName", effects), ("SpellVisualKit", kits),
                       ("SpellVisualKitModelAttach", attachments), ("SpellVisual", visuals)):
        tables[name].replace(FIRST_ID, rows)
        tables[name].save(client_root / "DBFilesClient" / (name + ".dbc"))
    spell_rows(client_root / "DBFilesClient" / "Spell.dbc", ids)
    write_auras_sql(auras_sql, auras_template, ids)
    write_mapping_sql(mapping_sql, mapping)
    print(len(ids), "held weapons,", len(mapping), "displays,", skipped, "looks without a model")


if __name__ == "__main__":
    main(*sys.argv[1:])
