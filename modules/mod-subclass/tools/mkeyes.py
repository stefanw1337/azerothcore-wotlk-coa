"""Give the two-headed Ogre real eyes: calm, half-lidded eyes in the two sockets of one head and a large staring eye
on the cyclops head. Every eyeball is drawn unlit so it glows a little and sits under a skin-coloured upper lid that
drops over it to blink; the two heads blink out of step, and their irises glance around on their own.
The eyeballs draw the character's hair texture, so CharSections hair colours choose the eyes: one atlas per eye set,
stacked as the outer eye, the inner eye and the cyclops eye of that set, padded to a power-of-two height."""
import math
import struct
import sys
from dataclasses import dataclass
from pathlib import Path

import numpy as np
from PIL import Image

from mkcape import Blob, arr, compress


@dataclass(frozen=True)
class Look:
    height: float
    depth: float
    iris: float
    lid_edge: float
    blink_period: int
    gaze_period: int
    gaze: tuple


@dataclass(frozen=True)
class EyeColor:
    name: str
    inner: tuple
    outer: tuple
    sclera: tuple = (128, 120, 104)
    pupil: tuple = (10, 8, 6)
    glow: bool = False


CALM_GAZE = ((0, 0.0, 0.0), (1500, 0.0, 0.0), (1800, 0.13, 0.0), (2900, 0.13, 0.0), (3200, 0.0, 0.0),
             (4700, 0.0, 0.0), (5000, -0.13, 0.02), (6000, -0.13, 0.02), (6300, 0.0, 0.0), (7600, 0.0, 0.0),
             (7800, 0.0, 0.07), (8400, 0.0, 0.07), (8700, 0.0, 0.0), (9900, 0.0, 0.0))
STARE_GAZE = ((0, 0.0, 0.0), (900, 0.0, 0.0), (1050, -0.16, 0.0), (1900, -0.16, 0.0), (2050, 0.16, 0.0),
              (3100, 0.16, 0.0), (3300, 0.0, 0.0), (4600, 0.0, 0.0), (4750, 0.05, -0.08), (5400, 0.05, -0.08),
              (5600, 0.0, 0.0), (7300, 0.0, 0.0))
CALM = Look(0.62, 0.55, 0.74, 22.0, 4100, 9900, CALM_GAZE)
STARE = Look(0.72, 0.3, 0.62, 62.0, 5300, 7300, STARE_GAZE)
EYE_COLORS = (
    EyeColor("Brown", (132, 92, 48), (58, 36, 18)),
    EyeColor("Hazel", (150, 132, 62), (70, 62, 26)),
    EyeColor("Amber", (190, 130, 40), (110, 62, 14)),
    EyeColor("Green", (96, 130, 60), (38, 62, 24)),
    EyeColor("Grey", (130, 132, 128), (60, 64, 66)),
    EyeColor("Blue", (96, 124, 150), (36, 54, 80)),
    EyeColor("Ice", (170, 200, 215), (80, 110, 130)),
    EyeColor("Violet", (130, 100, 150), (60, 40, 80)),
    EyeColor("Black", (40, 34, 30), (14, 12, 10)),
    EyeColor("Red", (150, 52, 36), (70, 18, 12)),
    EyeColor("FelGlow", (170, 255, 90), (40, 160, 20), (150, 190, 120), (20, 60, 10), glow=True),
    EyeColor("ArcaneGlow", (170, 220, 255), (40, 110, 230), (140, 170, 200), (10, 30, 70), glow=True),
    EyeColor("FireGlow", (255, 220, 120), (230, 90, 20), (200, 150, 110), (70, 20, 5), glow=True),
    EyeColor("HolyGlow", (255, 250, 200), (230, 190, 70), (210, 200, 160), (90, 70, 20), glow=True),
    EyeColor("ShadowGlow", (220, 150, 255), (120, 40, 200), (150, 120, 170), (30, 5, 50), glow=True),
)
COLOR = {color.name: color for color in EYE_COLORS}
NATURAL = ("Brown", "Hazel", "Amber", "Green", "Grey", "Blue", "Ice", "Violet", "Black", "Red")
GLOWING = ("FelGlow", "ArcaneGlow", "FireGlow", "HolyGlow", "ShadowGlow")
SIGHTED = NATURAL + GLOWING
ONE_BLIND = ((2,), (0,), (1,))
TWO_BLIND = ((0, 1), (0, 2), (1, 2))
ALL_BLIND = ((0, 1, 2),)


