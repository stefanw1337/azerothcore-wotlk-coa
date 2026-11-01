import argparse
import math
import struct
from pathlib import Path

SEQUENCES, SEQUENCE_LOOKUP, BONES, KEY_BONES, VERTICES = 0x1C, 0x24, 0x2C, 0x34, 0x3C
VERTEX_SIZE, SKIN_SAMPLES = 48, 6000
TEXTURE_WEIGHTS, TEXTURE_TRANSFORMS = 0x58, 0x60
ATTACHMENTS, EVENTS, CAMERAS = 0xF0, 0x100, 0x110
SEQUENCE_SIZE, BONE_SIZE = 64, 88
BONE_PARENT, BONE_PIVOT = 8, 76
TRANSLATION, ROTATION, SCALE = 16, 36, 56
VALUE_SIZES = {TRANSLATION: 12, ROTATION: 8, SCALE: 12}
RECORD_TRACKS = {TEXTURE_WEIGHTS: (20, (0,)), TEXTURE_TRANSFORMS: (60, (0, 20, 40)), ATTACHMENTS: (40, (20,)),
                 CAMERAS: (100, (16, 48, 80))}
EVENT_SIZE, EVENT_TRACK = 36, 24
SEQUENCE_DURATION, SEQUENCE_MOVE_SPEED, SEQUENCE_FLAGS = 4, 8, 12
SEQUENCE_BLEND, SEQUENCE_BOUNDS, SEQUENCE_NEXT = 28, 32, 60
EMBEDDED = 0x20
KEY_ARM_LEFT, KEY_ARM_RIGHT, KEY_ROOT = 0, 1, 26
INDEX_FINGERS = (13, 8)

STAND, JUMP_START, JUMP, JUMP_END = 0, 37, 38, 39
HANDSPRING, BACK_HANDSPRING = 1792, 1794
FLY_HANDSPRING, FLY_BACK_HANDSPRING = 1793, 1795
FLY_JUMP_START, FLY_JUMP = 266, 267
DURATION_MS, KEY_STEP_MS, BLEND_MS = 700, 25, 100
TURN_START, TURN_END = 0.08, 0.82
REACH_START, REACH_HELD, REACH_RELEASED, REACH_END = 0.04, 0.2, 0.62, 0.86
HOP = 0.25
LOOP_CLOSE = 0.86
FLIGHT_START, FLIGHT_END = 0.5, 0.8
GROUND_CLEARANCE = 0.03
BOUNDS_GROWTH = 1.6
FORWARD_REACH, REACH_SPREAD = 0.3, 0.0
PLANT_AHEAD = 1.2
HANG_AHEAD = 0.15
PLANT_START, PLANT_HELD, PLANT_RELEASED, PLANT_END = 0.14, 0.3, 0.45, 0.6
ANIMATION_FIELDS, KIT_FIELDS, KIT_ANIMATION = 8, 38, 2
ANIMATION_WEAPON_FLAGS, WEAPONS_SHEATHED = 2, 4
VAULT_DUST_KIT = 963356
VAULT_KITS = {3455900: HANDSPRING, 3455901: BACK_HANDSPRING}
ANIMATION_NAMES = {HANDSPRING: "Handspring", FLY_HANDSPRING: "FlyHandspring",
                   BACK_HANDSPRING: "BackHandspring", FLY_BACK_HANDSPRING: "FlyBackHandspring"}


def counted(data, offset):
    return struct.unpack_from("<II", data, offset)


def product(first, second):
    x1, y1, z1, w1 = first
    x2, y2, z2, w2 = second
    return (w1 * x2 + x1 * w2 + y1 * z2 - z1 * y2, w1 * y2 - x1 * z2 + y1 * w2 + z1 * x2,
            w1 * z2 + x1 * y2 - y1 * x2 + z1 * w2, w1 * w2 - x1 * x2 - y1 * y2 - z1 * z2)


