"""Give the Thin Twins a short crop as hairstyle 0. A hat that hides hair switches the hair group to geoset 1
(client 0x4EF1DD), so the same crop is the hair they show under hats. The crop follows the scalp of the head,
cut along a hairline that passes over the ears and down to the nape.
    python mkhathair.py <m2> <skin> <out m2> <out skin> [hairstyle geoset for the look]"""
import struct
import sys
from pathlib import Path

import numpy as np

from mkcape import Blob, arr

CROP = 1
BODY = 10000
VERTEX_SIZE, SUBMESH_SIZE, TEXTURE_UNIT_SIZE = 48, 48, 24
SKIN_VERTICES, SKIN_INDICES, SKIN_PROPERTIES, SKIN_SUBMESHES, SKIN_TEXTURE_UNITS = 4, 12, 20, 28, 36
NECK = 1.66
LIFT, LIFT_FADE = 0.005, 4.0
HAIRLINE = ((0, 30), (30, 28), (50, 20), (62, 5), (70, -20), (76, -20), (82, 8), (95, 12), (108, 8),
            (118, -10), (135, -30), (160, -38), (180, -40))
CROWN_V, NAPE_V, LOWEST = 0.10, 0.95, -40.0
U_FROM, U_SPAN = 0.05, 0.60


def skull_centre(points):
    top = points[points[:, 2] > points[:, 2].max() - 0.06]
    solution = np.linalg.lstsq(np.c_[2 * top, np.ones(len(top))], (top ** 2).sum(1), rcond=None)[0]
    return solution[:3]


def angles(points, centre):
    rel = points - centre
    phi = np.degrees(np.arctan2(rel[:, 1], rel[:, 0]))
    beta = np.degrees(np.arcsin(np.clip(rel[:, 2] / np.linalg.norm(rel, axis=1), -1, 1)))
    return phi, beta


def above_hairline(points, centre):
    phi, beta = angles(points, centre)
    line = np.interp(np.abs(phi), [a for a, _ in HAIRLINE], [b for _, b in HAIRLINE])
    return beta - line


def vertex_table(m2):
    count, offset = arr(m2, 60)
    raw = np.frombuffer(m2, np.uint8, count * VERTEX_SIZE, offset).reshape(-1, VERTEX_SIZE)
    positions = raw[:, 0:12].copy().view(np.float32).reshape(-1, 3).astype(np.float64)
    normals = raw[:, 20:32].copy().view(np.float32).reshape(-1, 3).astype(np.float64)
    return raw, positions, normals


def body_triangles(skin):
    lookup_count, lookup_offset = arr(skin, SKIN_VERTICES)
    lookup = np.frombuffer(skin, np.uint16, lookup_count, lookup_offset).astype(np.int64)
    index_count, index_offset = arr(skin, SKIN_INDICES)
    corners = np.frombuffer(skin, np.uint16, index_count, index_offset).astype(np.int64)
    count, offset = arr(skin, SKIN_SUBMESHES)
    found = []
    for number in range(count):
        geoset, level, _, _, start, size = struct.unpack_from("<6H", skin, offset + number * SUBMESH_SIZE)
        if geoset == BODY:
            first = start | level << 16
            found.append(lookup[corners[first:first + size]].reshape(-1, 3))
    return np.concatenate(found)


def weights_of(raw, vertex):
    return {int(bone): int(amount) for bone, amount in zip(raw[vertex, 16:20], raw[vertex, 12:16]) if amount}


def blended(first, second, share):
    mix = {bone: first.get(bone, 0) * (1 - share) + second.get(bone, 0) * share for bone in set(first) | set(second)}
    kept = sorted(mix.items(), key=lambda item: -item[1])[:4]
    total = sum(amount for _, amount in kept)
    whole = {bone: int(round(255 * amount / total)) for bone, amount in kept}
    largest = max(whole, key=whole.get)
    whole[largest] += 255 - sum(whole.values())
    return {bone: amount for bone, amount in whole.items() if amount}


class Crop:
    def __init__(self, raw, positions, normals, centre):
        self.raw, self.positions, self.normals, self.centre = raw, positions, normals, centre
        self.points, self.directions, self.weights, self.made = [], [], [], {}

    def corner(self, vertex):
        key = (int(vertex),)
        if key not in self.made:
            self.add(key, self.positions[vertex], self.normals[vertex], weights_of(self.raw, vertex))
        return self.made[key]

    def cut(self, first, second, share):
        key = (int(min(first, second)), int(max(first, second)))
        if key not in self.made:
            if first > second:
                first, second, share = second, first, 1 - share
            self.add(key, self.positions[first] * (1 - share) + self.positions[second] * share,
                     self.normals[first] * (1 - share) + self.normals[second] * share,
                     blended(weights_of(self.raw, first), weights_of(self.raw, second), share))
        return self.made[key]

    def add(self, key, point, direction, weights):
        self.made[key] = len(self.points)
        self.points.append(point)
        self.directions.append(direction / (np.linalg.norm(direction) or 1))
        self.weights.append(weights)

    def triangle(self, corners, heights):
        polygon = []
        for index in range(3):
            here, there = corners[index], corners[(index + 1) % 3]
            above_here, above_there = heights[index] >= 0, heights[(index + 1) % 3] >= 0
            if above_here:
                polygon.append(self.corner(here))
            if above_here != above_there:
                share = heights[index] / (heights[index] - heights[(index + 1) % 3])
                polygon.append(self.cut(here, there, share))
        return [(polygon[0], polygon[k], polygon[k + 1]) for k in range(1, len(polygon) - 1)]


