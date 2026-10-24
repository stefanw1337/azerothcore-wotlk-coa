"""Hide the Two-Headed Ogre's jaws under helmets that cover the mouth, and bring those helmets forward far enough
to cover her upper lip. Hide the cyclops head's crest under the hats that enclose it.
    python mkjaws.py split <TwoHeadedOgre.m2> <TwoHeadedOgre00.skin> <CharacterFacialHairStyles.dbc>
    python mkjaws.py fit <TwoHeadedOgre.m2> <TwoHeadedOgre00.skin> <helmets.zip> <own helmet folder>
        <cyclops root> <cyclops map.sql> <mouth cover.sql>
    python mkjaws.py crown <TwoHeadedOgre.m2> <TwoHeadedOgre00.skin> <CharacterFacialHairStyles.dbc>
    python mkjaws.py crownfit <TwoHeadedOgre.m2> <TwoHeadedOgre00.skin> <cyclops root> <cyclops map.sql>
        <mouth cover.sql> <crown cover.sql>
split moves each jaw into facial geosets the server switches with her facial style. fit measures every helmet on
both heads, pushes the mouth-covering ones forward in place and writes which displays cover which mouth. crown
moves the cyclops crest into facial geosets of its own: the whole crest, or only its base when the top above a high
band is hidden. crownfit marks, per cut, the cyclops helmets whose band reaches below it all around and that roof over
the opening, even where the crest pokes through them."""
import re
import struct
import sys
import textwrap
import zipfile
from pathlib import Path, PureWindowsPath

import numpy as np

from mkcape import arr
from mkcyclops import bounded
from mkeyes import HELMET_SCALE
from mkhelmets import (CYCLOPS_HEAD, HEAD, TWO_EYED_HEAD, bone_parents, dbc_text, helmet_attachment, load_mesh,
                       pivots, read_dbc, subtree)

LEFT_JAW, RIGHT_JAW = 27, 26
BOTH_JAWS, RIGHT_JAW_ONLY, LEFT_JAW_ONLY, NO_JAW = 301, 303, 305, 307
GEOSET_SPAN = 2
OGRE_RACE, FEMALE = 85, 1
STYLE_FIELDS = 8
JAW_GEOSET_FIELD = 4
SUBMESH_SIZE, TEXTURE_UNIT_SIZE = 48, 24
SKIN_INDICES, SKIN_SUBMESHES, SKIN_TEXTURE_UNITS = 12, 28, 36
VERTEX_SIZE = 48
CELL = 0.03
FRONT_OF_FACE = 0.1
COVERED_SHARE = 0.6
LIP_HEIGHT = 0.04
LIP_MARGIN = {"left": 0.015, "right": 0.04}
MOST_PUSH = {"left": 0.01, "right": 0.15}
TOP_CUT, BASE_CUT = 2.68, 2.62
CREST_WHOLE, CREST_BASE, CREST_NONE = 1601, 1603, 1605
CROWN_GEOSET_FIELD = 6
TOP_HIDDEN, WHOLE_HIDDEN = 4, 8
BAND_DIRECTIONS = 12
BAND_WIDTH, BAND_REACH, BAND_SLACK = 0.05, 0.2, 0.01
ABOVE_RADIUS = 0.06
CROWN_ENCLOSED = 0.95
SURFACE_SAMPLES = ((0.33, 0.33), (0.6, 0.2), (0.2, 0.6), (0.2, 0.2), (0.5, 0.5), (0.5, 0.0), (0.0, 0.5))


def dominant_bones(m2):
    count, offset = arr(m2, 60)
    raw = np.frombuffer(m2, np.uint8, count * VERTEX_SIZE, offset).reshape(-1, VERTEX_SIZE)
    weights, indices = raw[:, 12:16].astype(np.int32), raw[:, 16:20].astype(np.int32)
    return indices[np.arange(count), weights.argmax(axis=1)]


