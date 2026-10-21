import struct
import sys
from pathlib import Path

import numpy as np


def grid(*ranges):
    return [np.array(point) for point in np.array(np.meshgrid(*ranges)).reshape(len(ranges), -1).T]


SEQUENCES, BONES, KEY_BONES = 0x1C, 0x2C, 0x34
TEXTURE_WEIGHTS, TEXTURE_TRANSFORMS = 0x58, 0x60
ATTACHMENTS, EVENTS, CAMERAS = 0xF0, 0x100, 0x110
SEQUENCE_SIZE, BONE_SIZE = 64, 88
BONE_PIVOT = 76
SEQUENCE_BOUNDS = slice(32, 60)
SEQUENCE_NEXT = 60
BONE_TRACKS = (16, 36, 56)
TRACK_VALUE_SIZES = (12, 8, 12)
ROTATION = 36
TRACK_SIZE = 20
RECORD_TRACKS = {TEXTURE_WEIGHTS: (20, (0,)), TEXTURE_TRANSFORMS: (60, (0, 20, 40)), ATTACHMENTS: (40, (20,)),
                 CAMERAS: (100, (16, 48, 80))}
EVENT_SIZE, EVENT_TRACK = 36, 24
KEY_ARM_LEFT, KEY_ARM_RIGHT = 0, 1
SPINE = (2, 4)
SPINE_PATH = (2, 4)
STAND = 0
FORWARD = (1.0, 0.0, 0.0)
HELD_BARREL = FORWARD
ATTACK_OFF_PIERCE = 88
READY_RIFLE, READY_BOW = 48, 29
HELD_AIM = 0.5
ATTACK_1H = 17
MACE_RAISED_MS = 300
MACE_SHAFT = tuple(np.array((-0.05, 1.0, -0.2)) / np.linalg.norm((-0.05, 1.0, -0.2)))
MACE_RESTING_REACH = (-0.2, -0.3, 0.175)
GUN_REACHES = grid(np.arange(0.2, 0.85, 0.1), np.arange(0.0, 0.45, 0.1), np.arange(-0.9, 0.05, 0.15))
FRONT_CLEARANCE = 0.06
LATERAL = (0.0, 1.0, 0.0)
SPIN_COCK = ((0, 0, 0), (70, -30, -12), (160, -10, -4), (260, 90, 10), (360, 180, 14), (460, 270, 10),
             (560, 360, 4), (720, 355, 0), (1000, 360, 0))
COPIED = ((49, READY_RIFLE, True, True, HELD_AIM), (49, 49, True, True, None), (49, 110, True, True, None),
          (109, READY_BOW, False, False, HELD_AIM), (46, 46, False, False, None), (109, 109, False, False, None),
          (49, ATTACK_OFF_PIERCE, True, True, None))
HOLD_LOOPS = 12
KICKED = (49, ATTACK_OFF_PIERCE)
LEFT_HAND_HOLDS = (17, 21, 26, 57, 31, 51, 52, 53, 54, 107, 108, 124, 125)
READY_1H = 26
OFF_HAND_SWINGS = (87,)
FIST_RAISED = (0.6, 0.3, 0.35)
FIST_DIPPED = (0.6, 0.3, 0.2)
TOSSED_UP = (0.5, -0.35, 0.8)
DRAWN_BACK = (0.15, -0.4, 0.55)
THROWN_FORWARD = (0.95, -0.15, 0.2)
TOWARD_HERSELF = (-0.5, 0.25, 0.85)
UPWARD = (0.0, 0.0, 1.0)
SPELL_LEFT_HAND, SPELL_RIGHT_HAND = 21, 22
SPELL_HOLDS = (31, 51, 52, 124, 125)
SPELL_HOLD = ((0.0, FIST_RAISED, TOWARD_HERSELF),)
SPELL_RELEASES = {
    54: ((0.0, FIST_RAISED, TOWARD_HERSELF), (0.25, FIST_DIPPED, TOWARD_HERSELF), (0.45, TOSSED_UP, UPWARD),
         (0.75, TOSSED_UP, UPWARD), (1.0, None, None)),
    53: ((0.0, FIST_RAISED, TOWARD_HERSELF), (0.25, DRAWN_BACK, UPWARD), (0.45, THROWN_FORWARD, FORWARD),
         (0.75, THROWN_FORWARD, FORWARD), (1.0, None, None))}


