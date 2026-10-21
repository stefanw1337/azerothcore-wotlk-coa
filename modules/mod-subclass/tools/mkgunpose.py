import struct
import sys
from pathlib import Path

import numpy as np

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
SPECIAL_UNARMED, ATTACK_UNARMED = 118, 16
HELD_AIM = 0.5
COPIED = ((48, 48, True, False, None), (49, 49, True, False, None), (110, 110, True, False, None),
          (29, 29, False, False, None), (46, 46, False, False, None), (109, 109, False, False, None),
          (49, ATTACK_OFF_PIERCE, True, True, None), (110, ATTACK_UNARMED, True, False, HELD_AIM),
          (109, SPECIAL_UNARMED, False, False, HELD_AIM))
HOLD_LOOPS = 12


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


def aimed_joint(ogre, ready, chain, entries, axis):
    posed = {bone: decoded(entry) for bone, entry in entries.items()}
    stamps, keys = posed[chain[0]]
    lower = [posed[bone] for bone in chain[1:]]
    aimed = []
    for stamp, key in zip(stamps, keys):
        above = posed_rotation(ogre, parent_of(ogre, chain[0]), ready, stamp, posed)
        hand = product(above, key)
        for bone_stamps, bone_keys_ in lower:
            hand = product(hand, at_time(bone_stamps, bone_keys_, stamp))
        correction = turning(unit(rotate(hand, axis)), FORWARD)
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
    posed = {bone: decoded(entry) for bone, entry in entries.items()}
    upper, fore, hand = chain[1:]
    scale = arm_length(ogre, chain) / arm_length(source, source_chain)
    stamps = posed[upper][0]
    uppers, fores = [], []
    for stamp in stamps:
        model = Pose(source, sequence, {}, stamp)
        source_chest = model.frame(parent_of(source, source_chain[0]))[0]
        held = source_chest.T @ (model.joint(source_chain[3]) - model.joint(source_chain[1]))
        pose = Pose(ogre, ready, posed, stamp)
        chest = pose.frame(parent_of(ogre, chain[0]))[0]
        reach = chest @ (held * np.array((1.0, -1.0, 1.0))) * scale
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


class Grafter:
    def __init__(self, ogre, source):
        self.blob = Blob(ogre)
        self.source = source
        self.old_count = array(ogre, SEQUENCES)[0]
        self.base = sequence_index(ogre, STAND)
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
        for (sequence, mirror), mapping, (_, _, _, left_arm_only, held) in zip(wanted, mappings, COPIED):
            keys = {bone: rotation_keys(self.source, chain, sequence, mirror) for bone, chain in mapping.items()}
            if held is not None:
                moment = int(sequence_length(self.source, sequence) * held)
                keys = {bone: frozen(entry, moment) for bone, entry in keys.items()}
            if mirror and not left_arm_only:
                source_sides = (arm_chain(self.source, KEY_ARM_RIGHT), arm_chain(self.source, KEY_ARM_LEFT))
                for chain, source_chain in zip((left_arm, right_arm), source_sides):
                    keys = reached(ogre, self.base, self.source, sequence, keys, chain, source_chain)
            if mirror:
                keys[left_arm[-1]] = aimed_joint(ogre, self.base, left_arm[-1:], keys, HELD_BARREL)
            rotations.append(keys)
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
        return bytes(self.blob.data)

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


def graft(ogre_path, source_path):
    ogre = Path(ogre_path)
    ogre.write_bytes(Grafter(ogre.read_bytes(), Path(source_path).read_bytes()).graft())


if __name__ == "__main__":
    graft(sys.argv[1], sys.argv[2])