def blinded(color, blind_eyes):
    return tuple((color, eye in blind_eyes) for eye in range(3))


def seeing(*colors):
    return tuple((color, False) for color in colors)


EYE_SETS = (
    tuple(seeing(name, name, name) for name in SIGHTED) +
    tuple(blinded(name, eyes) for name in SIGHTED for eyes in ONE_BLIND + TWO_BLIND + ALL_BLIND) +
    tuple(seeing(name, name, glow) for name, glow in (("Brown", "FelGlow"), ("Grey", "ArcaneGlow"),
                                                      ("Black", "FireGlow"), ("Blue", "HolyGlow"),
                                                      ("Hazel", "ShadowGlow"))) +
    (seeing("Blue", "Brown", "Green"), seeing("Green", "Blue", "Brown"))
)
CLOUD = (226, 230, 234)
CLOUD_STRENGTH = 0.68
SORE = (150, 42, 36)
SORE_FROM = 0.42
SORE_STRENGTH = 0.55
HAIR_TEXTURE = 6
ATTACHMENTS, ATTACHMENT_LOOKUP, ATTACHMENT_SIZE, ATTACHMENT_POSITION = 240, 248, 40, 8
HELMET_ATTACHMENT = 11
HELMET_SHIFT = (-0.1, 0.0, -0.17)
HELMET_SCALE = (0.95, 1.08, 0.95)
HELMET_TILT = 0.0
CYCLOPS_HELMET_ATTACHMENT = 25
CYCLOPS_HELMET_SHIFT = (-0.02, -0.03, -0.03)
ATTACHMENT_SCALE = 1.9
ANIMATED = b"\x01"
BONE_PIVOT = 76
CAMERAS, CAMERA_SIZE, CAMERA_POSITION, CAMERA_TARGET = 272, 100, 36, 68
CHARACTER_CAMERA = 1
CHARACTER_CAMERA_PULL = 0.72
CHARACTER_CAMERA_SHIFT = (0.0, -0.32, 0.48)
CELLS = 4
TWO_EYED_HEAD, CYCLOPS_HEAD = 21, 20
HEAD_LOOK = {TWO_EYED_HEAD: CALM, CYCLOPS_HEAD: STARE}
EYES = [
    ((-0.273, 2.625), 0.030, TWO_EYED_HEAD),
    ((-0.128, 2.625), 0.030, TWO_EYED_HEAD),
    ((0.198, 2.618), 0.066, CYCLOPS_HEAD),
]
EYE_FRONT_X = {TWO_EYED_HEAD: 0.208, CYCLOPS_HEAD: 0.250}
TEXTURE_FOLDER = "Character\\Ogre\\Female\\"
SKIN_UV = (0.272, 0.814)
LID_SCALE = 1.12
LID_SPAN = 175.0
LID_LOWEST = -72.0
BLINK_CLOSE, BLINK_SHUT = 90, 70
LINEAR = 1
TRANSFORMED_BONE = 0x200
UNLIT = 0x01
BODY_TEXTURE = 1
RINGS, SEGMENTS = 9, 14
LID_ROWS, LID_COLUMNS = 8, 12
TEXTURE_SIZE = 128


def eyeball(centre, radius, look, cell):
    cx, cy, cz = centre
    vertices = []
    for ring in range(RINGS + 1):
        polar = math.pi * ring / RINGS
        for segment in range(SEGMENTS):
            azimuth = 2 * math.pi * segment / SEGMENTS
            nx = math.cos(polar)
            ny = math.sin(polar) * math.cos(azimuth)
            nz = math.sin(polar) * math.sin(azimuth)
            position = (cx + nx * radius * look.depth, cy + ny * radius, cz + nz * radius * look.height)
            reach = min(1.0, 1.6 * math.sin(polar))
            u, v = (0.5 + 0.5 * ny * reach, 0.5 - 0.5 * nz * reach) if nx >= 0 else (0.02, 0.02)
            uv = (u, (cell + v) / CELLS)
            vertices.append((position, (nx, ny, nz), uv))
    triangles = []
    for ring in range(RINGS):
        for segment in range(SEGMENTS):
            a = ring * SEGMENTS + segment
            b = ring * SEGMENTS + (segment + 1) % SEGMENTS
            c, d = a + SEGMENTS, b + SEGMENTS
            triangles += [a, c, b, b, c, d]
    return vertices, triangles