def array(data, offset):
    return struct.unpack_from("<II", data, offset)


class Blob:
    def __init__(self, data):
        self.data = bytearray(data)

    def append(self, payload, align=16):
        while len(self.data) % align:
            self.data.append(0)
        offset = len(self.data)
        self.data += payload
        return offset


def sequence_index(data, animation):
    count, offset = array(data, SEQUENCES)
    for index in range(count):
        found, variation = struct.unpack_from("<HH", data, offset + index * SEQUENCE_SIZE)
        if found == animation and variation == 0:
            return index
    raise SystemExit(f"no animation {animation}")


def bone_offset(data, bone):
    return array(data, BONES)[1] + bone * BONE_SIZE


def children(data):
    count, _ = array(data, BONES)
    found = {}
    for bone in range(count):
        parent = struct.unpack_from("<h", data, bone_offset(data, bone) + 8)[0]
        found.setdefault(parent, []).append(bone)
    return found


def arm_chain(data, key):
    kids = children(data)

    def size(bone):
        return 1 + sum(size(child) for child in kids.get(bone, []))

    count, offset = array(data, KEY_BONES)
    arm = struct.unpack_from(f"<{count}h", data, offset)[key]
    chain = [struct.unpack_from("<h", data, bone_offset(data, arm) + 8)[0], arm]
    for _ in range(2):
        chain.append(max(kids[chain[-1]], key=size))
    return chain


def decode(value):
    return (value + 32768) / 32767.0 if value < 0 else (value - 32767) / 32767.0


def encode(value):
    value = max(-1.0, min(1.0, value))
    return round(value * 32767) - 32768 if value >= 0 else round(value * 32767) + 32767


def unpacked(packed):
    return tuple(decode(v) for v in struct.unpack("<4h", packed))


def packed(quaternion, mirror):
    x, y, z, w = quaternion
    if mirror:
        x, z = -x, -z
    return struct.pack("<4h", encode(x), encode(y), encode(z), encode(w))


def product(first, second):
    x1, y1, z1, w1 = first
    x2, y2, z2, w2 = second
    return (w1 * x2 + x1 * w2 + y1 * z2 - z1 * y2, w1 * y2 - x1 * z2 + y1 * w2 + z1 * x2,
            w1 * z2 + x1 * y2 - y1 * x2 + z1 * w2, w1 * w2 - x1 * x2 - y1 * y2 - z1 * z2)


def bone_keys(data, bone, sequence):
    track = bone_offset(data, bone) + ROTATION
    times, values = struct.unpack_from("<II", data, track + 8)[0], struct.unpack_from("<I", data, track + 16)[0]
    count, at = array(data, times + sequence * 8)
    stamps = list(struct.unpack_from(f"<{count}I", data, at))
    count, at = array(data, values + sequence * 8)
    return stamps, [unpacked(data[at + index * 8:at + index * 8 + 8]) for index in range(count)]


def at_time(stamps, keys, time):
    if not keys:
        return (0.0, 0.0, 0.0, 1.0)
    earlier = [index for index, stamp in enumerate(stamps) if stamp <= time]
    return keys[earlier[-1] if earlier else 0]


def conjugate(quaternion):
    x, y, z, w = quaternion
    return (-x, -y, -z, w)


def rotate(quaternion, vector):
    return product(product(quaternion, (*vector, 0.0)), conjugate(quaternion))[:3]


def turning(start, end):
    cross = (start[1] * end[2] - start[2] * end[1], start[2] * end[0] - start[0] * end[2],
             start[0] * end[1] - start[1] * end[0])
    turn = (*cross, 1.0 + sum(a * b for a, b in zip(start, end)))
    length = sum(v * v for v in turn) ** 0.5
    return tuple(v / length for v in turn)


def unit(vector):
    length = sum(v * v for v in vector) ** 0.5
    return tuple(v / length for v in vector)


def parent_of(data, bone):
    return struct.unpack_from("<h", data, bone_offset(data, bone) + 8)[0]


