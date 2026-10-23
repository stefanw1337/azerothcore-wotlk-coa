import struct
import sys
from pathlib import Path

import numpy as np

import mkgunpose as g

LEFT_CASTS = ((31, 214), (51, 215), (52, 216), (53, 217), (54, 218), (124, 219), (125, 220))
READY_1H = 26
MIRROR_REACH = 0.08
CENTERED = 0.05
QUATERNION_SIZE, VECTOR_SIZE = 8, 12
SPINE_TWIST_KEPT = 0.5
STILL_ROTATION = (0.0, 0.0, 0.0, 1.0)


def mirrored_rotations(values):
    count = len(values) // QUATERNION_SIZE
    quaternions = [g.unpacked(values[index * 8:index * 8 + 8]) for index in range(count)]
    return b"".join(g.packed(quaternion, True) for quaternion in quaternions)


def mirrored_places(values):
    count = len(values) // VECTOR_SIZE
    places = [struct.unpack_from("<3f", values, index * 12) for index in range(count)]
    return b"".join(struct.pack("<3f", x, -y, z) for x, y, z in places)


MIRRORS = {g.ROTATION: mirrored_rotations, g.TRANSLATION: mirrored_places}


def slerp(first, second, share):
    first, second = np.array(first), np.array(second)
    dot = float(first @ second)
    if dot < 0.0:
        second, dot = -second, -dot
    if dot > 0.9995:
        blend = first + share * (second - first)
        return tuple(blend / np.linalg.norm(blend))
    angle = np.arccos(dot)
    blend = (np.sin((1.0 - share) * angle) * first + np.sin(share * angle) * second) / np.sin(angle)
    return tuple(blend / np.linalg.norm(blend))


def calmed(track, values, rest, kept):
    if track == g.ROTATION:
        count = len(values) // QUATERNION_SIZE
        return b"".join(g.packed(slerp(rest, g.unpacked(values[index * 8:index * 8 + 8]), kept), False)
                        for index in range(count))
    if track == g.TRANSLATION:
        count = len(values) // VECTOR_SIZE
        places = [np.array(struct.unpack_from("<3f", values, index * 12)) for index in range(count)]
        return b"".join(struct.pack("<3f", *(np.array(rest) + kept * (place - np.array(rest))))
                        for place in places)
    return values


def keyed_sequences(data, place):
    return struct.unpack_from("<I", data, place + 12)[0]


def rest_of(data, bone, track, size, ready_sequence):
    place = g.bone_offset(data, bone) + track
    entry = g.own_track(data, place, ready_sequence, size) if ready_sequence < keyed_sequences(data, place) else None
    if entry and entry[0]:
        if track == g.ROTATION:
            return g.unpacked(entry[2][:8])
        return struct.unpack_from("<3f", entry[2], 0)
    return STILL_ROTATION if track == g.ROTATION else (0.0, 0.0, 0.0)


def spine_bones(data):
    left_arm = g.arm_chain(data, g.KEY_ARM_LEFT)
    return {bone for bone in g.spine_of(data, left_arm) if abs(g.pivot(data, bone)[1]) < CENTERED}


def calm_left_casts(data, kept=SPINE_TWIST_KEPT):
    data = bytearray(data)
    ready_sequence = g.sequence_index(data, READY_1H)
    for _, animation in LEFT_CASTS:
        sequence = g.sequence_index(data, animation)
        for bone in spine_bones(data):
            for track, size in ((g.ROTATION, QUATERNION_SIZE), (g.TRANSLATION, VECTOR_SIZE)):
                place = g.bone_offset(data, bone) + track
                if struct.unpack_from("<h", data, place + 2)[0] != -1 or sequence >= keyed_sequences(data, place):
                    continue
                values = struct.unpack_from("<I", data, place + 16)[0]
                count, values_at = g.array(data, values + sequence * 8)
                if not count:
                    continue
                old = bytes(data[values_at:values_at + count * size])
                data[values_at:values_at + count * size] = calmed(track, old, rest_of(data, bone, track, size,
                                                                                      ready_sequence), kept)
    return bytes(data)


def mirror_partners(data, left_root, right_root):
    kids = g.children(data)
    partners, pending = {left_root: right_root}, [(left_root, right_root)]
    while pending:
        left, right = pending.pop()
        free = list(kids.get(right, []))
        for child in kids.get(left, []):
            x, y, z = g.pivot(data, child)
            wanted = np.array((x, -y, z))
            nearest = min(free, key=lambda other: np.linalg.norm(g.pivot(data, other) - wanted), default=None)
            if nearest is None or np.linalg.norm(g.pivot(data, nearest) - wanted) > MIRROR_REACH:
                continue
            free.remove(nearest)
            partners[child] = nearest
            pending.append((child, nearest))
    return partners


