"""Give the Thin Twins hair under hats. A hat that hides hair switches the hair group to geoset 1 (client 0x4EF1DD),
which the model leaves empty; this copies the part of a hairstyle below the hat line into geoset 1.
    python mkhathair.py <m2> <skin> <out skin> [hairstyle geoset] [hat line] [front limit]"""
import struct
import sys
from pathlib import Path

import numpy as np

from mkcape import arr

SUBMESH_SIZE, TEXTURE_UNIT_SIZE = 48, 24
SKIN_LOOKUP, SKIN_INDICES, SKIN_SUBMESHES, SKIN_TEXTURE_UNITS = 4, 12, 28, 36
VERTEX_SIZE = 48
HAT_HAIR = 1


def positions(m2):
    count, offset = arr(m2, 60)
    raw = np.frombuffer(m2, np.uint8, count * VERTEX_SIZE, offset).reshape(-1, VERTEX_SIZE)
    return raw[:, 0:12].copy().view(np.float32).reshape(-1, 3).astype(np.float64)


def submeshes(skin):
    count, offset = arr(skin, SKIN_SUBMESHES)
    return [struct.unpack_from("<6H", skin, offset + index * SUBMESH_SIZE) for index in range(count)]


def under_hat(m2, skin, style, hat_line, front):
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
        shown = (corner_points[:, :, 2].max(axis=1) < hat_line) & (corner_points[:, :, 0].max(axis=1) < front)
        kept.append((number, triangles[shown]))
    return kept


def with_hat_hair(skin, pieces):
    skin = bytearray(skin)
    if any(geoset == HAT_HAIR for geoset, *_ in submeshes(skin)):
        raise SystemExit("The skin already has hair under hats")
    index_count, index_offset = arr(skin, SKIN_INDICES)
    indices = bytearray(skin[index_offset:index_offset + index_count * 2])
    submesh_count, submesh_offset = arr(skin, SKIN_SUBMESHES)
    sections = bytearray(skin[submesh_offset:submesh_offset + submesh_count * SUBMESH_SIZE])
    unit_count, unit_offset = arr(skin, SKIN_TEXTURE_UNITS)
    units = bytearray(skin[unit_offset:unit_offset + unit_count * TEXTURE_UNIT_SIZE])
    for source, triangles in pieces:
        start = len(indices) // 2
        section = bytearray(sections[source * SUBMESH_SIZE:(source + 1) * SUBMESH_SIZE])
        struct.pack_into("<HH", section, 0, HAT_HAIR, start >> 16)
        struct.pack_into("<HH", section, 8, start & 0xFFFF, triangles.size)
        indices += triangles.astype(np.uint16).tobytes()
        target = len(sections) // SUBMESH_SIZE
        sections += section
        for unit in range(unit_count):
            if struct.unpack_from("<H", units, unit * TEXTURE_UNIT_SIZE + 4)[0] == source:
                copy = bytearray(units[unit * TEXTURE_UNIT_SIZE:(unit + 1) * TEXTURE_UNIT_SIZE])
                struct.pack_into("<HH", copy, 4, target, target)
                units += copy
    for header, payload, size in ((SKIN_INDICES, indices, 2), (SKIN_SUBMESHES, sections, SUBMESH_SIZE),
                                  (SKIN_TEXTURE_UNITS, units, TEXTURE_UNIT_SIZE)):
        while len(skin) % 16:
            skin.append(0)
        struct.pack_into("<II", skin, header, len(payload) // size, len(skin))
        skin += payload
    return bytes(skin)


def main(m2_path, skin_path, out_path, style="2", hat_line="1.83", front="0.06"):
    m2, skin = Path(m2_path).read_bytes(), Path(skin_path).read_bytes()
    pieces = under_hat(m2, skin, int(style), float(hat_line), float(front))
    if not any(triangles.size for _, triangles in pieces):
        raise SystemExit(f"Hairstyle geoset {style} has nothing below {hat_line}")
    Path(out_path).write_bytes(with_hat_hair(skin, pieces))
    print(f"geoset {HAT_HAIR}: {sum(len(triangles) for _, triangles in pieces)} triangles of hairstyle geoset {style}")


if __name__ == "__main__":
    main(*sys.argv[1:])
