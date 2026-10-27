"""Let the Two-Headed Ogre's capes drape over her hump instead of standing off her back like a board, without her
body poking through them while she stands, shuffles or readies a weapon. Every cape vertex is moved, in the space
of its own bones, just behind the furthest-back point her body reaches at or above its height in any frame of those
animations (her legs count only while she stands); the surface is then smoothed. Walking, running and casting swing
the cape on its own bones as before. The cape keeps its bones, animations and texture.
    python mkogrecape.py <m2> <skin> <out m2>
    python mkogrecape.py --check <m2> <skin>
--check prints, per animation, how far her body reaches through the capes."""
import struct
import sys
from pathlib import Path

import numpy as np

import mkgunpose as g
from mkcape import arr

CAPES = range(1502, 1507)
VERTEX_SIZE, SUBMESH_SIZE, SEQUENCE_SIZE, BONE_SIZE = 48, 48, 64, 88
TRANSLATION, ROTATION, PIVOT = 16, 36, 76
STAND = 0
CLEAR_OF = (STAND, 8, 11, 12, 14, 25, 26, 27, 28, 31, 52, 89, 90)
HIP_SIDE = 0.15
SAMPLES = 8
BAND, REACH, LEVEL, BELOW = 0.30, 1.20, 0.06, 0.05
CLEARANCE, FLARE, FLARE_DEPTH = 0.06, 0.12, 0.60
OVER_SHOULDER = 0.02
SMOOTHING = 8
CHECK_BAND = 0.10
TOP_ROW_V, PEAK_HALF, PEAK_DROP = 0.01, 0.05, 0.02
EASE_STEPS = 2


def triangles(skin, keep):
    lookup_count, lookup_offset = arr(skin, 4)
    lookup = np.frombuffer(skin, np.uint16, lookup_count, lookup_offset).astype(np.int64)
    index_count, index_offset = arr(skin, 12)
    corners = np.frombuffer(skin, np.uint16, index_count, index_offset).astype(np.int64)
    count, offset = arr(skin, 28)
    found = []
    for number in range(count):
        geoset, level, _, _, start, size = struct.unpack_from("<6H", skin, offset + number * SUBMESH_SIZE)
        if keep(geoset):
            first = start | level << 16
            found.append(lookup[corners[first:first + size]].reshape(-1, 3))
    return np.concatenate(found)


def vertices(m2):
    count, offset = arr(m2, 60)
    raw = np.frombuffer(m2, np.uint8, count * VERTEX_SIZE, offset).reshape(-1, VERTEX_SIZE)
    positions = raw[:, 0:12].copy().view(np.float32).reshape(-1, 3).astype(np.float64)
    normals = raw[:, 20:32].copy().view(np.float32).reshape(-1, 3).astype(np.float64)
    return positions, normals, raw[:, 12:16].astype(np.float64) / 255.0, raw[:, 16:20].astype(np.int64)


def texture_coordinates(m2):
    count, offset = arr(m2, 60)
    raw = np.frombuffer(m2, np.uint8, count * VERTEX_SIZE, offset).reshape(-1, VERTEX_SIZE)
    return raw[:, 32:40].copy().view(np.float32).reshape(-1, 2).astype(np.float64)


def key_at(m2, bone, track, size, layout, sequence, time):
    at = g.bone_offset(m2, bone) + track
    _, global_sequence, time_tables, times, value_tables, values = struct.unpack_from("<hhIIII", m2, at)
    if global_sequence != -1 or sequence >= time_tables:
        return None
    count, stamps_at = arr(m2, times + sequence * 8)
    if not count:
        return None
    stamps = struct.unpack_from(f"<{count}I", m2, stamps_at)
    _, values_at = arr(m2, values + sequence * 8)
    index = max([number for number, stamp in enumerate(stamps) if stamp <= time] or [0])
    return struct.unpack_from(layout, m2, values_at + index * size)