class LeftCaster:
    def __init__(self, data):
        self.blob = g.Blob(data)
        self.old_count = g.array(data, g.SEQUENCES)[0]
        _, offset = g.array(data, g.SEQUENCES)
        present = {struct.unpack_from("<H", data, offset + index * g.SEQUENCE_SIZE)[0]
                   for index in range(self.old_count)}
        taken = [animation for _, animation in LEFT_CASTS if animation in present]
        if taken:
            raise SystemExit(f"left-arm casts already present: {taken}")

    def track_entry(self, place, sequence, size):
        data = self.blob.data
        _, global_sequence, count, times = struct.unpack_from("<hhII", data, place)
        if global_sequence != -1 or count != self.old_count:
            return None
        return g.own_track(data, place, sequence, size)

    def extend(self, track, has_values, entries):
        data = self.blob.data
        _, global_sequence, count, times = struct.unpack_from("<hhII", data, track)
        if global_sequence != -1 or count != self.old_count:
            return
        values = struct.unpack_from("<I", data, track + 16)[0] if has_values else 0
        outer_times = bytearray(data[times:times + count * 8])
        outer_values = bytearray(data[values:values + count * 8]) if has_values else bytearray()
        for entry in entries:
            if isinstance(entry, int):
                outer_times += data[times + entry * 8:times + entry * 8 + 8]
                if has_values:
                    outer_values += data[values + entry * 8:values + entry * 8 + 8]
                continue
            keys, stamps, packed = entry
            outer_times += struct.pack("<II", keys, self.blob.append(stamps, 4))
            outer_values += struct.pack("<II", keys, self.blob.append(packed, 4))
        total = count + len(entries)
        struct.pack_into("<II", self.blob.data, track + 4, total, self.blob.append(bytes(outer_times), 4))
        if has_values:
            struct.pack_into("<II", self.blob.data, track + 12, total, self.blob.append(bytes(outer_values), 4))

    def bone_entry(self, bone, track, size, cast, ready, swapped, spine):
        place = g.bone_offset(self.blob.data, bone) + track
        if bone in swapped:
            source = self.track_entry(g.bone_offset(self.blob.data, swapped[bone]) + track, cast, size)
            if source is None or track not in MIRRORS:
                return cast if source is None else source
            keys, stamps, values = source
            return keys, stamps, MIRRORS[track](values)
        if bone in ready:
            return ready[bone]
        if bone in spine and track in MIRRORS:
            source = self.track_entry(place, cast, size)
            if source is not None:
                keys, stamps, values = source
                rest = rest_of(self.blob.data, bone, track, size, self.ready_sequence)
                return keys, stamps, calmed(track, MIRRORS[track](values), rest, SPINE_TWIST_KEPT)
        return cast

    def cast(self):
        ogre = bytes(self.blob.data)
        left_arm = g.arm_chain(ogre, g.KEY_ARM_LEFT)
        right_arm = g.arm_chain(ogre, g.KEY_ARM_RIGHT)
        swapped = mirror_partners(ogre, left_arm[0], right_arm[0])
        right_side = g.subtree_of(ogre, right_arm[0])
        ready_sequence = g.sequence_index(ogre, READY_1H)
        self.ready_sequence = ready_sequence
        spine = spine_bones(ogre)
        casts = [g.sequence_index(ogre, animation) for animation, _ in LEFT_CASTS]
        bone_count, _ = g.array(ogre, g.BONES)
        for bone in range(bone_count):
            for track, size in zip(g.BONE_TRACKS, g.TRACK_VALUE_SIZES):
                place = g.bone_offset(self.blob.data, bone) + track
                ready = {bone: ready_sequence} if bone in right_side else {}
                entries = [self.bone_entry(bone, track, size, cast, ready, swapped, spine) for cast in casts]
                self.extend(place, True, entries)
        for header, (size, tracks) in g.RECORD_TRACKS.items():
            count, offset = g.array(self.blob.data, header)
            for record in range(count):
                for track in tracks:
                    self.extend(offset + record * size + track, True, list(casts))
        count, offset = g.array(self.blob.data, g.EVENTS)
        for record in range(count):
            self.extend(offset + record * g.EVENT_SIZE + g.EVENT_TRACK, False, list(casts))
        self.append_sequences(casts)
        return bytes(self.blob.data)

    def append_sequences(self, casts):
        data = self.blob.data
        _, offset = g.array(data, g.SEQUENCES)
        records = bytearray(data[offset:offset + self.old_count * g.SEQUENCE_SIZE])
        for position, (sequence, (_, animation)) in enumerate(zip(casts, LEFT_CASTS)):
            record = bytearray(data[offset + sequence * g.SEQUENCE_SIZE:offset + (sequence + 1) * g.SEQUENCE_SIZE])
            struct.pack_into("<HH", record, 0, animation, 0)
            struct.pack_into("<hH", record, g.SEQUENCE_NEXT, -1, self.old_count + position)
            records += record
        struct.pack_into("<II", self.blob.data, g.SEQUENCES, len(records) // g.SEQUENCE_SIZE,
                         self.blob.append(bytes(records), 16))


def main(model_path, mode="cast"):
    model = Path(model_path)
    if mode == "calm":
        model.write_bytes(calm_left_casts(model.read_bytes()))
    else:
        model.write_bytes(LeftCaster(model.read_bytes()).cast())


if __name__ == "__main__":
    main(*sys.argv[1:])