def conjugate(quaternion):
    x, y, z, w = quaternion
    return (-x, -y, -z, w)


def rotate(quaternion, vector):
    return product(product(quaternion, (*vector, 0.0)), conjugate(quaternion))[:3]


def unit(vector):
    length = math.sqrt(sum(value * value for value in vector))
    return tuple(value / length for value in vector)


def normalized(quaternion):
    return unit(quaternion)


def turning(start, end):
    start, end = unit(start), unit(end)
    dot = sum(a * b for a, b in zip(start, end))
    if dot < -0.9999:
        side = (0.0, 1.0, 0.0) if abs(start[1]) < 0.9 else (1.0, 0.0, 0.0)
        axis = unit((start[1] * side[2] - start[2] * side[1], start[2] * side[0] - start[0] * side[2],
                     start[0] * side[1] - start[1] * side[0]))
        return (*axis, 0.0)
    cross = (start[1] * end[2] - start[2] * end[1], start[2] * end[0] - start[0] * end[2],
             start[0] * end[1] - start[1] * end[0])
    return normalized((*cross, 1.0 + dot))


def about(axis, angle):
    half = angle / 2.0
    return (*(value * math.sin(half) for value in unit(axis)), math.cos(half))


def slerp(first, second, share):
    dot = sum(a * b for a, b in zip(first, second))
    if dot < 0.0:
        second, dot = tuple(-value for value in second), -dot
    if dot > 0.9995:
        return normalized(tuple(a + (b - a) * share for a, b in zip(first, second)))
    angle = math.acos(dot)
    weight_first = math.sin((1.0 - share) * angle) / math.sin(angle)
    weight_second = math.sin(share * angle) / math.sin(angle)
    return normalized(tuple(a * weight_first + b * weight_second for a, b in zip(first, second)))


def decoded(value):
    return (value + 32768) / 32767.0 if value < 0 else (value - 32767) / 32767.0


def encoded(value):
    value = max(-1.0, min(1.0, value))
    return round(value * 32767) - 32768 if value >= 0 else round(value * 32767) + 32767


def unpack_value(track, raw):
    if track == ROTATION:
        return normalized(tuple(decoded(value) for value in struct.unpack("<4h", raw)))
    return struct.unpack("<3f", raw)


def pack_value(track, value):
    if track == ROTATION:
        return struct.pack("<4h", *(encoded(component) for component in value))
    return struct.pack("<3f", *value)


def resting(track):
    return {TRANSLATION: (0.0, 0.0, 0.0), ROTATION: (0.0, 0.0, 0.0, 1.0), SCALE: (1.0, 1.0, 1.0)}[track]


def smoothstep(share):
    share = max(0.0, min(1.0, share))
    return share * share * (3.0 - 2.0 * share)


def window(time, start, end):
    return smoothstep((time - start) / (end - start))


def smoothed_continuity(previous, quaternion):
    if previous is not None and sum(a * b for a, b in zip(previous, quaternion)) < 0.0:
        return tuple(-value for value in quaternion)
    return quaternion


def rotation_matrix(quaternion):
    x, y, z, w = quaternion
    return ((1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)),
            (2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)),
            (2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)))


def bone_matrix(pivot, translation, rotation, scale):
    turned = rotation_matrix(rotation)
    linear = tuple(tuple(turned[row][column] * scale[column] for column in range(3)) for row in range(3))
    offset = tuple(pivot[row] + translation[row] - sum(linear[row][column] * pivot[column] for column in range(3))
                   for row in range(3))
    return linear, offset


def combined(outer, inner):
    outer_linear, outer_offset = outer
    inner_linear, inner_offset = inner
    linear = tuple(tuple(sum(outer_linear[row][k] * inner_linear[k][column] for k in range(3)) for column in range(3))
                   for row in range(3))
    return linear, transformed(outer, inner_offset)


def transformed(matrix, point):
    linear, offset = matrix
    return tuple(offset[row] + sum(linear[row][column] * point[column] for column in range(3)) for row in range(3))


