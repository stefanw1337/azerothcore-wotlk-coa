import math
import struct
import unittest

import patch_handspring_client as handspring

BONES = ((-1, 0, (0.0, 0.0, 1.0)), (0, 1, (0.0, 0.25, 1.5)), (1, 2, (0.0, 0.55, 1.5)), (2, 3, (0.0, 0.8, 1.5)),
         (0, 4, (0.0, -0.25, 1.5)), (4, 5, (0.0, -0.55, 1.5)), (5, 6, (0.0, -0.8, 1.5)))
KEY_BONES = {handspring.KEY_ROOT: 0, handspring.KEY_ARM_LEFT: 1, handspring.KEY_ARM_RIGHT: 4}
VERTICES = (((0.0, 0.0, 0.0), 0), ((0.0, 0.0, 1.9), 0), ((0.0, 0.95, 1.5), 3), ((0.0, -0.95, 1.5), 6))
SEQUENCE_IDS = (handspring.STAND, handspring.JUMP_START)


class Writer:
    def __init__(self):
        self.data = bytearray(0x130)

    def put(self, payload):
        while len(self.data) % 16:
            self.data.append(0)
        offset = len(self.data)
        self.data += payload
        return offset

    def array(self, header, count, payload):
        struct.pack_into("<II", self.data, header, count, self.put(payload) if count else 0)

    def track(self, value):
        stamps = [self.put(struct.pack("<I", 0)) for _ in SEQUENCE_IDS]
        values = [self.put(value) for _ in SEQUENCE_IDS]
        outer_stamps = self.put(b"".join(struct.pack("<II", 1, at) for at in stamps))
        outer_values = self.put(b"".join(struct.pack("<II", 1, at) for at in values))
        return struct.pack("<hhIIII", 1, -1, len(SEQUENCE_IDS), outer_stamps, len(SEQUENCE_IDS), outer_values)


def synthetic_model():
    writer = Writer()
    writer.data[:8] = b"MD20" + struct.pack("<I", 264)
    sequences = b""
    for index, animation in enumerate(SEQUENCE_IDS):
        record = bytearray(handspring.SEQUENCE_SIZE)
        struct.pack_into("<HHIfI", record, 0, animation, 0, 800, 0.0, handspring.EMBEDDED)
        struct.pack_into("<7f", record, handspring.SEQUENCE_BOUNDS, -1, -1, 0, 1, 1, 2, 2)
        struct.pack_into("<hH", record, handspring.SEQUENCE_NEXT, -1, index)
        sequences += record
    writer.array(handspring.SEQUENCES, len(SEQUENCE_IDS), sequences)
    lookup = [-1] * (handspring.JUMP_START + 1)
    for index, animation in enumerate(SEQUENCE_IDS):
        lookup[animation] = index
    writer.array(handspring.SEQUENCE_LOOKUP, len(lookup), struct.pack(f"<{len(lookup)}h", *lookup))
    bones = b""
    for parent, _, pivot in BONES:
        bone = bytearray(struct.pack("<iIhHI", -1, 0, parent, 0, 0))
        bone += writer.track(struct.pack("<3f", 0.0, 0.0, 0.0))
        bone += writer.track(handspring.pack_value(handspring.ROTATION, (0.0, 0.0, 0.0, 1.0)))
        bone += writer.track(struct.pack("<3f", 1.0, 1.0, 1.0))
        bone += struct.pack("<3f", *pivot)
        bones += bone
    writer.array(handspring.BONES, len(BONES), bones)
    keys = [-1] * (handspring.KEY_ROOT + 1)
    for key, bone in KEY_BONES.items():
        keys[key] = bone
    writer.array(handspring.KEY_BONES, len(keys), struct.pack(f"<{len(keys)}h", *keys))
    vertices = b""
    for position, bone in VERTICES:
        vertices += struct.pack("<3f4B4B3f2f2f", *position, 255, 0, 0, 0, bone, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0)
    writer.array(handspring.VERTICES, len(VERTICES), vertices)
    return bytes(writer.data)


def table(fields, rows, texts):
    body = b"".join(struct.pack(f"<{fields}I", *row) for row in rows)
    return struct.pack("<4s4I", b"WDBC", len(rows), fields, fields * 4, len(texts)) + body + texts