def world_rotation(data, bone, sequence, time):
    rotation = (0.0, 0.0, 0.0, 1.0)
    while bone >= 0:
        rotation = product(at_time(*bone_keys(data, bone, sequence), time), rotation)
        bone = parent_of(data, bone)
    return rotation


def sequence_length(data, sequence):
    return struct.unpack_from("<I", data, array(data, SEQUENCES)[1] + sequence * SEQUENCE_SIZE + 4)[0]


def decoded(entry):
    count, stamps, values = entry
    return (list(struct.unpack(f"<{count}I", stamps)),
            [unpacked(values[index * 8:index * 8 + 8]) for index in range(count)])


def posed_rotation(ogre, bone, ready, time, posed):
    rotation = (0.0, 0.0, 0.0, 1.0)
    while bone >= 0:
        keys = posed[bone] if bone in posed else bone_keys(ogre, bone, ready)
        rotation = product(at_time(*keys, time), rotation)
        bone = parent_of(ogre, bone)
    return rotation


def aimed_joint(ogre, ready, chain, entries, axis, toward=FORWARD):
    posed = {bone: decoded(entry) for bone, entry in entries.items()}
    stamps, keys = posed[chain[0]]
    lower = [posed[bone] for bone in chain[1:]]
    aimed = []
    for stamp, key in zip(stamps, keys):
        above = posed_rotation(ogre, parent_of(ogre, chain[0]), ready, stamp, posed)
        hand = product(above, key)
        for bone_stamps, bone_keys_ in lower:
            hand = product(hand, at_time(bone_stamps, bone_keys_, stamp))
        correction = turning(unit(rotate(hand, axis)), unit(toward))
        aimed.append(packed(product(product(conjugate(above), product(correction, above)), key), False))
    return len(stamps), struct.pack(f"<{len(stamps)}I", *stamps), b"".join(aimed)


