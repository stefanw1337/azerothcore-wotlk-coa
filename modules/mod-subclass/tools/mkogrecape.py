"""Let the Two-Headed Ogre's capes drape over her hump instead of standing off her back like a board. Every cape
vertex is placed, in her standing pose, just behind the furthest-back point of her body at or above its height, the
surface is smoothed, and the result is turned back into the bind pose through the vertex's own bone weights, so the
cape keeps its bones, animations and texture.
    python mkogrecape.py <m2> <skin> <out m2>"""
import struct
import sys
from pathlib import Path

import numpy as np

import mkgunpose as g
from mkcape import arr

BODY, CAPES = 0, range(1502, 1507)
STAND = 0
VERTEX_SIZE, SUBMESH_SIZE = 48, 48
TORSO_HALF_WIDTH, BAND, REACH, LEVEL = 0.75, 0.30, 1.20, 0.06
CLEARANCE, FLARE, FLARE_DEPTH = 0.06, 0.12, 0.60
OVER_SHOULDER = 0.02
SMOOTHING = 8


def triangles(skin, geosets):
    lookup_count, lookup_offset = arr(skin, 4)
    lookup = np.frombuffer(skin, np.uint16, lookup_count, lookup_offset).astype(np.int64)
    index_count, index_offset = arr(skin, 12)
    corners = np.frombuffer(skin, np.uint16, index_count, index_offset).astype(np.int64)
    count, offset = arr(skin, 28)
    found = []
    for number in range(count):
        geoset, level, _, _, start, size = struct.unpack_from("<6H", skin, offset + number * SUBMESH_SIZE)
        if geoset in geosets:
            first = start | level << 16
            found.append(lookup[corners[first:first + size]].reshape(-1, 3))
    return np.concatenate(found)


def vertices(m2):
    count, offset = arr(m2, 60)
    raw = np.frombuffer(m2, np.uint8, count * VERTEX_SIZE, offset).reshape(-1, VERTEX_SIZE)
    positions = raw[:, 0:12].copy().view(np.float32).reshape(-1, 3).astype(np.float64)
    normals = raw[:, 20:32].copy().view(np.float32).reshape(-1, 3).astype(np.float64)
    return positions, normals, raw[:, 12:16].astype(np.float64) / 255.0, raw[:, 16:20].astype(np.int64)


def skinning(pose, weights, bones, index):
    rotation, shift = np.zeros((3, 3)), np.zeros(3)
    for bone, weight in zip(bones[index], weights[index]):
        if weight:
            bone_rotation, bone_shift = pose.frame(int(bone))
            rotation += weight * bone_rotation
            shift += weight * bone_shift
    return rotation, shift


def draped(standing, torso, cape):
    target, behind = standing.copy(), {}
    for index in cape:
        x, y, z = standing[index]
        column = torso[np.abs(torso[:, 1] - y) < BAND]
        above = column[(column[:, 2] >= z - 0.05) & (column[:, 2] <= z + REACH)]
        if not len(above):
            continue
        level = column[np.abs(column[:, 2] - z) < LEVEL]
        envelope = above[:, 0].min()
        here = level[:, 0].min() if len(level) else envelope
        if x > here + OVER_SHOULDER:
            continue
        hang = min(1.0, max(0.0, here - envelope) / FLARE_DEPTH)
        behind[index] = envelope - CLEARANCE
        target[index, 0] = behind[index] - FLARE * hang ** 1.3
    return target, behind


def smoothed(target, behind, standing, cape, faces):
    place = {index: tuple(np.round(standing[index], 4)) for index in cape}
    twins = {}
    for index in cape:
        twins.setdefault(place[index], []).append(index)
    neighbours = {index: set() for index in cape}
    for a, b, c in faces:
        for first, second in ((a, b), (b, c), (c, a)):
            neighbours[first].add(second)
            neighbours[second].add(first)
    for _ in range(SMOOTHING):
        moved = {}
        for index in behind:
            ring = set().union(*(neighbours[twin] for twin in twins[place[index]])) - set(twins[place[index]])
            if ring:
                average = 0.5 * target[index, 0] + 0.5 * np.mean([target[other, 0] for other in ring])
                moved[index] = min(average, behind[index])
        for index, x in moved.items():
            for twin in twins[place[index]]:
                target[twin, 0] = x
    return target


def with_draped_capes(m2, skin):
    positions, normals, weights, bones = vertices(m2)
    pose = g.Pose(m2, g.sequence_index(m2, STAND), {}, 0)
    body = np.unique(triangles(skin, {BODY}))
    faces = triangles(skin, set(CAPES))
    cape = np.unique(faces)
    standing = positions.copy()
    for index in np.concatenate([body, cape]):
        rotation, shift = skinning(pose, weights, bones, index)
        standing[index] = rotation @ positions[index] + shift
    torso = standing[body]
    torso = torso[np.abs(torso[:, 1]) < TORSO_HALF_WIDTH]
    target, behind = draped(standing, torso, cape)
    target = smoothed(target, behind, standing, cape, faces)
    bind = positions.copy()
    for index in cape:
        rotation, shift = skinning(pose, weights, bones, index)
        bind[index] = np.linalg.solve(rotation, target[index] - shift)
    face_normals = np.cross(bind[faces[:, 1]] - bind[faces[:, 0]], bind[faces[:, 2]] - bind[faces[:, 0]])
    summed = np.zeros_like(bind)
    for corner in range(3):
        np.add.at(summed, faces[:, corner], face_normals)
    data = bytearray(m2)
    _, offset = arr(m2, 60)
    for index in cape:
        normal = summed[index] / (np.linalg.norm(summed[index]) or 1.0)
        if normal @ normals[index] < 0:
            normal = -normal
        struct.pack_into("<3f", data, offset + index * VERTEX_SIZE, *bind[index])
        struct.pack_into("<3f", data, offset + index * VERTEX_SIZE + 20, *normal)
    return bytes(data), len(behind), len(cape)


def main(m2_path, skin_path, out_m2):
    data, moved, total = with_draped_capes(Path(m2_path).read_bytes(), Path(skin_path).read_bytes())
    Path(out_m2).write_bytes(data)
    print(f"capes: {moved} of {total} vertices draped over the back")


if __name__ == "__main__":
    main(*sys.argv[1:])