def jaw_triangles(m2, skin, body):
    bones = dominant_bones(m2)
    lookup_count, lookup_offset = arr(skin, 4)
    lookup = np.frombuffer(skin, np.uint16, lookup_count, lookup_offset).astype(np.int64)
    _, index_offset = arr(skin, SKIN_INDICES)
    start, count = struct.unpack_from("<HH", skin, body + 8)
    corners = np.frombuffer(skin, np.uint16, count, index_offset + start * 2).reshape(-1, 3)
    owners = bones[lookup[corners]]
    return corners, (owners == LEFT_JAW).all(axis=1), (owners == RIGHT_JAW).all(axis=1)


def split(m2_path, skin_path, styles_path):
    m2, skin = Path(m2_path).read_bytes(), bytearray(Path(skin_path).read_bytes())
    body = arr(skin, SKIN_SUBMESHES)[1]
    corners, left, right = jaw_triangles(m2, bytes(skin), body)
    pieces = ((BOTH_JAWS, left | right), (RIGHT_JAW_ONLY, right), (LEFT_JAW_ONLY, left))
    skin = moved_into_geosets(skin, body, corners, left | right, pieces)
    Path(skin_path).write_bytes(bytes(skin))
    write_styles(styles_path)
    print("jaw triangles: left", int(left.sum()), "right", int(right.sum()))