def as_matrix(quaternion):
    x, y, z, w = quaternion
    return np.array([[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
                     [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
                     [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]])


def as_quaternion(matrix):
    trace = matrix[0, 0] + matrix[1, 1] + matrix[2, 2]
    if trace > 0:
        s = (trace + 1.0) ** 0.5 * 2
        return ((matrix[2, 1] - matrix[1, 2]) / s, (matrix[0, 2] - matrix[2, 0]) / s,
                (matrix[1, 0] - matrix[0, 1]) / s, s / 4)
    axis = int(np.argmax(np.diag(matrix)))
    other, last = (axis + 1) % 3, (axis + 2) % 3
    s = (1.0 + matrix[axis, axis] - matrix[other, other] - matrix[last, last]) ** 0.5 * 2
    values = [0.0, 0.0, 0.0, 0.0]
    values[axis] = s / 4
    values[other] = (matrix[other, axis] + matrix[axis, other]) / s
    values[last] = (matrix[last, axis] + matrix[axis, last]) / s
    values[3] = (matrix[last, other] - matrix[other, last]) / s
    return tuple(values)


def about(axis, angle):
    axis = axis / np.linalg.norm(axis)
    half = angle / 2
    return as_matrix((*(axis * np.sin(half)), np.cos(half)))


def arc(start, end):
    return as_matrix(turning(unit(start), unit(end)))


def pivot(data, bone):
    return np.array(struct.unpack_from("<3f", data, bone_offset(data, bone) + BONE_PIVOT))


class Pose:
    def __init__(self, data, sequence, posed, time):
        self.data, self.sequence, self.posed, self.time = data, sequence, posed, time
        self.frames = {}

    def local(self, bone):
        keys = self.posed[bone] if bone in self.posed else bone_keys(self.data, bone, self.sequence)
        return as_matrix(at_time(*keys, self.time))

    def frame(self, bone):
        if bone < 0:
            return np.eye(3), np.zeros(3)
        if bone not in self.frames:
            rotation, shift = self.frame(parent_of(self.data, bone))
            local, place = self.local(bone), pivot(self.data, bone)
            self.frames[bone] = (rotation @ local, rotation @ (place - local @ place) + shift)
        return self.frames[bone]

    def joint(self, bone):
        rotation, shift = self.frame(parent_of(self.data, bone))
        return rotation @ pivot(self.data, bone) + shift


def arm_length(data, chain):
    return sum(np.linalg.norm(pivot(data, chain[index + 1]) - pivot(data, chain[index])) for index in (1, 2))


def reached(ogre, ready, source, sequence, entries, chain, source_chain):
    scale = arm_length(ogre, chain) / arm_length(source, source_chain)

    def tauren_hold(stamp):
        model = Pose(source, sequence, {}, stamp)
        source_chest = model.frame(parent_of(source, source_chain[0]))[0]
        return source_chest.T @ (model.joint(source_chain[3]) - model.joint(source_chain[1])) * \
            np.array((1.0, -1.0, 1.0)) * scale

    return bent_toward(ogre, ready, entries, chain, tauren_hold)


def pointed(ogre, ready, entries, chain, direction):
    reach = np.array(direction) * arm_length(ogre, chain)
    return bent_toward(ogre, ready, entries, chain, lambda stamp: reach)


def bent_toward(ogre, ready, entries, chain, reach_in_chest):
    posed = {bone: decoded(entry) for bone, entry in entries.items()}
    upper, fore, hand = chain[1:]
    stamps = posed[upper][0]
    uppers, fores = [], []
    for stamp in stamps:
        pose = Pose(ogre, ready, posed, stamp)
        chest = pose.frame(parent_of(ogre, chain[0]))[0]
        reach = chest @ reach_in_chest(stamp)
        shoulder, elbow, wrist = pose.joint(upper), pose.joint(fore), pose.joint(hand)
        above = pose.frame(parent_of(ogre, upper))[0]
        upper_world = above @ pose.local(upper)
        fore_world = upper_world @ pose.local(fore)
        first, second = np.linalg.norm(elbow - shoulder), np.linalg.norm(wrist - elbow)
        wanted = min(max(np.linalg.norm(reach), abs(first - second) + 1e-3), first + second - 1e-3)
        bend_axis = np.cross(shoulder - elbow, wrist - elbow)
        bend = np.eye(3)
        if np.linalg.norm(bend_axis) > 1e-6:
            current = np.arccos(np.clip(np.dot(unit(shoulder - elbow), unit(wrist - elbow)), -1, 1))
            target = np.arccos(np.clip((first ** 2 + second ** 2 - wanted ** 2) / (2 * first * second), -1, 1))
            bend = about(bend_axis, target - current)
        bent_wrist = elbow + bend @ (wrist - elbow)
        swing = arc(bent_wrist - shoulder, reach)
        uppers.append(packed(as_quaternion(above.T @ swing @ upper_world), False))
        fores.append(packed(as_quaternion(upper_world.T @ bend @ fore_world), False))
    times = struct.pack(f"<{len(stamps)}I", *stamps)
    entries = dict(entries)
    entries[upper] = (len(stamps), times, b"".join(uppers))
    entries[fore] = (len(stamps), times, b"".join(fores))
    return entries


def continuous(quaternion, previous):
    if previous is not None and sum(a * b for a, b in zip(quaternion, previous)) < 0:
        return tuple(-value for value in quaternion)
    return quaternion


def kicked(ogre, ready, entries, chain):
    posed = {bone: decoded(entry) for bone, entry in entries.items()}
    _, fore, hand = chain[1:]
    pose = Pose(ogre, ready, posed, 0)
    elbow_frame = pose.frame(parent_of(ogre, fore))[0]
    fore_world = elbow_frame @ pose.local(fore)
    hand_world = fore_world @ pose.local(hand)
    stamps, fores, hands = [], [], []
    fore_before = hand_before = None
    for stamp, wrist_degrees, elbow_degrees in SPIN_COCK:
        swing = about(np.array(LATERAL), np.radians(elbow_degrees))
        spin = about(np.array(LATERAL), np.radians(wrist_degrees))
        swung_fore = swing @ fore_world
        fore_before = continuous(as_quaternion(elbow_frame.T @ swung_fore), fore_before)
        hand_before = continuous(as_quaternion(swung_fore.T @ spin @ swing @ hand_world), hand_before)
        stamps.append(stamp)
        fores.append(packed(fore_before, False))
        hands.append(packed(hand_before, False))
    times = struct.pack(f"<{len(stamps)}I", *stamps)
    entries = dict(entries)
    entries[fore] = (len(stamps), times, b"".join(fores))
    entries[hand] = (len(stamps), times, b"".join(hands))
    return entries


def angle_between(first, second):
    return float(np.arccos(np.clip(np.dot(unit(first), unit(second)), -1.0, 1.0)))


def rolled(ogre, ready, entries, chain, toward):
    posed = {bone: decoded(entry) for bone, entry in entries.items()}
    _, fore, hand = chain[1:]
    stamps = posed[fore][0]
    fores, bend = [], 0.0
    for stamp in stamps:
        pose = Pose(ogre, ready, posed, stamp)
        elbow_frame = pose.frame(parent_of(ogre, fore))[0]
        fore_world = elbow_frame @ pose.local(fore)
        held = (fore_world @ pose.local(hand))[:, 0]
        along = np.array(unit(pose.joint(hand) - pose.joint(fore)))
        flat_held = held - np.dot(held, along) * along
        flat_wanted = np.array(toward) - np.dot(toward, along) * along
        roll = np.arctan2(np.dot(along, np.cross(flat_held, flat_wanted)), np.dot(flat_held, flat_wanted))
        turn = about(along, roll)
        bend = max(bend, angle_between(turn @ held, toward))
        fores.append(packed(as_quaternion(elbow_frame.T @ turn @ fore_world), False))
    entries = dict(entries)
    entries[fore] = (len(stamps), struct.pack(f"<{len(stamps)}I", *stamps), b"".join(fores))
    return entries, bend


def straightest(ogre, ready, entries, chain, toward, candidates, fits):
    best = None
    for reach in candidates:
        posed_entries, bend = rolled(ogre, ready, pointed(ogre, ready, entries, chain, reach), chain, toward)
        pose = Pose(ogre, ready, {bone: decoded(entry) for bone, entry in posed_entries.items()}, 0)
        if not fits(pose.joint(chain[3]), np.array(unit(toward))):
            continue
        if best is None or bend < best[0]:
            best = (bend, posed_entries)
    return best[1]


def finger_axis(ogre, chain):
    forearm = pivot(ogre, chain[3]) - pivot(ogre, chain[2])
    fingers = [bone for bone in children(ogre).get(chain[3], []) if children(ogre).get(bone)]
    longest = max(fingers, key=lambda bone: np.dot(pivot(ogre, bone) - pivot(ogre, chain[3]), forearm))
    return tuple(unit(pivot(ogre, longest) - pivot(ogre, chain[3])))


def raised_hand(ogre, ready, chain, frames, length):
    base = {bone: frozen(own_keys(ogre, bone, ready), 0) for bone in chain}
    fingers = finger_axis(ogre, chain)
    stamps, values = [], {bone: [] for bone in chain[1:]}
    previous = dict.fromkeys(chain[1:])
    for fraction, reach, pointing in frames:
        posed = base if reach is None else pointed(ogre, ready, base, chain, reach)
        if pointing is not None:
            posed[chain[3]] = aimed_joint(ogre, ready, chain[3:], posed, fingers, pointing)
        stamps.append(int(length * fraction))
        for bone in chain[1:]:
            previous[bone] = continuous(unpacked(posed[bone][2][:8]), previous[bone])
            values[bone].append(packed(previous[bone], False))
    times = struct.pack(f"<{len(stamps)}I", *stamps)
    return {bone: (len(stamps), times, b"".join(values[bone])) for bone in chain[1:]}


def own_keys(data, bone, sequence):
    track = bone_offset(data, bone) + ROTATION
    times, values = struct.unpack_from("<I", data, track + 8)[0], struct.unpack_from("<I", data, track + 16)[0]
    count, stamps_at = array(data, times + sequence * 8)
    _, values_at = array(data, values + sequence * 8)
    return count, bytes(data[stamps_at:stamps_at + count * 4]), bytes(data[values_at:values_at + count * 8])


def frozen(entry, moment):
    count, stamps, values = entry
    times = struct.unpack(f"<{count}I", stamps)
    earlier = [index for index, stamp in enumerate(times) if stamp <= moment]
    index = earlier[-1] if earlier else 0
    return 1, struct.pack("<I", 0), values[index * 8:index * 8 + 8]


def rotation_keys(data, chain, sequence, mirror):
    stamps, keys = bone_keys(data, chain[-1], sequence)
    parents = [bone_keys(data, bone, sequence) for bone in chain[:-1]]
    composed = []
    for stamp, key in zip(stamps, keys):
        rotation = (0.0, 0.0, 0.0, 1.0)
        for parent_stamps, parent_keys in parents:
            rotation = product(rotation, at_time(parent_stamps, parent_keys, stamp))
        composed.append(packed(product(rotation, key), mirror))
    return len(composed), struct.pack(f"<{len(stamps)}I", *stamps), b"".join(composed)


def bone_sources(ogre, source, mirror, left_arm_only, calm):
    ogre_sides = (arm_chain(ogre, KEY_ARM_LEFT), arm_chain(ogre, KEY_ARM_RIGHT))
    source_sides = (arm_chain(source, KEY_ARM_LEFT), arm_chain(source, KEY_ARM_RIGHT))
    if mirror:
        source_sides = source_sides[::-1]
    if left_arm_only:
        mapping = {bone: (source,) for bone, source in zip(ogre_sides[0][1:], source_sides[0][1:])}
        mapping[ogre_sides[0][0]] = (*SPINE_PATH, source_sides[0][0])
        return mapping
    mapping = {} if calm else {bone: (bone,) for bone in SPINE}
    for ogre_chain, source_chain in zip(ogre_sides, source_sides):
        mapping.update((bone, (source,)) for bone, source in zip(ogre_chain, source_chain))
    return mapping


class Body:
    def __init__(self, ogre, skin):
        from mkhelmets import CYCLOPS_HEAD, TWO_EYED_HEAD, bone_parents, load_mesh, subtree
        mesh = load_mesh(ogre, skin, {0})
        parents = bone_parents(ogre)
        arms = subtree(parents, {arm_chain(ogre, KEY_ARM_LEFT)[1], arm_chain(ogre, KEY_ARM_RIGHT)[1]})
        heads = subtree(parents, {CYCLOPS_HEAD, TWO_EYED_HEAD})
        self.torso = mesh.positions[~np.isin(mesh.bones, list(arms | heads))]
        self.heads = mesh.positions[np.isin(mesh.bones, list(heads))]

    def clears_front(self, hand, _):
        near = self.torso[(np.abs(self.torso[:, 1] - hand[1]) < 0.12) & (np.abs(self.torso[:, 2] - hand[2]) < 0.12)]
        return not len(near) or hand[0] >= near[:, 0].max() + FRONT_CLEARANCE


class Grafter:
    def __init__(self, ogre, source, skin):
        self.blob = Blob(ogre)
        self.source = source
        self.old_count = array(ogre, SEQUENCES)[0]
        self.base = sequence_index(ogre, STAND)
        self.body = Body(ogre, skin)
        self.added = []

    def looped(self, track, size):
        data = self.blob.data
        _, global_sequence, count, times = struct.unpack_from("<hhII", data, track)
        if global_sequence != -1 or count != self.old_count:
            return None
        values = struct.unpack_from("<I", data, track + 16)[0]
        keys, stamps_at = array(data, times + self.base * 8)
        _, values_at = array(data, values + self.base * 8)
        if not keys:
            return None
        length = sequence_length(bytes(data), self.base)
        stamps = struct.unpack_from(f"<{keys}I", data, stamps_at)
        looped_stamps, looped_values = [], bytearray()
        for loop in range(HOLD_LOOPS):
            for index, stamp in enumerate(stamps):
                if stamp >= length and loop < HOLD_LOOPS - 1:
                    continue
                looped_stamps.append(stamp + loop * length)
                looped_values += data[values_at + index * size:values_at + (index + 1) * size]
        return len(looped_stamps), struct.pack(f"<{len(looped_stamps)}I", *looped_stamps), bytes(looped_values)

    def extend(self, track, has_values, entries):
        data = self.blob.data
        _, global_sequence, count, times = struct.unpack_from("<hhII", data, track)
        if global_sequence != -1 or count != self.old_count:
            return
        outer_times = bytearray(data[times:times + count * 8])
        outer_values = bytearray()
        if has_values:
            values = struct.unpack_from("<I", data, track + 16)[0]
            outer_values = bytearray(data[values:values + count * 8])
        for entry in entries:
            if entry is None:
                outer_times += data[times + self.base * 8:times + self.base * 8 + 8]
                if has_values:
                    outer_values += data[values + self.base * 8:values + self.base * 8 + 8]
                continue
            keys, stamps, packed = entry
            outer_times += struct.pack("<II", keys, self.blob.append(stamps, 4))
            outer_values += struct.pack("<II", keys, self.blob.append(packed, 4))
        total = count + len(entries)
        struct.pack_into("<II", self.blob.data, track + 4, total, self.blob.append(bytes(outer_times), 4))
        if has_values:
            struct.pack_into("<II", self.blob.data, track + 12, total, self.blob.append(bytes(outer_values), 4))

    def graft(self):
        source_count, source_offset = array(self.source, SEQUENCES)
        wanted = [(sequence_index(self.source, animation), mirror) for animation, _, mirror, _, _ in COPIED]
        mappings = [bone_sources(bytes(self.blob.data), self.source, mirror, left_arm_only, held is not None)
                    for _, _, mirror, left_arm_only, held in COPIED]
        ogre = bytes(self.blob.data)
        left_arm = arm_chain(ogre, KEY_ARM_LEFT)
        right_arm = arm_chain(ogre, KEY_ARM_RIGHT)
        rotations = []
        for (sequence, mirror), mapping, (_, target, _, left_arm_only, held) in zip(wanted, mappings, COPIED):
            keys = {bone: rotation_keys(self.source, chain, sequence, mirror) for bone, chain in mapping.items()}
            if held is not None:
                moment = int(sequence_length(self.source, sequence) * held)
                keys = {bone: frozen(entry, moment) for bone, entry in keys.items()}
            if mirror and not left_arm_only:
                source_sides = (arm_chain(self.source, KEY_ARM_RIGHT), arm_chain(self.source, KEY_ARM_LEFT))
                for chain, source_chain in zip((left_arm, right_arm), source_sides):
                    keys = reached(ogre, self.base, self.source, sequence, keys, chain, source_chain)
            if left_arm_only:
                keys = straightest(ogre, self.base, keys, left_arm, FORWARD, GUN_REACHES, self.body.clears_front)
                attack = sequence_index(ogre, ATTACK_1H)
                keys.update((bone, frozen(own_keys(ogre, bone, attack), MACE_RAISED_MS)) for bone in right_arm
                            if own_keys(ogre, bone, attack)[0])
                keys, _ = rolled(ogre, self.base, pointed(ogre, self.base, keys, right_arm, MACE_RESTING_REACH),
                                 right_arm, MACE_SHAFT)
                keys[right_arm[-1]] = aimed_joint(ogre, self.base, right_arm[-1:], keys, HELD_BARREL, MACE_SHAFT)
            if mirror:
                keys[left_arm[-1]] = aimed_joint(ogre, self.base, left_arm[-1:], keys, HELD_BARREL)
            if target in KICKED:
                keys = kicked(ogre, self.base, keys, left_arm)
            rotations.append(keys)
        self.hold_left_hand_weapon(left_arm, right_arm, rotations[0])
        held = [entry[4] is not None for entry in COPIED]
        bone_count, _ = array(self.blob.data, BONES)
        for bone in range(bone_count):
            for track, size in zip(BONE_TRACKS, TRACK_VALUE_SIZES):
                place = bone_offset(self.blob.data, bone) + track
                entries = [keys.get(bone) if track == ROTATION else None for keys in rotations]
                entries = [self.looped(place, size) if entry is None and still else entry
                           for entry, still in zip(entries, held)]
                self.extend(place, True, entries)
        for header, (size, tracks) in RECORD_TRACKS.items():
            count, offset = array(self.blob.data, header)
            for record in range(count):
                for track in tracks:
                    self.extend(offset + record * size + track, True, [None] * len(wanted))
        count, offset = array(self.blob.data, EVENTS)
        for record in range(count):
            self.extend(offset + record * EVENT_SIZE + EVENT_TRACK, False, [None] * len(wanted))
        self.append_sequences(wanted, source_offset)
        self.cast_from_right_hand()
        return bytes(self.blob.data)

    def cast_from_right_hand(self):
        count, offset = array(self.blob.data, ATTACHMENTS)
        records = {struct.unpack_from("<I", self.blob.data, offset + index * 40)[0]: offset + index * 40
                   for index in range(count)}
        if SPELL_LEFT_HAND in records and SPELL_RIGHT_HAND in records:
            left, right = records[SPELL_LEFT_HAND], records[SPELL_RIGHT_HAND]
            self.blob.data[left + 4:left + 20] = self.blob.data[right + 4:right + 20]

    def set_own_rotation(self, bone, sequence, entry):
        data = self.blob.data
        track = bone_offset(data, bone) + ROTATION
        _, global_sequence, count, times = struct.unpack_from("<hhII", data, track)
        if global_sequence != -1 or count != self.old_count:
            return
        values = struct.unpack_from("<I", data, track + 16)[0]
        keys, stamps, packed_values = entry
        struct.pack_into("<II", self.blob.data, times + sequence * 8, keys, self.blob.append(stamps, 4))
        struct.pack_into("<II", self.blob.data, values + sequence * 8, keys, self.blob.append(packed_values, 4))

    def hold_left_hand_weapon(self, left_arm, right_arm, gun_hold):
        ogre = bytes(self.blob.data)
        _, offset = array(ogre, SEQUENCES)
        for sequence in range(self.old_count):
            animation = struct.unpack_from("<H", ogre, offset + sequence * SEQUENCE_SIZE)[0]
            if animation in LEFT_HAND_HOLDS:
                for bone in left_arm:
                    if bone in gun_hold:
                        self.set_own_rotation(bone, sequence, gun_hold[bone])
            if animation in OFF_HAND_SWINGS:
                ready = sequence_index(ogre, READY_1H)
                for bone in right_arm:
                    if own_keys(ogre, bone, ready)[0]:
                        self.set_own_rotation(bone, sequence, frozen(own_keys(ogre, bone, ready), 0))
            frames = SPELL_HOLD if animation in SPELL_HOLDS else SPELL_RELEASES.get(animation)
            if frames:
                length = sequence_length(ogre, sequence)
                for bone, entry in raised_hand(ogre, self.base, right_arm, frames, length).items():
                    self.set_own_rotation(bone, sequence, entry)

    def append_sequences(self, wanted, source_offset):
        data = self.blob.data
        _, offset = array(data, SEQUENCES)
        records = bytearray(data[offset:offset + self.old_count * SEQUENCE_SIZE])
        ready = data[offset + self.base * SEQUENCE_SIZE:offset + (self.base + 1) * SEQUENCE_SIZE]
        for position, (sequence, _) in enumerate(wanted):
            record = bytearray(self.source[source_offset + sequence * SEQUENCE_SIZE:
                                           source_offset + (sequence + 1) * SEQUENCE_SIZE])
            record[SEQUENCE_BOUNDS] = ready[SEQUENCE_BOUNDS]
            struct.pack_into("<H", record, 0, COPIED[position][1])
            if COPIED[position][4] is not None:
                struct.pack_into("<I", record, 4, sequence_length(bytes(data), self.base) * HOLD_LOOPS)
            struct.pack_into("<hH", record, SEQUENCE_NEXT, -1, self.old_count + position)
            records += record
        struct.pack_into("<II", self.blob.data, SEQUENCES, len(records) // SEQUENCE_SIZE,
                         self.blob.append(bytes(records), 16))


def graft(ogre_path, source_path, skin_path):
    ogre = Path(ogre_path)
    ogre.write_bytes(Grafter(ogre.read_bytes(), Path(source_path).read_bytes(), Path(skin_path).read_bytes()).graft())


if __name__ == "__main__":
    graft(sys.argv[1], sys.argv[2], sys.argv[3])