class HandspringModelTest(unittest.TestCase):
    def setUp(self):
        self.model = handspring.Model(handspring.add_handsprings(synthetic_model()))

    def test_both_handsprings_are_reachable_by_animation_id(self):
        count, offset = handspring.counted(self.model.data, handspring.SEQUENCE_LOOKUP)
        lookup = struct.unpack_from(f"<{count}h", self.model.data, offset)
        for animation in (handspring.HANDSPRING, handspring.BACK_HANDSPRING):
            index = self.model.sequences[animation]
            self.assertEqual(lookup[animation], index)
            self.assertEqual(self.model.duration(index), handspring.DURATION_MS)
        self.assertEqual(lookup[handspring.JUMP_START], self.model.sequences[handspring.JUMP_START])

    def test_body_turns_over_in_opposite_directions(self):
        for animation, sign in ((handspring.HANDSPRING, 1.0), (handspring.BACK_HANDSPRING, -1.0)):
            sequence = self.model.sequences[animation]
            ahead = self.model.value(0, handspring.ROTATION, sequence, 150)
            self.assertGreater(sign * rotated_head(ahead)[0], 0.3)
            inverted = self.model.value(0, handspring.ROTATION, sequence, handspring.DURATION_MS * 0.45)
            self.assertLess(rotated_head(inverted)[2], -0.8)
            landed = self.model.value(0, handspring.ROTATION, sequence, handspring.DURATION_MS)
            self.assertGreater(rotated_head(landed)[2], 0.99)

    def test_body_stays_on_the_ground(self):
        for animation in (handspring.HANDSPRING, handspring.BACK_HANDSPRING):
            sequence = self.model.sequences[animation]
            for time in range(0, handspring.DURATION_MS + 1, handspring.KEY_STEP_MS):
                self.assertGreater(lowest(self.model, sequence, time), -0.01)

    def test_handsprings_end_in_the_pose_they_start_in(self):
        for animation in (handspring.HANDSPRING, handspring.BACK_HANDSPRING):
            sequence = self.model.sequences[animation]
            for bone in range(self.model.bone_count):
                for track in (handspring.TRANSLATION, handspring.ROTATION, handspring.SCALE):
                    first = self.model.value(bone, track, sequence, 0)
                    last = self.model.value(bone, track, sequence, handspring.DURATION_MS)
                    if first is None:
                        self.assertIsNone(last)
                        continue
                    if track == handspring.ROTATION:
                        self.assertGreater(abs(sum(a * b for a, b in zip(first, last))), 0.9999)
                    else:
                        for a, b in zip(first, last):
                            self.assertAlmostEqual(a, b, places=4)

    def test_existing_sequences_keep_their_tracks(self):
        before = handspring.Model(synthetic_model())
        for bone in range(before.bone_count):
            for track in (handspring.TRANSLATION, handspring.ROTATION, handspring.SCALE):
                for sequence in range(before.sequence_count):
                    self.assertEqual(before.keys(before.bone(bone) + track, track, sequence),
                                     self.model.keys(self.model.bone(bone) + track, track, sequence))

    def test_model_with_external_animation_keys_is_refused(self):
        data = bytearray(synthetic_model())
        _, offset = handspring.counted(data, handspring.SEQUENCES)
        jump_start = offset + SEQUENCE_IDS.index(handspring.JUMP_START) * handspring.SEQUENCE_SIZE
        struct.pack_into("<I", data, jump_start + handspring.SEQUENCE_FLAGS, 0)
        with self.assertRaises(ValueError):
            handspring.add_handsprings(bytes(data))

    def test_model_with_handsprings_is_refused(self):
        with self.assertRaises(ValueError):
            handspring.add_handsprings(bytes(self.model.data))


def rotated_head(rotation):
    return handspring.rotate(rotation, (0.0, 0.0, 1.0))


def lowest(model, sequence, time):
    def world(bone):
        parent = model.parents[bone]
        rotation = (model.value(bone, handspring.ROTATION, sequence, time)
                    or handspring.resting(handspring.ROTATION))
        translation = (model.value(bone, handspring.TRANSLATION, sequence, time)
                       or handspring.resting(handspring.TRANSLATION))
        pivot = model.pivots[bone]
        origin = tuple(p + t - r for p, t, r in zip(pivot, translation, handspring.rotate(rotation, pivot)))
        if parent < 0:
            return rotation, origin
        parent_rotation, parent_origin = world(parent)
        return (handspring.product(parent_rotation, rotation),
                tuple(o + v for o, v in zip(parent_origin, handspring.rotate(parent_rotation, origin))))

    heights = []
    for position, bone in VERTICES:
        rotation, origin = world(bone)
        heights.append(origin[2] + handspring.rotate(rotation, position)[2])
    return min(heights)


def synthetic_skin(geosets):
    lookup = list(range(len(VERTICES)))
    head = bytearray(b"SKIN" + bytes(44))
    lookup_at = len(head)
    head += struct.pack(f"<{len(lookup)}H", *lookup)
    while len(head) % 4:
        head.append(0)
    submesh_at = len(head)
    for geoset, start, size in geosets:
        submesh = bytearray(handspring.SUBMESH_SIZE)
        struct.pack_into("<4H", submesh, 0, geoset, 0, start, size)
        head += submesh
    struct.pack_into("<II", head, handspring.SKIN_VERTICES, len(lookup), lookup_at)
    struct.pack_into("<II", head, handspring.SKIN_SUBMESHES, len(geosets), submesh_at)
    return bytes(head)