def upper_lid(centre, radius, look):
    cx, cy, cz = centre
    lid = radius * LID_SCALE
    vertices = []
    for row in range(LID_ROWS + 1):
        elevation = math.radians(look.lid_edge + LID_SPAN * row / LID_ROWS)
        for column in range(LID_COLUMNS + 1):
            side = math.radians(-90 + 180 * column / LID_COLUMNS)
            nx = math.cos(elevation) * math.cos(side)
            ny = math.cos(elevation) * math.sin(side)
            nz = math.sin(elevation)
            vertices.append(((cx + nx * lid, cy + ny * lid, cz + nz * lid), (nx, ny, nz), SKIN_UV))
    triangles = []
    for row in range(LID_ROWS):
        for column in range(LID_COLUMNS):
            a = row * (LID_COLUMNS + 1) + column
            b, c, d = a + 1, a + LID_COLUMNS + 1, a + LID_COLUMNS + 2
            triangles += [a, b, c, b, d, c]
    return vertices, triangles


def outward(vertices, triangles, centre):
    points = np.array([v[0] for v in vertices])
    fixed = []
    for i in range(0, len(triangles), 3):
        a, b, c = triangles[i:i + 3]
        normal = np.cross(points[b] - points[a], points[c] - points[a])
        middle = (points[a] + points[b] + points[c]) / 3
        fixed += [a, b, c] if np.dot(normal, middle - np.array(centre)) >= 0 else [a, c, b]
    return fixed


def eye_cell(look, color, blind=False):
    size = TEXTURE_SIZE
    yy, xx = np.mgrid[0:size, 0:size].astype(np.float32)
    r = np.hypot(xx - size / 2 + 0.5, yy - size / 2 + 0.5) / (size / 2)
    outer = np.array(color.outer, np.float32)
    inner = np.array(color.inner, np.float32)
    sclera = np.array(color.sclera, np.float32)
    image = np.zeros((size, size, 3), np.float32)
    image[:] = sclera * np.clip(1.15 - 0.45 * r, 0.55, 1.0)[..., None]
    if color.glow:
        halo = np.clip(1.0 - (r - look.iris) / 0.5, 0, 1)[..., None] ** 2
        image = image * (1 - halo) + outer * halo
    iris = r < look.iris
    t = np.clip(r / look.iris, 0, 1)[..., None]
    image[iris] = (inner * (1 - t) + outer * t)[iris]
    ring = (r > look.iris - 0.06) & (r < look.iris + 0.02)
    image[ring] = image[ring] * (0.75 if color.glow else 0.45)
    image[r < look.iris * (0.28 if color.glow else 0.36)] = color.pupil
    image[np.hypot(xx - size * 0.38, yy - size * 0.36) < size * 0.05] = (176, 170, 160)
    if blind:
        cloud = (np.clip(1.15 - r / look.iris * 0.5, 0, 1) * CLOUD_STRENGTH)[..., None]
        image = image * (1 - cloud) + np.array(CLOUD, np.float32) * cloud
        sore = (np.clip((r - SORE_FROM) / (1.0 - SORE_FROM), 0, 1) ** 1.5)[..., None] * SORE_STRENGTH
        image = image * (1 - sore) + np.array(SORE, np.float32) * sore
    return image


def eye_texture_name(index):
    return TEXTURE_FOLDER + "TwoHeadedOgreEyes%02d.blp" % index


def eye_atlas(eye_set, path):
    looks = [HEAD_LOOK[head] for _, _, head in EYES]
    cells = [eye_cell(look, COLOR[name], blind) for look, (name, blind) in zip(looks, eye_set)]
    cells += [cells[-1]] * (CELLS - len(cells))
    image = np.vstack(cells)
    write_blp(Image.fromarray(np.clip(image, 0, 255).astype(np.uint8)), path)