def moved_into_geosets(skin, body, corners, moved, pieces):
    kept = corners[~moved]
    _, index_offset = arr(skin, SKIN_INDICES)
    start = struct.unpack_from("<H", skin, body + 8)[0]
    skin[index_offset + start * 2:index_offset + start * 2 + kept.size * 2] = kept.astype(np.uint16).tobytes()
    struct.pack_into("<H", skin, body + 10, kept.size)
    submesh_count, submesh_offset = arr(skin, SKIN_SUBMESHES)
    index_count = arr(skin, SKIN_INDICES)[0]
    indices = bytearray(skin[index_offset:index_offset + index_count * 2])
    submeshes = bytearray(skin[submesh_offset:submesh_offset + submesh_count * SUBMESH_SIZE])
    unit_count, unit_offset = arr(skin, SKIN_TEXTURE_UNITS)
    units = bytearray(skin[unit_offset:unit_offset + unit_count * TEXTURE_UNIT_SIZE])
    body_unit = next(index for index in range(unit_count)
                     if struct.unpack_from("<H", units, index * TEXTURE_UNIT_SIZE + 4)[0] == 0)
    for geoset, shown in ((first + offset, shown) for first, shown in pieces for offset in range(GEOSET_SPAN)):
        piece = corners[shown]
        submesh = bytearray(submeshes[:SUBMESH_SIZE])
        struct.pack_into("<HH", submesh, 0, geoset, 0)
        struct.pack_into("<HH", submesh, 8, len(indices) // 2, piece.size)
        indices += piece.astype(np.uint16).tobytes()
        unit = bytearray(units[body_unit * TEXTURE_UNIT_SIZE:(body_unit + 1) * TEXTURE_UNIT_SIZE])
        struct.pack_into("<HH", unit, 4, len(submeshes) // SUBMESH_SIZE, len(submeshes) // SUBMESH_SIZE)
        submeshes += submesh
        units += unit
    for header, payload, size in ((SKIN_INDICES, indices, 2), (SKIN_SUBMESHES, submeshes, SUBMESH_SIZE),
                                  (SKIN_TEXTURE_UNITS, units, TEXTURE_UNIT_SIZE)):
        while len(skin) % 16:
            skin.append(0)
        struct.pack_into("<II", skin, header, len(payload) // size, len(skin))
        skin += payload
    return skin


def crest_triangles(m2, skin, body):
    bones = dominant_bones(m2)
    lookup_count, lookup_offset = arr(skin, 4)
    lookup = np.frombuffer(skin, np.uint16, lookup_count, lookup_offset).astype(np.int64)
    _, index_offset = arr(skin, SKIN_INDICES)
    start, count = struct.unpack_from("<HH", skin, body + 8)
    corners = np.frombuffer(skin, np.uint16, count, index_offset + start * 2).reshape(-1, 3)
    lowest = positions(m2)[lookup[corners], 2].min(axis=1)
    cyclops = np.isin(bones[lookup[corners]], list(subtree(bone_parents(m2), {CYCLOPS_HEAD}))).all(axis=1)
    return corners, cyclops & (lowest > TOP_CUT), cyclops & (lowest > BASE_CUT) & (lowest <= TOP_CUT)


def crown(m2_path, skin_path, styles_path):
    m2, skin = Path(m2_path).read_bytes(), bytearray(Path(skin_path).read_bytes())
    body = arr(skin, SKIN_SUBMESHES)[1]
    corners, top, base = crest_triangles(m2, bytes(skin), body)
    skin = moved_into_geosets(skin, body, corners, top | base, ((CREST_WHOLE, top | base), (CREST_BASE, base)))
    Path(skin_path).write_bytes(bytes(skin))
    write_styles(styles_path)
    print("crest triangles: top", int(top.sum()), "base", int(base.sum()))


def write_styles(path):
    data = Path(path).read_bytes()
    _, count, fields, size, string_size = struct.unpack_from("<4s4I", data)
    rows = [data[20 + index * size:20 + (index + 1) * size] for index in range(count)]
    kept = [row for row in rows if struct.unpack_from("<2i", row) != (OGRE_RACE, FEMALE)]
    jaws = (BOTH_JAWS, RIGHT_JAW_ONLY, LEFT_JAW_ONLY, NO_JAW)
    for variation, (crest, geoset) in enumerate((crest, geoset) for crest in (CREST_WHOLE, CREST_BASE, CREST_NONE)
                                                for geoset in jaws):
        values = [0] * STYLE_FIELDS
        values[:3] = OGRE_RACE, FEMALE, variation
        values[JAW_GEOSET_FIELD] = geoset - 300
        values[CROWN_GEOSET_FIELD] = crest - 1600
        kept.append(struct.pack("<%di" % STYLE_FIELDS, *values))
    strings = data[20 + count * size:]
    Path(path).write_bytes(struct.pack("<4s4I", b"WDBC", len(kept), fields, size, string_size) + b"".join(kept) +
                           strings)


class Face:
    def __init__(self, m2_path, skin_path):
        m2, skin = Path(m2_path).read_bytes(), Path(skin_path).read_bytes()
        mesh = load_mesh(m2, skin, {0})
        parents = bone_parents(m2)
        self.attach = helmet_attachment(m2)[1]
        self.heads = {}
        for side, skull, jaw in (("left", TWO_EYED_HEAD, LEFT_JAW), ("right", CYCLOPS_HEAD, RIGHT_JAW)):
            bones = subtree(parents, {skull})
            points = mesh.positions[np.isin(mesh.bones, list(bones))]
            jaw_points = mesh.positions[mesh.bones == jaw]
            top = jaw_points[:, 2].max()
            lip = points[(mesh.bones[np.isin(mesh.bones, list(bones))] != jaw) & (points[:, 2] >= top) &
                         (points[:, 2] <= top + LIP_HEIGHT)]
            self.heads[side] = (pivots(m2)[skull], jaw_points, lip)

    def fit(self, side, helmet):
        skull, jaw, lip = self.heads[side]
        front = helmet[helmet[:, 0] > skull[0] + FRONT_OF_FACE]
        cells = covered = 0
        for y in np.arange(jaw[:, 1].min(), jaw[:, 1].max(), CELL):
            for z in np.arange(jaw[:, 2].min(), jaw[:, 2].max(), CELL):
                if not ((np.abs(jaw[:, 1] - y) < CELL) & (np.abs(jaw[:, 2] - z) < CELL)).any():
                    continue
                cells += 1
                covered += ((np.abs(front[:, 1] - y) < CELL) & (np.abs(front[:, 2] - z) < CELL)).any()
        if not cells or covered < cells * COVERED_SHARE:
            return False, 0.0
        need = 0.0
        for point in lip:
            near = front[(np.abs(front[:, 1] - point[1]) < CELL) & (np.abs(front[:, 2] - point[2]) < CELL)]
            if len(near):
                need = max(need, point[0] + LIP_MARGIN[side] - near[:, 0].max())
        return True, min(need, MOST_PUSH[side])


def positions(data):
    count, offset = arr(data, 60)
    return np.array([struct.unpack_from("<3f", data, offset + index * VERTEX_SIZE) for index in range(count)])


def pushed(data, push):
    out = bytearray(data)
    count, offset = arr(out, 60)
    for index in range(count):
        vertex = offset + index * VERTEX_SIZE
        x = struct.unpack_from("<f", out, vertex)[0]
        struct.pack_into("<f", out, vertex, x + push)
    return bounded(bytes(out))


def fit(m2_path, skin_path, zip_path, own_folder, cyclops_root, cyclops_map, out_sql):
    face = Face(m2_path, skin_path)
    scale = np.array(HELMET_SCALE)
    folders = [folder for folder in Path(own_folder).parent.iterdir()
               if folder.is_dir() and folder.name.lower() == Path(own_folder).name.lower()]
    own = {path.stem[:-len("_TwF")].lower(): path for folder in folders for path in folder.iterdir()
           if path.suffix.lower() == ".m2" and path.stem.lower().endswith("_twf")}
    covers = {}
    bundle = zipfile.ZipFile(zip_path)
    rows, strings = read_dbc(bundle.read("DBFilesClient/ItemDisplayInfo.dbc"))
    by_model = {}
    for row in rows:
        display, model_name = struct.unpack_from("<2I", row)
        stem = PureWindowsPath(dbc_text(strings, model_name)).stem.lower()
        if stem in own:
            by_model.setdefault(stem, []).append(display)
    for stem, displays in by_model.items():
        data = own[stem].read_bytes()
        covered, push = face.fit("left", face.attach + positions(data) * scale)
        if covered:
            if push > 0:
                own[stem].write_bytes(pushed(data, push / scale[0]))
            for display in displays:
                covers[display] = covers.get(display, 0) | 1
    cyclops_models = {}
    for display, model in re.findall(r"\((\d+), \d+, '([^']+)'\)", Path(cyclops_map).read_text()):
        cyclops_models.setdefault(model.replace("\\\\", "\\"), []).append(int(display))
    for model, displays in cyclops_models.items():
        path = Path(cyclops_root, *PureWindowsPath(model).parts)
        if not path.exists():
            continue
        data = path.read_bytes()
        covered, push = face.fit("right", positions(data))
        if covered:
            if push > 0:
                path.write_bytes(pushed(data, push))
            for display in displays:
                covers[display] = covers.get(display, 0) | 2
    with open(out_sql, "w") as out:
        out.write("-- mod-subclass: helmet displays that cover a Two-Headed Ogre mouth "
                  "(1 left head, 2 right head).\n\n")
        out.write("DROP TABLE IF EXISTS `mod_subclass_mouth_cover`;\n")
        out.write("CREATE TABLE `mod_subclass_mouth_cover` (\n    `DisplayId` INT UNSIGNED NOT NULL,\n"
                  "    `Heads` TINYINT UNSIGNED NOT NULL,\n    PRIMARY KEY (`DisplayId`)\n) ENGINE=InnoDB "
                  "DEFAULT CHARSET=utf8mb4;\n")
        entries = sorted(covers.items())
        for start in range(0, len(entries), 500):
            chunk = entries[start:start + 500]
            out.write("DELETE FROM `mod_subclass_mouth_cover` WHERE `DisplayId` BETWEEN %d AND %d;\n"
                      % (chunk[0][0], chunk[-1][0]))
            out.write("INSERT INTO `mod_subclass_mouth_cover` (`DisplayId`, `Heads`) VALUES\n")
            out.write(",\n".join("(%d, %d)" % pair for pair in chunk) + ";\n")
    print(len(by_model), "own helmets,", len(cyclops_models), "cyclops helmets,", len(covers), "covering displays")


def surface_points(data, skin):
    mesh = load_mesh(data, skin)
    corners = mesh.positions[mesh.triangles]
    samples = [corners[:, 0] * (1 - a - b) + corners[:, 1] * a + corners[:, 2] * b for a, b in SURFACE_SAMPLES]
    return np.concatenate([mesh.positions] + samples)


class Crest:
    def __init__(self, m2_path, skin_path):
        m2, skin = Path(m2_path).read_bytes(), Path(skin_path).read_bytes()
        crest = load_mesh(m2, skin, {CREST_WHOLE})
        self.points = crest.positions[np.unique(crest.triangles)]
        body = load_mesh(m2, skin, {0})
        head = body.positions[np.isin(body.bones, list(subtree(bone_parents(m2), {CYCLOPS_HEAD})))]
        self.centre = pivots(m2)[CYCLOPS_HEAD][:2]
        self.bands = []
        for angle in np.linspace(0, 2 * np.pi, BAND_DIRECTIONS, endpoint=False):
            direction = np.array([np.cos(angle), np.sin(angle)])
            across = np.array([-direction[1], direction[0]])
            offset = head[:, :2] - self.centre
            side = head[(np.abs(offset @ across) < BAND_WIDTH) & (offset @ direction > 0)]
            self.bands.append((direction, across, ((side[:, :2] - self.centre) @ direction).max()))

    def enclosed(self, hat, cut):
        offset = hat[:, :2] - self.centre
        for direction, across, reach in self.bands:
            along = offset @ direction
            band = (np.abs(offset @ across) < BAND_WIDTH) & (along > 0) & (along < reach + BAND_REACH)
            if not band.any() or hat[band, 2].min() > cut + BAND_SLACK:
                return False
        opening = self.points[self.points[:, 2] > cut + BAND_SLACK]
        apart = np.hypot(*(hat[None, :, :2] - opening[:, None, :2]).transpose(2, 0, 1))
        roofed = ((apart < ABOVE_RADIUS) & (hat[None, :, 2] > cut)).any(axis=1)
        return roofed.mean() >= CROWN_ENCLOSED

    def hidden(self, hat):
        return ((WHOLE_HIDDEN if self.enclosed(hat, BASE_CUT) else 0) |
                (TOP_HIDDEN if self.enclosed(hat, TOP_CUT) else 0))


def crownfit(m2_path, skin_path, cyclops_root, cyclops_map, mouth_sql, out_sql):
    crest = Crest(m2_path, skin_path)
    cyclops_models = {}
    for display, model in re.findall(r"\((\d+), \d+, '([^']+)'\)", Path(cyclops_map).read_text()):
        cyclops_models.setdefault(model.replace("\\\\", "\\"), []).append(int(display))
    covering = {}
    for model, displays in cyclops_models.items():
        path = Path(cyclops_root, *PureWindowsPath(model).parts)
        skin = path.with_name(path.stem + "00.skin")
        if path.exists() and skin.exists():
            hidden = crest.hidden(surface_points(path.read_bytes(), skin.read_bytes()))
            if hidden:
                covering.update((display, hidden) for display in displays)
    mouths = {int(display): int(heads)
              for display, heads in re.findall(r"^\((\d+), (\d+)\)", Path(mouth_sql).read_text(), re.M)}
    entries = sorted((display, mouths.get(display, 0) | hidden) for display, hidden in covering.items())
    with open(out_sql, "w") as out:
        out.write("-- mod-subclass: cyclops helmet displays that enclose the cyclops head's crest top (Heads bit "
                  "%d) or the whole\n-- crest (bit %d), kept with the mouths they cover.\n"
                  % (TOP_HIDDEN, WHOLE_HIDDEN))
        for start in range(0, len(entries), 500):
            chunk = entries[start:start + 500]
            ids = textwrap.fill(", ".join(str(display) for display, _ in chunk), 116, initial_indent="    ",
                                subsequent_indent="    ")
            out.write("\nDELETE FROM `mod_subclass_mouth_cover` WHERE `DisplayId` IN (\n%s);\n" % ids)
            out.write("INSERT INTO `mod_subclass_mouth_cover` (`DisplayId`, `Heads`) VALUES\n")
            out.write(",\n".join("(%d, %d)" % pair for pair in chunk) + ";\n")
    whole = sum(1 for hidden in covering.values() if hidden & WHOLE_HIDDEN)
    print(len(cyclops_models), "cyclops helmets,", len(covering), "displays hide the crest top,", whole, "all of it")


if __name__ == "__main__":
    {"split": split, "fit": fit, "crown": crown, "crownfit": crownfit}[sys.argv[1]](*sys.argv[2:])