class BodyGeosetTest(unittest.TestCase):
    def test_only_the_body_geosets_count(self):
        skin = synthetic_skin([(0, 0, 2), (5, 2, 1), (401, 3, 1)])
        self.assertEqual(handspring.body_vertices(skin), {0, 1, 3})

    def test_hidden_geometry_does_not_hold_the_body_up(self):
        skin = synthetic_skin([(0, 0, 4)])
        model = handspring.Model(handspring.add_handsprings(synthetic_model(), handspring.DIVE_ROLL, skin))
        sequence = model.sequences[handspring.HANDSPRING]
        for time in range(0, handspring.DIVE_ROLL.duration + 1, handspring.KEY_STEP_MS):
            self.assertGreater(lowest(model, sequence, time), -0.01)

    def test_skin_without_body_is_refused(self):
        with self.assertRaises(ValueError):
            handspring.body_vertices(synthetic_skin([(5, 0, 4)]))


class HandspringStyleTest(unittest.TestCase):
    def test_races_get_their_own_vault(self):
        self.assertIs(handspring.style_for("TaurenFemale"), handspring.DIVE_ROLL)
        self.assertIs(handspring.style_for("ScourgeMale"), handspring.SIDE_FLIP)
        self.assertEqual(handspring.SIDE_FLIP.axis, handspring.SIDE_AXIS)
        self.assertEqual(handspring.style_for("GnomeMale").turns, 3)
        self.assertGreater(handspring.style_for("NightElfMale").air, 0.0)
        self.assertIs(handspring.style_for("HumanMale"), handspring.HANDSPRING_STYLE)

    def test_every_style_always_plays_and_turns_over_whole_times(self):
        for style in (handspring.HANDSPRING_STYLE, *handspring.STYLES.values()):
            model = handspring.Model(handspring.add_handsprings(synthetic_model(), style))
            for animation in (handspring.HANDSPRING, handspring.BACK_HANDSPRING):
                sequence = model.sequences[animation]
                record = model.sequence_offset + sequence * handspring.SEQUENCE_SIZE
                frequency = struct.unpack_from("<h", model.data, record + handspring.SEQUENCE_FREQUENCY)[0]
                self.assertEqual(frequency, handspring.ALWAYS)
                self.assertEqual(model.duration(sequence), style.duration)
                for time in range(0, style.duration + 1, handspring.KEY_STEP_MS):
                    self.assertGreater(lowest(model, sequence, time), -0.01)

    def test_aerial_flip_leaves_the_ground(self):
        style = handspring.STYLES["nightelf"]
        model = handspring.Model(handspring.add_handsprings(synthetic_model(), style))
        sequence = model.sequences[handspring.HANDSPRING]
        self.assertGreater(lowest(model, sequence, style.duration // 2), 0.2)


class ClientTableTest(unittest.TestCase):
    def test_animation_rows_fall_back_to_jump_start(self):
        texts = b"\0Jump\0FlyJump\0"
        source = table(8, [[handspring.JUMP, 1, 0, 257, 2, 0, 38, 0],
                           [handspring.FLY_JUMP, 6, 0, 257, 2, 229, 38, 3]], texts)
        result = handspring.add_animation_rows(source)
        count, fields, size, _ = struct.unpack_from("<4I", result, 4)
        rows = {row[0]: row for row in (struct.unpack_from("<8I", result, 20 + index * size) for index in range(count))}
        strings = result[20 + count * size:]
        for animation, name in handspring.ANIMATION_NAMES.items():
            row = rows[animation]
            self.assertEqual(strings[row[1]:strings.index(b"\0", row[1])].decode(), name)
            flying = animation in (handspring.FLY_HANDSPRING, handspring.FLY_BACK_HANDSPRING)
            self.assertEqual(row[5], handspring.FLY_JUMP_START if flying else handspring.JUMP_START)
            self.assertEqual(row[4], 2)
            self.assertEqual(row[handspring.ANIMATION_WEAPON_FLAGS], handspring.WEAPONS_SHEATHED)
        with self.assertRaises(ValueError):
            handspring.add_animation_rows(result)

    def test_vault_kits_play_the_handsprings(self):
        dust = [handspring.VAULT_DUST_KIT, 0xFFFFFFFF, handspring.JUMP_START] + [0] * 35
        result = handspring.add_visual_kits(table(38, [dust], b"\0"))
        count, _, size, _ = struct.unpack_from("<4I", result, 4)
        rows = {row[0]: row for row in (struct.unpack_from("<38I", result, 20 + index * size) for index in range(count))}
        for kit, animation in handspring.VAULT_KITS.items():
            self.assertEqual(rows[kit][handspring.KIT_ANIMATION], animation)
            self.assertEqual(rows[kit][3:], tuple(dust[3:]))


if __name__ == "__main__":
    unittest.main()