def write_blp(img, path):
    rgb = img.convert("RGB")
    paletted = rgb.quantize(256, method=Image.Quantize.MEDIANCUT)
    palette = paletted.getpalette()[:768]
    palette += [0] * (768 - len(palette))
    mips, width, height, level = [], rgb.width, rgb.height, rgb
    while True:
        mips.append(level.quantize(palette=paletted, dither=Image.Dither.NONE).tobytes())
        if width == 1 and height == 1:
            break
        width, height = max(1, width // 2), max(1, height // 2)
        level = rgb.resize((width, height), Image.LANCZOS)
    mips = mips[:16]
    cursor = 20 + 16 * 4 * 2 + 256 * 4
    offsets, sizes = [], []
    for mip in mips:
        offsets.append(cursor)
        sizes.append(len(mip))
        cursor += len(mip)
    offsets += [0] * (16 - len(offsets))
    sizes += [0] * (16 - len(sizes))
    out = bytearray(b"BLP2") + struct.pack("<I4B2I", 1, 1, 0, 8, 1, rgb.width, rgb.height)
    out += struct.pack("<16I", *offsets) + struct.pack("<16I", *sizes)
    for i in range(256):
        r, g, b = palette[i * 3:i * 3 + 3]
        out += bytes((b, g, r, 255))
    for mip in mips:
        out += mip
    open(path, "wb").write(out)


def lid_turn(degrees):
    half = math.radians(degrees) / 2
    return struct.pack("<4h", compress(0.0), compress(math.sin(half)), compress(0.0), compress(math.cos(half)))


def track(blob, m2, global_sequence, times, values):
    sequence_count = arr(m2, 28)[0]
    time_data = blob.append(struct.pack("<%dI" % len(times), *times), 4)
    value_data = blob.append(b"".join(values), 4)
    time_table = blob.append(struct.pack("<II", len(times), time_data) * sequence_count, 4)
    value_table = blob.append(struct.pack("<II", len(values), value_data) * sequence_count, 4)
    return struct.pack("<HhIIII", LINEAR, global_sequence, sequence_count, time_table, sequence_count, value_table)


def still_track(blob, m2):
    sequence_count = arr(m2, 28)[0]
    empty = blob.append(struct.pack("<II", 0, 0) * sequence_count, 4)
    return struct.pack("<HhIIII", 0, -1, sequence_count, empty, sequence_count, empty)


def bone_record(parent, pivot, translation, rotation, scale):
    return (struct.pack("<iIhHI", -1, TRANSFORMED_BONE, parent, 0, 0) + translation + rotation + scale +
            struct.pack("<3f", *pivot))


def lid_bones(blob, m2, head, shape_index, pivot, global_sequence, look):
    still = still_track(blob, m2)
    stretch = track(blob, m2, global_sequence, [0], [struct.pack("<3f", look.depth, 1.0, look.height)])
    shape = bone_record(head, pivot, still, still, stretch)
    period = look.blink_period
    closed = look.lid_edge - LID_LOWEST
    times = [0, period - BLINK_CLOSE - BLINK_SHUT, period - BLINK_SHUT, period - BLINK_SHUT // 2, period]
    angles = [0.0, 0.0, closed, closed, 0.0]
    turning = track(blob, m2, global_sequence, times, [lid_turn(a) for a in angles])
    return shape + bone_record(shape_index, pivot, still, turning, still)


def append_array(blob, m2, header, size, payload, align=4):
    count, offset = arr(m2, header)
    new = blob.append(m2[offset:offset + count * size] + payload, align)
    struct.pack_into("<II", blob.data, header, count + len(payload) // size, new)
    return count


def body_drawing(m2, skin):
    texture_count, texture_offset = arr(m2, 80)
    body = next(i for i in range(texture_count)
                if struct.unpack_from("<I", m2, texture_offset + i * 16)[0] == BODY_TEXTURE)
    lookup_count, lookup_offset = arr(m2, 128)
    lookups = struct.unpack_from("<%dh" % lookup_count, m2, lookup_offset)
    submesh_count, submesh_offset = arr(skin, 28)
    batch_count, batch_offset = arr(skin, 36)
    for i in range(batch_count):
        batch = struct.unpack_from("<BbHHHhHHHHHHH", skin, batch_offset + i * 24)
        if lookups[batch[9]] == body:
            return batch[9], batch[6]
    raise ValueError("no body batch")


def frame_closer(data):
    count, offset = arr(data, CAMERAS)
    for index in range(count):
        camera = offset + index * CAMERA_SIZE
        if struct.unpack_from("<I", data, camera)[0] != CHARACTER_CAMERA:
            continue
        position = np.array(struct.unpack_from("<3f", data, camera + CAMERA_POSITION))
        target = np.array(struct.unpack_from("<3f", data, camera + CAMERA_TARGET))
        shift = np.array(CHARACTER_CAMERA_SHIFT)
        struct.pack_into("<3f", data, camera + CAMERA_POSITION,
                         *(target + (position - target) * CHARACTER_CAMERA_PULL + shift))
        struct.pack_into("<3f", data, camera + CAMERA_TARGET, *(target + shift))


def helmet_seat(m2):
    count, offset = arr(m2, ATTACHMENTS)
    bone_offset = arr(m2, 44)[1]
    for index in range(count):
        attachment = offset + index * ATTACHMENT_SIZE
        attachment_id, head_bone = struct.unpack_from("<IH", m2, attachment)
        if attachment_id == HELMET_ATTACHMENT:
            position = np.array(struct.unpack_from("<3f", m2, attachment + ATTACHMENT_POSITION))
            position[1] = struct.unpack_from("<f", m2, bone_offset + head_bone * 88 + BONE_PIVOT + 4)[0]
            return index, head_bone, position + np.array(HELMET_SHIFT)
    raise ValueError("no helmet attachment")


def cyclops_helmet_seat(helmet_position):
    return helmet_position * (1.0, -1.0, 1.0) + np.array(CYCLOPS_HELMET_SHIFT)


def add_attachment(blob, m2, attachment_id, bone, position, global_sequence):
    record = struct.pack("<IHH3f", attachment_id, bone, 0, *position)
    record += track(blob, m2, global_sequence, [0], [ANIMATED])
    index = append_array(blob, m2, ATTACHMENTS, ATTACHMENT_SIZE, record)
    struct.pack_into("<h", blob.data, arr(blob.data, ATTACHMENT_LOOKUP)[1] + attachment_id * 2, index)


def seat_helmet(data, index, helmet_bone, position):
    attachment = arr(data, ATTACHMENTS)[1] + index * ATTACHMENT_SIZE
    struct.pack_into("<H", data, attachment + 4, helmet_bone)
    struct.pack_into("<3f", data, attachment + ATTACHMENT_POSITION, *position)
    struct.pack_into("<h", data, arr(data, ATTACHMENT_LOOKUP)[1] + HELMET_ATTACHMENT * 2, index)


def graft(m2_path, skin_path, out_m2, out_skin, out_folder):
    m2 = open(m2_path, "rb").read()
    skin = open(skin_path, "rb").read()
    blob = Blob(m2)
    heads = sorted(HEAD_LOOK)
    body_lookup, body_material = body_drawing(m2, skin)

    eyes = []
    for (cy, cz), radius, head in EYES:
        look = HEAD_LOOK[head]
        eyes.append(((EYE_FRONT_X[head] - radius * look.depth, cy, cz), radius, head, look))

    periods = [HEAD_LOOK[h].blink_period for h in heads] + [HEAD_LOOK[h].gaze_period for h in heads]
    first_global = append_array(blob, m2, 20, 4, struct.pack("<%dI" % len(periods), *periods))
    gaze_global = {head: first_global + len(heads) + i for i, head in enumerate(heads)}
    bone_count = arr(m2, 44)[0]
    new_bones = b""
    for i, (centre, _, head, look) in enumerate(eyes):
        new_bones += lid_bones(blob, m2, head, bone_count + 2 * i, centre, first_global + heads.index(head), look)
    helmet_index, head_bone, helmet_position = helmet_seat(m2)
    helmet_bone = bone_count + 2 * len(eyes)
    new_bones += bone_record(head_bone, helmet_position, still_track(blob, m2),
                             track(blob, m2, first_global, [0], [lid_turn(HELMET_TILT)]),
                             track(blob, m2, first_global, [0], [struct.pack("<3f", *HELMET_SCALE)]))
    cyclops_seat = cyclops_helmet_seat(helmet_position)
    new_bones += bone_record(CYCLOPS_HEAD, cyclops_seat, still_track(blob, m2), still_track(blob, m2),
                             still_track(blob, m2))
    append_array(blob, m2, 44, 88, new_bones, 16)
    add_attachment(blob, m2, CYCLOPS_HELMET_ATTACHMENT, helmet_bone + 1, cyclops_seat, first_global)

    hair_texture = append_array(blob, m2, 80, 16, struct.pack("<IIII", HAIR_TEXTURE, 0, 1, blob.append(b"\0", 4)), 16)
    eye_texture_lookup = append_array(blob, m2, 128, 2, struct.pack("<h", hair_texture))
    eye_lookup = {head: eye_texture_lookup for head in heads}
    eye_material = append_array(blob, m2, 112, 4, struct.pack("<HH", UNLIT, 0))
    transforms = b""
    for head in heads:
        gaze = HEAD_LOOK[head].gaze
        sliding = track(blob, m2, gaze_global[head], [g[0] for g in gaze],
                        [struct.pack("<3f", g[1], g[2] / CELLS, 0.0) for g in gaze])
        transforms += sliding + still_track(blob, m2) + still_track(blob, m2)
    first_transform = append_array(blob, m2, 96, 60, transforms, 16)
    first_transform_lookup = append_array(blob, m2, 152, 2, struct.pack(
        "<%dh" % len(heads), *range(first_transform, first_transform + len(heads))))
    gaze_lookup = {head: first_transform_lookup + i for i, head in enumerate(heads)}

    bones_used = heads + [bone_count + 2 * i + 1 for i in range(len(eyes))]
    first_combo = append_array(blob, m2, 120, 2, struct.pack("<%dH" % len(bones_used), *bones_used))
    combo = {bone: first_combo + i for i, bone in enumerate(bones_used)}

    meshes = []
    for i, (centre, radius, head, look) in enumerate(eyes):
        vertices, triangles = eyeball(centre, radius, look, i)
        meshes.append((vertices, outward(vertices, triangles, centre), head, centre, radius,
                       eye_lookup[head], eye_material, gaze_lookup[head]))
        vertices, triangles = upper_lid(centre, radius, look)
        lid_bone = bone_count + 2 * i + 1
        meshes.append((vertices, outward(vertices, triangles, centre), lid_bone, centre, radius * LID_SCALE,
                       body_lookup, body_material, 0))

    vertex_count = arr(m2, 60)[0]
    new_vertices = bytearray()
    for vertices, _, bone, _, _, _, _, _ in meshes:
        for position, normal, uv in vertices:
            new_vertices += struct.pack("<3f4B4B3f2f2f", *position, 255, 0, 0, 0, bone, 0, 0, 0, *normal, *uv,
                                        0.0, 0.0)
    append_array(blob, m2, 60, 48, bytes(new_vertices), 16)

    sk = Blob(skin)
    skin_vertex_count, skin_vertex_offset = arr(skin, 4)
    triangle_count, triangle_offset = arr(skin, 12)
    property_count, property_offset = arr(skin, 20)
    submesh_count, submesh_offset = arr(skin, 28)
    batch_count, batch_offset = arr(skin, 36)
    skin_vertices = bytearray(skin[skin_vertex_offset:skin_vertex_offset + skin_vertex_count * 2])
    skin_triangles = bytearray(skin[triangle_offset:triangle_offset + triangle_count * 2])
    properties = bytearray(skin[property_offset:property_offset + property_count * 4])
    submeshes = bytearray(skin[submesh_offset:submesh_offset + submesh_count * 48])
    batches = bytearray(skin[batch_offset:batch_offset + batch_count * 24])
    template = bytes(submeshes[:48])
    first_vertex = vertex_count
    for vertices, triangles, bone, centre, radius, lookup, material, transform in meshes:
        first_skin_vertex = len(skin_vertices) // 2
        first_triangle = len(skin_triangles) // 2
        for i in range(len(vertices)):
            skin_vertices += struct.pack("<H", first_vertex + i)
            properties += bytes(4)
        first_vertex += len(vertices)
        skin_triangles += struct.pack("<%dH" % len(triangles), *[first_skin_vertex + t for t in triangles])
        submesh = bytearray(template)
        struct.pack_into("<10H", submesh, 0, 0, first_triangle >> 16, first_skin_vertex, len(vertices),
                         first_triangle & 0xFFFF, len(triangles), 1, combo[bone], 1, bone)
        struct.pack_into("<3f", submesh, 20, *centre)
        struct.pack_into("<3f", submesh, 32, *centre)
        struct.pack_into("<f", submesh, 44, radius)
        section = len(submeshes) // 48
        submeshes += submesh
        batches += struct.pack("<BbHHHhHHHHHHH", 16, 0, 0, section, section, -1, material, 0, 1, lookup, 0, 0,
                               transform)
    for header, payload, size in ((4, skin_vertices, 2), (12, skin_triangles, 2), (20, properties, 4),
                                  (28, submeshes, 48), (36, batches, 24)):
        offset = sk.append(bytes(payload), 4)
        struct.pack_into("<II", sk.data, header, len(payload) // size, offset)
    frame_closer(blob.data)
    seat_helmet(blob.data, helmet_index, helmet_bone, helmet_position)
    open(out_m2, "wb").write(blob.data)
    open(out_skin, "wb").write(sk.data)
    for index, eye_set in enumerate(EYE_SETS):
        eye_atlas(eye_set, Path(out_folder) / Path(eye_texture_name(index).replace("\\", "/")).name)
    print("eyes", len(eyes), "vertices", sum(len(m[0]) for m in meshes))


if __name__ == "__main__":
    graft(*sys.argv[1:6])
