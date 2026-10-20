"""Give the Two-Headed Ogre's cyclops head its own helmet as a spell visual: one model copy per helmet model and
texture (the texture baked in, the fit mirrored from the two-eyed head), plus the spell visual table rows that fasten
it to the cyclops head's helmet attachment. Writes a JSON list of the auras for mkspells.py and the display mapping
SQL for the server.
    python mkcyclops.py <helmets.zip> <client DBC folder> <output root> <auras.json> <mapping.sql> [model:texture ...]
Without model:texture pairs every helmet display found in ItemDisplayInfo.dbc is written."""
import json
import struct
import sys
import zipfile
from pathlib import Path, PureWindowsPath

import numpy as np

from mkcape import Blob, arr
from mkeyes import ATTACHMENT_SCALE, CYCLOPS_HELMET_ATTACHMENT, HELMET_SCALE
from mkhelmets import HEAD, SOURCE_SUFFIX, baked, correction, armor_of_models, dbc_text, entries, read_dbc

FIRST_ID = 9531000
OBJECT_SKIN = 2
HARDCODED = 0
TEXTURES = 80
TEXTURE_SIZE = 16
FOLDER = "Item\\ObjectComponents\\Head\\Cyclops\\"
TEXTURE_FOLDER = "Item\\ObjectComponents\\Head\\"
KIT_FIELDS = 38
KIT_ANIMATIONS = (1, 2)
KIT_CHARACTER_PROCS = range(17, 21)
VISUAL_FIELDS = 32
VISUAL_STATE_KIT = 4
EFFECT_SCALES = (1.0, 1.0, 0.01, 1.0)
VERTEX_SIZE = 48
PAIRED_EYE_PIECES = {"helm_cloth_mawraid_d_01_eyes"}
SINGLE_EYE_PIECES = {"helm_monocle_a_01"}


class Table:
    def __init__(self, path):
        data = Path(path).read_bytes()
        _, count, self.fields, self.size, _ = struct.unpack_from("<4s4I", data)
        self.rows = [data[20 + index * self.size:20 + (index + 1) * self.size] for index in range(count)]
        self.strings = bytearray(data[20 + count * self.size:])
        self.text_offsets = {}

    def text(self, value):
        if value not in self.text_offsets:
            self.text_offsets[value] = len(self.strings)
            self.strings += value.encode("utf-8") + b"\0"
        return self.text_offsets[value]

    def replace(self, first, new_rows):
        last = first + len(new_rows)
        kept = [row for row in self.rows if not first <= struct.unpack_from("<I", row)[0] < last]
        self.rows = kept + new_rows

    def save(self, path):
        Path(path).parent.mkdir(parents=True, exist_ok=True)
        with open(path, "wb") as out:
            out.write(struct.pack("<4s4I", b"WDBC", len(self.rows), self.fields, self.size, len(self.strings)))
            out.write(b"".join(self.rows))
            out.write(self.strings)


def seated_on_cyclops(fix):
    size = ATTACHMENT_SCALE * np.array(HELMET_SCALE)
    shift = np.array(fix["shift"]) * (1.0, -1.0, 1.0)
    return {"shift": tuple(shift * size), "scale": tuple(np.array(fix["scale"]) * size)}


def one_eyed(m2, paired):
    data = bytearray(m2)
    count, offset = arr(data, 60)
    positions = np.array([struct.unpack_from("<3f", data, offset + i * VERTEX_SIZE) for i in range(count)])
    sides = (positions[:, 1] < 0, positions[:, 1] > 0) if paired else (np.ones(count, bool),)
    for side in sides:
        if side.any():
            positions[side, 1] -= positions[side, 1].mean()
    for index, position in enumerate(positions):
        struct.pack_into("<3f", data, offset + index * VERTEX_SIZE, *position)
    return bytes(data)


def textured(m2, texture_path):
    blob = Blob(m2)
    name = blob.append(texture_path.encode("latin1") + b"\0", 4)
    count, offset = arr(m2, TEXTURES)
    for index in range(count):
        record = offset + index * TEXTURE_SIZE
        if struct.unpack_from("<I", blob.data, record)[0] == OBJECT_SKIN:
            struct.pack_into("<IIII", blob.data, record, HARDCODED, 0, len(texture_path) + 1, name)
    return bytes(blob.data)


def displays(bundle):
    rows, strings = read_dbc(bundle.read("DBFilesClient/ItemDisplayInfo.dbc"))
    for row in rows:
        display, model_name, _, model_texture = struct.unpack_from("<4I", row)
        model = PureWindowsPath(dbc_text(strings, model_name)).stem.lower()
        texture = dbc_text(strings, model_texture).lower()
        if model and texture:
            yield display, model, texture


