import struct
import sys
from pathlib import Path

SEQUENCES, BONES, KEY_BONES = 0x1C, 0x2C, 0x34
TEXTURE_WEIGHTS, TEXTURE_TRANSFORMS = 0x58, 0x60
ATTACHMENTS, EVENTS, CAMERAS = 0xF0, 0x100, 0x110
SEQUENCE_SIZE, BONE_SIZE = 64, 88
SEQUENCE_BOUNDS = slice(32, 60)
SEQUENCE_NEXT = 60
BONE_TRACKS = (16, 36, 56)
ROTATION = 36
TRACK_SIZE = 20
RECORD_TRACKS = {TEXTURE_WEIGHTS: (20, (0,)), TEXTURE_TRANSFORMS: (60, (0, 20, 40)), ATTACHMENTS: (40, (20,)),
                 CAMERAS: (100, (16, 48, 80))}
EVENT_SIZE, EVENT_TRACK = 36, 24
KEY_ARM_LEFT, KEY_ARM_RIGHT = 0, 1
SPINE = (2, 3, 4)
READY_1H = 26
COPIED = ((48, True), (49, True), (110, True), (29, False), (46, False), (109, False))


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


def mirrored(packed):
    x, y, z, w = (decode(v) for v in struct.unpack("<4h", packed))
    return struct.pack("<4h", encode(-x), encode(y), encode(-z), encode(w))


def rotation_keys(data, bone, sequence, mirror):
    track = bone_offset(data, bone) + ROTATION
    _, _, times_count, times, values_count, values = struct.unpack_from("<hhIIII", data, track)
    count, at = array(data, times + sequence * 8)
    stamps = data[at:at + count * 4]
    count, at = array(data, values + sequence * 8)
    keys = [data[at + index * 8:at + index * 8 + 8] for index in range(count)]
    return count, bytes(stamps), b"".join(mirrored(key) if mirror else key for key in keys)


def bone_sources(ogre, source, mirror):
    ogre_sides = (arm_chain(ogre, KEY_ARM_LEFT), arm_chain(ogre, KEY_ARM_RIGHT))
    source_sides = (arm_chain(source, KEY_ARM_LEFT), arm_chain(source, KEY_ARM_RIGHT))
    if mirror:
        source_sides = source_sides[::-1]
    mapping = {bone: bone for bone in SPINE}
    for ogre_chain, source_chain in zip(ogre_sides, source_sides):
        mapping.update(zip(ogre_chain, source_chain))
    return mapping


class Grafter:
    def __init__(self, ogre, source):
        self.blob = Blob(ogre)
        self.source = source
        self.old_count = array(ogre, SEQUENCES)[0]
        self.ready = sequence_index(ogre, READY_1H)
        self.added = []

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
                outer_times += data[times + self.ready * 8:times + self.ready * 8 + 8]
                if has_values:
                    outer_values += data[values + self.ready * 8:values + self.ready * 8 + 8]
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
        wanted = [(sequence_index(self.source, animation), mirror) for animation, mirror in COPIED]
        mappings = [bone_sources(bytes(self.blob.data), self.source, mirror) for _, mirror in wanted]
        bone_count, _ = array(self.blob.data, BONES)
        for bone in range(bone_count):
            for track in BONE_TRACKS:
                entries = []
                for (sequence, mirror), mapping in zip(wanted, mappings):
                    copied = track == ROTATION and bone in mapping
                    entries.append(rotation_keys(self.source, mapping[bone], sequence, mirror) if copied else None)
                self.extend(bone_offset(self.blob.data, bone) + track, True, entries)
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
        ready = data[offset + self.ready * SEQUENCE_SIZE:offset + (self.ready + 1) * SEQUENCE_SIZE]
        for position, (sequence, _) in enumerate(wanted):
            record = bytearray(self.source[source_offset + sequence * SEQUENCE_SIZE:
                                           source_offset + (sequence + 1) * SEQUENCE_SIZE])
            record[SEQUENCE_BOUNDS] = ready[SEQUENCE_BOUNDS]
            struct.pack_into("<hH", record, SEQUENCE_NEXT, -1, self.old_count + position)
            records += record
        struct.pack_into("<II", self.blob.data, SEQUENCES, len(records) // SEQUENCE_SIZE,
                         self.blob.append(bytes(records), 16))


def graft(ogre_path, source_path):
    ogre = Path(ogre_path)
    ogre.write_bytes(Grafter(ogre.read_bytes(), Path(source_path).read_bytes()).graft())


if __name__ == "__main__":
    graft(sys.argv[1], sys.argv[2])