class Pose:
    def __init__(self, m2, sequence, time):
        self.m2, self.sequence, self.time, self.frames = m2, sequence, time, {}

    def frame(self, bone):
        if bone < 0:
            return np.eye(3), np.zeros(3)
        if bone not in self.frames:
            parent_rotation, parent_shift = self.frame(g.parent_of(self.m2, bone))
            turn = key_at(self.m2, bone, ROTATION, 8, "<4h", self.sequence, self.time)
            rotation = g.as_matrix(g.unpacked(struct.pack("<4h", *turn))) if turn else np.eye(3)
            moved = key_at(self.m2, bone, TRANSLATION, 12, "<3f", self.sequence, self.time)
            pivot = g.pivot(self.m2, bone)
            local = pivot + (np.array(moved) if moved else 0.0) - rotation @ pivot
            self.frames[bone] = (parent_rotation @ rotation, parent_rotation @ local + parent_shift)
        return self.frames[bone]


def frames(m2):
    count, offset = arr(m2, 28)
    for sequence in range(count):
        animation, _, length = struct.unpack_from("<HHI", m2, offset + sequence * SEQUENCE_SIZE)
        if animation in CLEAR_OF:
            for sample in range(SAMPLES):
                yield animation, Pose(m2, sequence, length * sample // SAMPLES)


def skinned(points, weights, bones, pose):
    out = np.zeros_like(points)
    for slot in range(4):
        for bone in np.unique(bones[:, slot]):
            chosen = (bones[:, slot] == bone) & (weights[:, slot] > 0)
            if chosen.any():
                rotation, shift = pose.frame(int(bone))
                out[chosen] += weights[chosen, slot:slot + 1] * (points[chosen] @ rotation.T + shift)
    return out


def blend(pose, weights, bones):
    rotation, shift = np.zeros((3, 3)), np.zeros(3)
    for bone, weight in zip(bones, weights):
        if weight:
            bone_rotation, bone_shift = pose.frame(int(bone))
            rotation += weight * bone_rotation
            shift += weight * bone_shift
    return rotation, shift


def arms(m2):
    return set().union(*(g.subtree_of(m2, g.arm_chain(m2, key)[1]) for key in (g.KEY_ARM_LEFT, g.KEY_ARM_RIGHT)))


def legs(m2):
    count, offset = arr(m2, g.KEY_BONES)
    waist = struct.unpack_from(f"<{count}h", m2, offset)[g.KEY_WAIST]
    thighs = [bone for bone in g.children(m2).get(waist, []) if abs(g.pivot(m2, bone)[1]) > HIP_SIDE]
    return set().union(*(g.subtree_of(m2, bone) for bone in thighs))


def body_seen_by_cape(m2, skin):
    positions, normals, weights, bones = vertices(m2)
    body = np.unique(triangles(skin, lambda geoset: geoset not in CAPES))
    arm_bones = arms(m2)
    dominant = bones[np.arange(len(bones)), np.argmax(weights, axis=1)]
    body = body[~np.isin(dominant[body], list(arm_bones))]
    torso = body[~np.isin(dominant[body], list(legs(m2)))]
    cape = np.unique(triangles(skin, lambda geoset: geoset in CAPES))
    groups = {}
    for index in cape:
        groups.setdefault((tuple(bones[index]), tuple(weights[index])), []).append(index)
    seen = {key: [] for key in groups}
    by_animation = {}
    for animation, pose in frames(m2):
        clear = body if animation == STAND else torso
        posed = skinned(positions[clear], weights[clear], bones[clear], pose)
        for key in groups:
            rotation, shift = blend(pose, key[1], key[0])
            local = np.linalg.solve(rotation, (posed - shift).T).T
            seen[key].append(local)
            by_animation.setdefault(animation, {}).setdefault(key, []).append(local)
    cloud = {key: mirrored(np.concatenate(parts)) for key, parts in seen.items()}
    return positions, normals, cape, groups, cloud, by_animation


def mirrored(points):
    return np.concatenate([points, points * np.array([1.0, -1.0, 1.0])])


def column(points, y):
    return points[np.abs(points[:, 1] - y) < BAND]


def neighbours_of(cape, faces):
    neighbours = {index: set() for index in cape}
    for a, b, c in faces:
        for first, second in ((a, b), (b, c), (c, a)):
            neighbours[first].add(second)
            neighbours[second].add(first)
    return neighbours


def draped(positions, cape, groups, cloud, neighbours):
    target, behind = positions.copy(), {}
    for key, members in groups.items():
        for index in members:
            x, y, z = positions[index]
            lowest = min([positions[other, 2] for other in neighbours[index]] + [z - BELOW])
            near = column(cloud[key], y)
            above = near[(near[:, 2] >= lowest) & (near[:, 2] <= z + REACH)]
            if not len(above):
                continue
            level = near[np.abs(near[:, 2] - z) < LEVEL]
            envelope = above[:, 0].min()
            here = level[:, 0].min() if len(level) else envelope
            if x > here + OVER_SHOULDER:
                continue
            hang = min(1.0, max(0.0, here - envelope) / FLARE_DEPTH)
            behind[index] = envelope - CLEARANCE
            target[index, 0] = behind[index] - FLARE * hang ** 1.3
    return target, behind


def eased_from_shoulders(bind, positions, cape, behind, neighbours):
    distance = {index: 0 for index in cape if index not in behind}
    frontier = list(distance)
    while frontier:
        reached = []
        for index in frontier:
            for other in neighbours[index]:
                if other not in distance:
                    distance[other] = distance[index] + 1
                    reached.append(other)
        frontier = reached
    eased = positions.copy()
    for index in cape:
        share = min(1.0, max(0.0, (distance.get(index, EASE_STEPS + 1) - 1) / EASE_STEPS))
        eased[index] = positions[index] + share * (bind[index] - positions[index])
    return eased


def flat_top(bind, cape, uv, neighbours):
    top = {index for index in cape if uv[index, 1] <= TOP_ROW_V}
    for index in top:
        sides = [other for other in neighbours[index] if other in top and abs(bind[other, 1]) >= PEAK_HALF]
        if abs(bind[index, 1]) < PEAK_HALF and sides:
            bind[index, 2] = min(bind[index, 2], np.mean(bind[sides, 2]) - PEAK_DROP)
    return bind


def smoothed(target, behind, positions, cape, neighbours):
    place = {index: tuple(np.round(positions[index], 4)) for index in cape}
    twins = {}
    for index in cape:
        twins.setdefault(place[index], []).append(index)
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


def reach_through(positions, groups, clouds, draped_part):
    worst = 0.0
    for key, members in groups.items():
        if key not in clouds:
            continue
        points = clouds[key]
        for index in members:
            if index not in draped_part:
                continue
            x, y, z = positions[index]
            near = points[(np.abs(points[:, 1] - y) < CHECK_BAND) & (np.abs(points[:, 2] - z) < CHECK_BAND)]
            if len(near):
                worst = max(worst, x - near[:, 0].min())
    return worst


def with_draped_capes(m2, skin):
    positions, normals, cape, groups, cloud, _ = body_seen_by_cape(m2, skin)
    faces = triangles(skin, lambda geoset: geoset in CAPES)
    neighbours = neighbours_of(cape, faces)
    target, behind = draped(positions, cape, groups, cloud, neighbours)
    bind = smoothed(target, behind, positions, cape, neighbours)
    bind = eased_from_shoulders(bind, positions, cape, behind, neighbours)
    bind = flat_top(bind, cape, texture_coordinates(m2), neighbours)
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


def check(m2, skin):
    positions, _, cape, groups, cloud, by_animation = body_seen_by_cape(m2, skin)
    neighbours = neighbours_of(cape, triangles(skin, lambda geoset: geoset in CAPES))
    _, draped_part = draped(positions, cape, groups, cloud, neighbours)
    for animation, seen in sorted(by_animation.items()):
        clouds = {key: np.concatenate(parts) for key, parts in seen.items()}
        reach = reach_through(positions, groups, clouds, draped_part)
        print(f"animation {animation:3d}: body reaches {reach:+.3f} through")


def main(*arguments):
    if arguments[0] == "--check":
        check(Path(arguments[1]).read_bytes(), Path(arguments[2]).read_bytes())
        return
    m2_path, skin_path, out_m2 = arguments
    data, moved, total = with_draped_capes(Path(m2_path).read_bytes(), Path(skin_path).read_bytes())
    Path(out_m2).write_bytes(data)
    print(f"capes: {moved} of {total} vertices draped clear of her body")


if __name__ == "__main__":
    main(*sys.argv[1:])
