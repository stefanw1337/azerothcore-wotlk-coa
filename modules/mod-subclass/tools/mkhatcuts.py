"""Give each Thin Twins hairstyle its own cut under hats. A hat that hides hair forces the hair group to geoset 1, so
geoset 1 is left empty and the part of hairstyle 2, 3 and 4 below the hat line becomes facial geoset 302, 303 and 304.
The server picks the facial style beard + 7 * (hairstyle + 1) while a hat is shown, so the table gets those rows.
    python mkhatcuts.py <thinhumanmale.m2> <skin> <CharacterFacialHairStyles.dbc> <out skin> <out dbc>"""
import struct
import sys
from pathlib import Path

import numpy as np

from mkcape import arr

SUBMESH_SIZE, TEXTURE_UNIT_SIZE, VERTEX_SIZE = 48, 24, 48
SKIN_LOOKUP, SKIN_INDICES, SKIN_SUBMESHES, SKIN_TEXTURE_UNITS = 4, 12, 28, 36
HAT_HAIR, UNUSED = 1, 9901
CUTS = ((2, 302), (3, 303), (4, 304))
HAT_LINE, FRONT = 1.83, 0.06
THIN_TWINS, MALE, BEARDS = 96, 0, 7
VARIATION_FIELD, CUT_FIELD = 2, 4


def positions(m2):
    count, offset = arr(m2, 60)
    raw = np.frombuffer(m2, np.uint8, count * VERTEX_SIZE, offset).reshape(-1, VERTEX_SIZE)
    return raw[:, 0:12].copy().view(np.float32).reshape(-1, 3).astype(np.float64)


def submeshes(skin):
    count, offset = arr(skin, SKIN_SUBMESHES)
    return [struct.unpack_from("<6H", skin, offset + index * SUBMESH_SIZE) for index in range(count)]


def emptied_hat_hair(skin):
    skin = bytearray(skin)
    _, offset = arr(skin, SKIN_SUBMESHES)
    for index, (geoset, *_) in enumerate(submeshes(skin)):
        if geoset == HAT_HAIR:
            struct.pack_into("<H", skin, offset + index * SUBMESH_SIZE, UNUSED)
    return bytes(skin)


def below_hat(m2, skin, style):
    lookup_count, lookup_offset = arr(skin, SKIN_LOOKUP)
    lookup = np.frombuffer(skin, np.uint16, lookup_count, lookup_offset).astype(np.int64)
    index_count, index_offset = arr(skin, SKIN_INDICES)
    corners = np.frombuffer(skin, np.uint16, index_count, index_offset).astype(np.int64)
    points = positions(m2)
    kept = []
    for number, (geoset, level, _, _, start, size) in enumerate(submeshes(skin)):
        if geoset != style:
            continue
        triangles = corners[(start | level << 16):(start | level << 16) + size].reshape(-1, 3)
        corner_points = points[lookup[triangles]]
        shown = (corner_points[:, :, 2].max(axis=1) < HAT_LINE) & (corner_points[:, :, 0].max(axis=1) < FRONT)
        kept.append((number, triangles[shown]))
    return kept


def with_pieces(skin, target, pieces):
    skin = bytearray(skin)
    index_count, index_offset = arr(skin, SKIN_INDICES)
    indices = bytearray(skin[index_offset:index_offset + index_count * 2])
    submesh_count, submesh_offset = arr(skin, SKIN_SUBMESHES)
    sections = bytearray(skin[submesh_offset:submesh_offset + submesh_count * SUBMESH_SIZE])
    unit_count, unit_offset = arr(skin, SKIN_TEXTURE_UNITS)
    units = bytearray(skin[unit_offset:unit_offset + unit_count * TEXTURE_UNIT_SIZE])
    for source, triangles in pieces:
        start = len(indices) // 2
        section = bytearray(sections[source * SUBMESH_SIZE:(source + 1) * SUBMESH_SIZE])
        struct.pack_into("<HH", section, 0, target, start >> 16)
        struct.pack_into("<HH", section, 8, start & 0xFFFF, triangles.size)
        indices += triangles.astype(np.uint16).tobytes()
        added = len(sections) // SUBMESH_SIZE
        sections += section
        for unit in range(unit_count):
            if struct.unpack_from("<H", units, unit * TEXTURE_UNIT_SIZE + 4)[0] == source:
                copy = bytearray(units[unit * TEXTURE_UNIT_SIZE:(unit + 1) * TEXTURE_UNIT_SIZE])
                struct.pack_into("<HH", copy, 4, added, added)
                units += copy
    for header, payload, size in ((SKIN_INDICES, indices, 2), (SKIN_SUBMESHES, sections, SUBMESH_SIZE),
                                  (SKIN_TEXTURE_UNITS, units, TEXTURE_UNIT_SIZE)):
        while len(skin) % 16:
            skin.append(0)
        struct.pack_into("<II", skin, header, len(payload) // size, len(skin))
        skin += payload
    return bytes(skin)


def with_cut_styles(dbc):
    count, fields, size, strings = struct.unpack_from("<4I", dbc, 4)
    rows = [list(struct.unpack_from(f"<{fields}I", dbc, 20 + index * size)) for index in range(count)]
    beards = [row for row in rows if row[0] == THIN_TWINS and row[1] == MALE and row[VARIATION_FIELD] < BEARDS]
    if any(row[0] == THIN_TWINS and row[1] == MALE and row[VARIATION_FIELD] >= BEARDS for row in rows):
        raise SystemExit("The table already has the hat cuts")
    for cut in range(1, len(CUTS) + 1):
        for beard in beards:
            row = list(beard)
            row[VARIATION_FIELD] += BEARDS * cut
            row[CUT_FIELD] = cut + 1
            rows.append(row)
    body = b"".join(struct.pack(f"<{fields}I", *row) for row in rows)
    return struct.pack("<4s4I", b"WDBC", len(rows), fields, size, strings) + body + dbc[20 + count * size:]


def main(m2_path, skin_path, styles_path, out_skin, out_styles):
    m2 = Path(m2_path).read_bytes()
    skin = emptied_hat_hair(Path(skin_path).read_bytes())
    for style, target in CUTS:
        pieces = below_hat(m2, skin, style)
        skin = with_pieces(skin, target, pieces)
        print(f"hairstyle geoset {style}: {sum(len(triangles) for _, triangles in pieces)} triangles as {target}")
    Path(out_skin).write_bytes(skin)
    Path(out_styles).write_bytes(with_cut_styles(Path(styles_path).read_bytes()))


if __name__ == "__main__":
    main(*sys.argv[1:])