def pairs_of(bundle, names, wanted):
    found = {}
    for display, model, texture in displays(bundle):
        if HEAD + model + SOURCE_SUFFIX + ".m2" not in names or HEAD + texture + ".blp" not in names:
            continue
        if wanted and (model, texture) not in wanted:
            continue
        found.setdefault((model, texture), []).append(display)
    return found


def kit_row(kit_id):
    values = [0] * KIT_FIELDS
    values[0] = kit_id
    for field in KIT_ANIMATIONS + tuple(KIT_CHARACTER_PROCS):
        values[field] = -1
    return struct.pack("<%di" % KIT_FIELDS, *values)


def visual_row(visual_id, kit_id):
    values = [0] * VISUAL_FIELDS
    values[0] = visual_id
    values[VISUAL_STATE_KIT] = kit_id
    return struct.pack("<%dI" % VISUAL_FIELDS, *values)


def main(zip_path, dbc_folder, root, auras_path, mapping_path, *wanted):
    bundle = zipfile.ZipFile(zip_path)
    names = entries(bundle)
    models, _ = armor_of_models(bundle)
    pairs = pairs_of(bundle, names, {tuple(pair.lower().split(":")) for pair in wanted})
    tables = {name: Table(Path(dbc_folder) / (name + ".dbc")) for name in
              ("SpellVisualEffectName", "SpellVisualKit", "SpellVisualKitModelAttach", "SpellVisual")}
    effects, kits, attachments, visuals, auras, mapping = [], [], [], [], [], []
    root = Path(root)
    for index, ((model, texture), shown_by) in enumerate(sorted(pairs.items())):
        record_id = FIRST_ID + index
        source = names[HEAD + model + SOURCE_SUFFIX + ".m2"]
        texture_path = str(PureWindowsPath(TEXTURE_FOLDER) / PureWindowsPath(names[HEAD + texture + ".blp"]).name)
        model_path = FOLDER + "%s__%s.m2" % (PureWindowsPath(source).stem[:-len(SOURCE_SUFFIX)],
                                             PureWindowsPath(texture_path).stem)
        target = root / Path(*PureWindowsPath(model_path).parts)
        target.parent.mkdir(parents=True, exist_ok=True)
        fix = seated_on_cyclops(correction(model, models.get(model, "misc")))
        helmet = bundle.read(source)
        if model in PAIRED_EYE_PIECES | SINGLE_EYE_PIECES:
            helmet = one_eyed(helmet, model in PAIRED_EYE_PIECES)
        target.write_bytes(textured(baked(helmet, fix), texture_path))
        target.with_name(target.stem + "00.skin").write_bytes(bundle.read(names[HEAD + model + SOURCE_SUFFIX +
                                                                              "00.skin"]))
        effect_table = tables["SpellVisualEffectName"]
        effects.append(struct.pack("<3I4f", record_id, effect_table.text(target.stem), effect_table.text(model_path),
                                   *EFFECT_SCALES))
        kits.append(kit_row(record_id))
        attachments.append(struct.pack("<4I6f", record_id, record_id, record_id, CYCLOPS_HELMET_ATTACHMENT,
                                       0, 0, 0, 0, 0, 0))
        visuals.append(visual_row(record_id, record_id))
        auras.append([record_id, record_id, "Cyclops Helmet"])
        mapping += [(display, record_id) for display in shown_by]
    for name, rows in (("SpellVisualEffectName", effects), ("SpellVisualKit", kits),
                       ("SpellVisualKitModelAttach", attachments), ("SpellVisual", visuals)):
        tables[name].replace(FIRST_ID, rows)
        tables[name].save(root / "DBFilesClient" / (name + ".dbc"))
    Path(auras_path).write_text(json.dumps(auras))
    with open(mapping_path, "w") as out:
        out.write("-- mod-subclass: which aura shows a helmet display on the Two-Headed Ogre's cyclops head.\n\n")
        out.write("DROP TABLE IF EXISTS `mod_subclass_cyclops_helmet`;\n")
        out.write("CREATE TABLE `mod_subclass_cyclops_helmet` (\n    `DisplayId` INT UNSIGNED NOT NULL,\n"
                  "    `SpellId` INT UNSIGNED NOT NULL,\n    PRIMARY KEY (`DisplayId`)\n) ENGINE=InnoDB "
                  "DEFAULT CHARSET=utf8mb4;\n")
        for start in range(0, len(mapping), 500):
            chunk = mapping[start:start + 500]
            out.write("INSERT INTO `mod_subclass_cyclops_helmet` (`DisplayId`, `SpellId`) VALUES\n")
            out.write(",\n".join("(%d, %d)" % pair for pair in chunk) + ";\n")
    print(len(pairs), "cyclops helmets,", len(mapping), "displays")


if __name__ == "__main__":
    main(*sys.argv[1:])