def build(m2, skin):
    raw, positions, normals = vertex_table(m2)
    triangles = body_triangles(skin)
    triangles = triangles[(positions[triangles][:, :, 2] > NECK).all(axis=1)]
    centre = skull_centre(positions[np.unique(triangles)])
    heights = above_hairline(positions, centre)
    crop = Crop(raw, positions, normals, centre)
    faces = []
    for corners in triangles:
        level = heights[corners]
        if (level < 0).all():
            continue
        faces += crop.triangle(corners, level)
    points = np.array(crop.points)
    lift = LIFT * np.clip(above_hairline(points, centre) / LIFT_FADE, 0.2, 1.0)
    points = points + np.array(crop.directions) * lift[:, None]
    phi, beta = angles(points, centre)
    uv = np.c_[U_FROM + U_SPAN * np.abs(phi) / 180.0,
               CROWN_V + (NAPE_V - CROWN_V) * (90.0 - beta) / (90.0 - LOWEST)]
    return points, np.array(crop.directions), uv, crop.weights, np.array(faces)


def submesh_of(skin, geoset):
    count, offset = arr(skin, SKIN_SUBMESHES)
    for number in range(count):
        if struct.unpack_from("<H", skin, offset + number * SUBMESH_SIZE)[0] == geoset:
            return number
    return None


def with_crop(m2, skin, style):
    if submesh_of(skin, CROP) is not None:
        raise SystemExit("The skin already has hairstyle geoset 1")
    source = submesh_of(skin, style)
    points, directions, uv, weights, faces = build(m2, skin)
    bones = sorted({bone for weight in weights for bone in weight})
    slot = {bone: index for index, bone in enumerate(bones)}

    model = Blob(m2)
    vertex_count, vertex_offset = arr(m2, 60)
    added = bytearray()
    for point, direction, coords, weight in zip(points, directions, uv, weights):
        kept = sorted(weight.items())
        amounts = [amount for _, amount in kept] + [0] * (4 - len(kept))
        indices = [bone for bone, _ in kept] + [0] * (4 - len(kept))
        added += struct.pack("<3f4B4B3f2f2f", *point, *amounts, *indices, *direction, *coords, 0.0, 0.0)
    offset = model.append(m2[vertex_offset:vertex_offset + vertex_count * VERTEX_SIZE] + added)
    struct.pack_into("<II", model.data, 60, vertex_count + len(points), offset)
    lookup_count, lookup_offset = arr(m2, 120)
    combo = struct.pack(f"<{len(bones)}H", *bones)
    offset = model.append(m2[lookup_offset:lookup_offset + lookup_count * 2] + combo, 4)
    struct.pack_into("<II", model.data, 120, lookup_count + len(bones), offset)

    out = Blob(skin)
    arrays = {}
    for header, size in ((SKIN_VERTICES, 2), (SKIN_INDICES, 2), (SKIN_PROPERTIES, 4), (SKIN_SUBMESHES, SUBMESH_SIZE),
                         (SKIN_TEXTURE_UNITS, TEXTURE_UNIT_SIZE)):
        count, at = arr(skin, header)
        arrays[header] = bytearray(skin[at:at + count * size])
    first_vertex = len(arrays[SKIN_VERTICES]) // 2
    first_index = len(arrays[SKIN_INDICES]) // 2
    for number, weight in enumerate(weights):
        arrays[SKIN_VERTICES] += struct.pack("<H", vertex_count + number)
        local = sorted(slot[bone] for bone in weight)
        arrays[SKIN_PROPERTIES] += bytes(local + [0] * (4 - len(local)))
    arrays[SKIN_INDICES] += struct.pack(f"<{faces.size}H", *(first_vertex + faces.ravel()))
    section = bytearray(arrays[SKIN_SUBMESHES][source * SUBMESH_SIZE:(source + 1) * SUBMESH_SIZE])
    struct.pack_into("<10H", section, 0, CROP, first_index >> 16, first_vertex, len(points), first_index & 0xFFFF,
                     faces.size, len(bones), lookup_count, max(len(weight) for weight in weights), 0)
    centre = points.mean(axis=0)
    struct.pack_into("<3f", section, 20, *centre)
    struct.pack_into("<3f", section, 32, *centre)
    struct.pack_into("<f", section, 44, float(np.linalg.norm(points - centre, axis=1).max()))
    target = len(arrays[SKIN_SUBMESHES]) // SUBMESH_SIZE
    arrays[SKIN_SUBMESHES] += section
    units = arrays[SKIN_TEXTURE_UNITS]
    for number in range(len(units) // TEXTURE_UNIT_SIZE):
        if struct.unpack_from("<H", units, number * TEXTURE_UNIT_SIZE + 4)[0] == source:
            copy = bytearray(units[number * TEXTURE_UNIT_SIZE:(number + 1) * TEXTURE_UNIT_SIZE])
            struct.pack_into("<HH", copy, 4, target, target)
            units += copy
    for header, size in ((SKIN_VERTICES, 2), (SKIN_INDICES, 2), (SKIN_PROPERTIES, 4), (SKIN_SUBMESHES, SUBMESH_SIZE),
                         (SKIN_TEXTURE_UNITS, TEXTURE_UNIT_SIZE)):
        offset = out.append(bytes(arrays[header]), 16)
        struct.pack_into("<II", out.data, header, len(arrays[header]) // size, offset)
    return bytes(model.data), bytes(out.data), len(points), len(faces), len(bones)


def main(m2_path, skin_path, out_m2, out_skin, style="2"):
    m2, skin, vertices, faces, bones = with_crop(Path(m2_path).read_bytes(), Path(skin_path).read_bytes(),
                                                 int(style))
    Path(out_m2).write_bytes(m2)
    Path(out_skin).write_bytes(skin)
    print(f"geoset {CROP}: {faces} triangles, {vertices} vertices, {bones} bones")


if __name__ == "__main__":
    main(*sys.argv[1:])