class Model:
    def __init__(self, data):
        self.data = bytearray(data)
        self.sequence_count, self.sequence_offset = counted(self.data, SEQUENCES)
        self.bone_count, self.bone_offset = counted(self.data, BONES)
        sequences = {}
        for index in range(self.sequence_count):
            animation, variation = struct.unpack_from("<HH", self.data, self.sequence_offset + index * SEQUENCE_SIZE)
            if variation == 0:
                sequences.setdefault(animation, index)
        self.sequences = sequences
        self.parents = [struct.unpack_from("<h", self.data, self.bone(bone) + BONE_PARENT)[0]
                        for bone in range(self.bone_count)]
        self.skinned = None
        self.pivots = [struct.unpack_from("<3f", self.data, self.bone(bone) + BONE_PIVOT)
                       for bone in range(self.bone_count)]

    def bone(self, index):
        return self.bone_offset + index * BONE_SIZE

    def key_bone(self, key):
        count, offset = counted(self.data, KEY_BONES)
        if key >= count:
            return -1
        return struct.unpack_from("<h", self.data, offset + key * 2)[0]

    def children(self, bone):
        return [child for child, parent in enumerate(self.parents) if parent == bone]

    def subtree_size(self, bone):
        return 1 + sum(self.subtree_size(child) for child in self.children(bone))

    def arm(self, key):
        arm = self.key_bone(key)
        if arm < 0:
            raise ValueError(f"model has no key bone {key}")
        chain = [arm]
        while len(chain) < 3 and self.children(chain[-1]):
            chain.append(max(self.children(chain[-1]), key=self.subtree_size))
        return chain

    def animated(self, place):
        _, global_sequence, count, _ = struct.unpack_from("<hhII", self.data, place)
        return global_sequence == -1 and count == self.sequence_count

    def keys(self, place, track, sequence):
        times = struct.unpack_from("<I", self.data, place + 8)[0]
        values = struct.unpack_from("<I", self.data, place + 16)[0]
        count, stamps_at = counted(self.data, times + sequence * 8)
        _, values_at = counted(self.data, values + sequence * 8)
        size = VALUE_SIZES[track]
        stamps = struct.unpack_from(f"<{count}I", self.data, stamps_at)
        return stamps, [unpack_value(track, bytes(self.data[values_at + index * size:values_at + (index + 1) * size]))
                        for index in range(count)]

    def value(self, bone, track, sequence, time):
        place = self.bone(bone) + track
        if sequence is None or not self.animated(place):
            return None
        stamps, values = self.keys(place, track, sequence)
        if not stamps:
            return None
        if time <= stamps[0]:
            return values[0]
        for index in range(1, len(stamps)):
            if time <= stamps[index]:
                share = (time - stamps[index - 1]) / max(1, stamps[index] - stamps[index - 1])
                if track == ROTATION:
                    return slerp(values[index - 1], values[index], share)
                return tuple(a + (b - a) * share for a, b in zip(values[index - 1], values[index]))
        return values[-1]

    def skin(self):
        if self.skinned is None:
            count, offset = counted(self.data, VERTICES)
            step = max(1, count // SKIN_SAMPLES)
            skinned = []
            for index in range(0, count, step):
                at = offset + index * VERTEX_SIZE
                position = struct.unpack_from("<3f", self.data, at)
                weights = struct.unpack_from("<4B", self.data, at + 12)
                bones = struct.unpack_from("<4B", self.data, at + 16)
                skinned.append((position, [(bone, weight / 255.0) for bone, weight in zip(bones, weights) if weight]))
            self.skinned = skinned
        return self.skinned

    def embedded(self, sequence):
        flags = struct.unpack_from("<I", self.data, self.sequence_offset + sequence * SEQUENCE_SIZE
                                   + SEQUENCE_FLAGS)[0]
        return bool(flags & EMBEDDED)

    def duration(self, sequence):
        return struct.unpack_from("<I", self.data, self.sequence_offset + sequence * SEQUENCE_SIZE
                                  + SEQUENCE_DURATION)[0]

    def append(self, payload, align=16):
        while len(self.data) % align:
            self.data.append(0)
        offset = len(self.data)
        self.data += payload
        return offset


class Handspring:
    def __init__(self, model, animation, backward):
        self.model = model
        self.animation = animation
        self.turn = -2.0 * math.pi if backward else 2.0 * math.pi
        self.reach = -FORWARD_REACH if backward else FORWARD_REACH
        sequences = model.sequences
        if JUMP_START not in sequences or STAND not in sequences:
            raise ValueError("model needs Stand and JumpStart sequences")
        self.take_off = sequences[JUMP_START]
        self.flight = sequences.get(JUMP)
        self.landing = sequences.get(JUMP_END, sequences[STAND])
        for sequence in (self.take_off, self.flight, self.landing):
            if sequence is not None and not model.embedded(sequence):
                raise ValueError(f"sequence {sequence} keeps its keys in an external .anim file")
        self.root = model.key_bone(KEY_ROOT)
        if self.root < 0 or not model.animated(model.bone(self.root) + ROTATION):
            raise ValueError("model has no animated root bone")
        self.arms = (model.arm(KEY_ARM_LEFT), model.arm(KEY_ARM_RIGHT))
        self.sides = (1.0, -1.0)
        self.stand = sequences[STAND]
        self.arm_bones = {bone for chain in self.arms for bone in self.limb(model.parents[chain[0]])}
        self.lowest = {}

    def limb(self, bone):
        found = [bone]
        for child in self.model.children(bone):
            found += self.limb(child)
        return found

    def source(self, share):
        take_off_length = self.model.duration(self.take_off)
        if share < TURN_START + 0.12:
            return self.take_off, take_off_length * 0.6 * share / (TURN_START + 0.12)
        if share < TURN_END - 0.04:
            if self.flight is not None:
                flight = (share - TURN_START - 0.12) / (TURN_END - 0.04 - TURN_START - 0.12)
                return self.flight, self.model.duration(self.flight) * flight
            return self.take_off, take_off_length * 0.6
        landing = (share - TURN_END + 0.04) / (1.0 - TURN_END + 0.04)
        return self.landing, self.model.duration(self.landing) * 0.5 * landing

    def posed(self, bone, track, share):
        sequence, time = (self.stand, 0) if bone in self.arm_bones else self.source(share)
        value = self.model.value(bone, track, sequence, time)
        return resting(track) if value is None else value

    def world_rotation(self, bone, share, overrides):
        rotation = (0.0, 0.0, 0.0, 1.0)
        while bone >= 0:
            local = overrides.get(bone) or self.posed(bone, ROTATION, share)
            rotation = product(local, rotation)
            bone = self.model.parents[bone]
        return rotation

    def arm_reach(self, chain):
        pivots = self.model.pivots
        finger = self.model.key_bone(INDEX_FINGERS[self.arms.index(chain)])
        tip = pivots[finger] if finger >= 0 else pivots[chain[-1]]
        return math.dist(pivots[chain[0]], tip), unit(tuple(b - a for a, b in zip(pivots[chain[0]], tip)))

    def reaching(self, share):
        rising = window(share, REACH_START, REACH_HELD)
        falling = 1.0 - window(share, REACH_RELEASED, REACH_END)
        return min(rising, falling)

    def arm_target(self, share, side):
        overhead = rotate(self.pitch(share), unit((self.reach, side * REACH_SPREAD, 1.0)))
        planted = unit((self.reach * PLANT_AHEAD, side * REACH_SPREAD, -1.0))
        planting = min(window(share, PLANT_START, PLANT_HELD), 1.0 - window(share, PLANT_RELEASED, PLANT_END))
        return unit(tuple(a + (b - a) * planting for a, b in zip(overhead, planted)))

    def swung(self, hanging, ahead, raised, weight):
        if weight <= 0.5:
            first, second, share = hanging, ahead, weight * 2.0
        else:
            first, second, share = ahead, raised, weight * 2.0 - 1.0
        return unit(tuple(a + (b - a) * share for a, b in zip(first, second)))

    def arm_rotations(self, share):
        weight = self.reaching(share)
        overrides = {}
        pitched = {self.root: self.root_rotation(share)}
        for chain, side in zip(self.arms, self.sides):
            _, bind_direction = self.arm_reach(chain)
            above = self.world_rotation(self.model.parents[chain[0]], share, pitched)
            standing = product(above, self.posed(chain[0], ROTATION, share))
            held = rotate(standing, bind_direction)
            hanging = unit((HANG_AHEAD, 0.0, -1.0))
            ahead = rotate(self.pitch(share), (1.0, side * REACH_SPREAD, 0.0))
            direction = self.swung(hanging, unit(ahead), self.arm_target(share, side), weight)
            overrides[chain[0]] = product(conjugate(above), product(turning(held, direction), standing))
            for bone in chain[1:]:
                overrides[bone] = slerp(self.posed(bone, ROTATION, share), (0.0, 0.0, 0.0, 1.0), weight)
        return overrides

    def local_rotation(self, bone, share, arms):
        if bone == self.root:
            return self.root_rotation(share)
        return arms.get(bone) or self.posed(bone, ROTATION, share)

    def lowest_point(self, share):
        if share in self.lowest:
            return self.lowest[share]
        arms = self.arm_rotations(share)
        placed = {}

        def place(bone):
            if bone not in placed:
                local = bone_matrix(self.model.pivots[bone], self.posed(bone, TRANSLATION, share),
                                    self.local_rotation(bone, share, arms), self.posed(bone, SCALE, share))
                parent = self.model.parents[bone]
                placed[bone] = combined(place(parent), local) if parent >= 0 else local
            return placed[bone]

        lowest = math.inf
        for position, influences in self.model.skin():
            height = sum(weight * transformed(place(bone), position)[2] for bone, weight in influences)
            lowest = min(lowest, height)
        self.lowest[share] = lowest
        return lowest

    def lift(self, share):
        hop = HOP * math.sin(math.pi * max(0.0, min(1.0, (share - FLIGHT_START) / (FLIGHT_END - FLIGHT_START))))
        return GROUND_CLEARANCE - self.lowest_point(share) + hop

    def frames(self):
        return [min(time, DURATION_MS) for time in range(0, DURATION_MS + KEY_STEP_MS, KEY_STEP_MS)]

    def pitch(self, share):
        return about((0.0, 1.0, 0.0), self.turn * window(share, TURN_START, TURN_END))

    def root_rotation(self, share):
        return product(self.pitch(share), self.posed(self.root, ROTATION, share))

    def root_translation(self, share):
        base = self.posed(self.root, TRANSLATION, share)
        return (base[0], base[1], base[2] + self.lift(share))

    def closed(self, track, value, start, share):
        if share <= 0.0:
            return value
        if track == ROTATION:
            return slerp(value, start, share)
        return tuple(a + (b - a) * share for a, b in zip(value, start))

    def entry(self, bone, track):
        times = self.frames()
        values = []
        previous = None
        for time in times:
            share = time / DURATION_MS
            if bone == self.root and track == ROTATION:
                value = self.root_rotation(share)
            elif bone == self.root and track == TRANSLATION:
                value = self.root_translation(share)
            elif track == ROTATION:
                value = self.arm_rotations(share).get(bone) or self.posed(bone, track, share)
            else:
                value = self.posed(bone, track, share)
            values.append(value)
        closing = [window(time / DURATION_MS, LOOP_CLOSE, 1.0) for time in times]
        values = [self.closed(track, value, values[0], share) for value, share in zip(values, closing)]
        for index, value in enumerate(values):
            if track == ROTATION:
                value = smoothed_continuity(previous, value)
                values[index] = previous = value
        if bone != self.root and all(value == resting(track) for value in values):
            return 0, b"", b""
        return (len(times), struct.pack(f"<{len(times)}I", *times),
                b"".join(pack_value(track, value) for value in values))


def extend(model, place, has_values, entries):
    if not model.animated(place):
        return
    times = struct.unpack_from("<I", model.data, place + 8)[0]
    count = model.sequence_count
    outer_times = bytearray(model.data[times:times + count * 8])
    outer_values = bytearray()
    if has_values:
        values = struct.unpack_from("<I", model.data, place + 16)[0]
        outer_values = bytearray(model.data[values:values + count * 8])
    for entry in entries:
        if isinstance(entry, int):
            outer_times += model.data[times + entry * 8:times + entry * 8 + 8]
            if has_values:
                outer_values += model.data[values + entry * 8:values + entry * 8 + 8]
            continue
        keys, stamps, packed = entry
        outer_times += struct.pack("<II", keys, model.append(stamps, 4) if keys else 0)
        outer_values += struct.pack("<II", keys, model.append(packed, 4) if keys else 0)
    total = count + len(entries)
    struct.pack_into("<II", model.data, place + 4, total, model.append(bytes(outer_times), 4))
    if has_values:
        struct.pack_into("<II", model.data, place + 12, total, model.append(bytes(outer_values), 4))


def sequence_record(model, handspring, index):
    template = model.sequence_offset + handspring.take_off * SEQUENCE_SIZE
    record = bytearray(model.data[template:template + SEQUENCE_SIZE])
    struct.pack_into("<HHIf", record, 0, handspring.animation, 0, DURATION_MS, 0.0)
    flags = struct.unpack_from("<I", record, SEQUENCE_FLAGS)[0]
    struct.pack_into("<I", record, SEQUENCE_FLAGS, flags | EMBEDDED)
    struct.pack_into("<I", record, SEQUENCE_BLEND, BLEND_MS)
    bounds = list(struct.unpack_from("<7f", record, SEQUENCE_BOUNDS))
    center = [(low + high) / 2.0 for low, high in zip(bounds[:3], bounds[3:6])]
    half = max((high - low) / 2.0 for low, high in zip(bounds[:3], bounds[3:6])) * BOUNDS_GROWTH
    bounds = [value - half for value in center] + [value + half for value in center] + [bounds[6] * BOUNDS_GROWTH]
    struct.pack_into("<7f", record, SEQUENCE_BOUNDS, *bounds)
    struct.pack_into("<hH", record, SEQUENCE_NEXT, -1, index)
    return record


def relink(model, added):
    count, offset = counted(model.data, SEQUENCE_LOOKUP)
    lookup = list(struct.unpack_from(f"<{count}h", model.data, offset))
    lookup += [-1] * max(0, max(added) + 1 - len(lookup))
    for animation, index in added.items():
        lookup[animation] = index
    struct.pack_into("<II", model.data, SEQUENCE_LOOKUP, len(lookup),
                     model.append(struct.pack(f"<{len(lookup)}h", *lookup), 16))


def add_handsprings(data):
    model = Model(data)
    taken = [animation for animation in (HANDSPRING, BACK_HANDSPRING) if animation in model.sequences]
    if taken:
        raise ValueError(f"model already has handspring sequences {taken}")
    moves = [Handspring(model, HANDSPRING, False), Handspring(model, BACK_HANDSPRING, True)]
    entries = {}
    for bone in range(model.bone_count):
        for track in (TRANSLATION, ROTATION, SCALE):
            place = model.bone(bone) + track
            if model.animated(place):
                entries[place] = [move.entry(bone, track) for move in moves]
    for place, made in entries.items():
        extend(model, place, True, made)
    for header, (size, tracks) in RECORD_TRACKS.items():
        count, offset = counted(model.data, header)
        for record in range(count):
            for track in tracks:
                extend(model, offset + record * size + track, True, [move.take_off for move in moves])
    count, offset = counted(model.data, EVENTS)
    for record in range(count):
        extend(model, offset + record * EVENT_SIZE + EVENT_TRACK, False, [move.take_off for move in moves])
    old_count = model.sequence_count
    records = bytearray(model.data[model.sequence_offset:model.sequence_offset + old_count * SEQUENCE_SIZE])
    added = {}
    for position, move in enumerate(moves):
        added[move.animation] = old_count + position
        records += sequence_record(model, move, old_count + position)
    struct.pack_into("<II", model.data, SEQUENCES, len(records) // SEQUENCE_SIZE,
                     model.append(bytes(records), 16))
    relink(model, added)
    return bytes(model.data)


def add_animation_rows(dbc):
    def made(by_id, texts):
        added = []
        for animation, name in sorted(ANIMATION_NAMES.items()):
            flying = animation in (FLY_HANDSPRING, FLY_BACK_HANDSPRING)
            row = list(by_id[FLY_JUMP if flying else JUMP])
            row[0], row[1] = animation, len(texts)
            texts += name.encode() + b"\0"
            row[ANIMATION_WEAPON_FLAGS] = WEAPONS_SHEATHED
            row[5] = FLY_JUMP_START if flying else JUMP_START
            row[6] = animation - 1 if flying else animation
            added.append(row)
        return added

    return appended_rows(dbc, ANIMATION_FIELDS, made)


def appended_rows(dbc, expected_fields, made):
    magic, count, fields, size, text_size = struct.unpack_from("<4s4I", dbc, 0)
    if magic != b"WDBC" or fields != expected_fields:
        raise ValueError("unexpected table layout")
    rows = [list(struct.unpack_from(f"<{fields}I", dbc, 20 + index * size)) for index in range(count)]
    texts = bytearray(dbc[20 + count * size:20 + count * size + text_size])
    added = made({row[0]: row for row in rows}, texts)
    taken = sorted({row[0] for row in rows} & {row[0] for row in added})
    if taken:
        raise ValueError(f"table already has ids {taken}")
    rows += added
    body = b"".join(struct.pack(f"<{fields}I", *row) for row in rows)
    return struct.pack("<4s4I", b"WDBC", len(rows), fields, size, len(texts)) + body + bytes(texts)


def add_visual_kits(dbc):
    def made(by_id, _):
        if VAULT_DUST_KIT not in by_id:
            raise ValueError(f"SpellVisualKit has no row {VAULT_DUST_KIT}")
        added = []
        for kit, animation in VAULT_KITS.items():
            row = list(by_id[VAULT_DUST_KIT])
            row[0], row[KIT_ANIMATION] = kit, animation
            added.append(row)
        return added

    return appended_rows(dbc, KIT_FIELDS, made)


def main():
    parser = argparse.ArgumentParser(description="Add Handspring and BackHandspring to client files")
    subcommands = parser.add_subparsers(dest="command", required=True)
    model_command = subcommands.add_parser("model", help="add both handspring sequences to a character M2")
    model_command.add_argument("source", type=Path)
    model_command.add_argument("output", type=Path)
    animations_command = subcommands.add_parser("animations", help="add the handspring rows to AnimationData.dbc")
    animations_command.add_argument("source", type=Path)
    animations_command.add_argument("output", type=Path)
    kits_command = subcommands.add_parser("kits", help="add the Vault handspring rows to SpellVisualKit.dbc")
    kits_command.add_argument("source", type=Path)
    kits_command.add_argument("output", type=Path)
    args = parser.parse_args()
    if args.source.resolve() == args.output.resolve():
        parser.error("Use a separate staging output; do not overwrite the source client")
    change = {"model": add_handsprings, "animations": add_animation_rows, "kits": add_visual_kits}[args.command]
    with args.output.open("xb") as output:
        output.write(change(args.source.read_bytes()))


if __name__ == "__main__":
    main()
