"""Give the two-headed Ogre real eyes: calm, half-lidded eyes in the two sockets of one head and a large staring eye
on the cyclops head. Every eyeball is drawn unlit so it glows a little and sits under a skin-coloured upper lid that
drops over it to blink; the two heads blink out of step."""
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
    texture: str
    height: float
    depth: float
    sclera: tuple
    iris: float
    lid_edge: float
    blink_period: int


CALM = Look("OgreMagePCEye.blp", 0.62, 0.55, (176, 170, 160), 0.74, 22.0, 4100)
STARE = Look("OgreMagePCCyclopsEye.blp", 0.72, 0.3, (214, 222, 228), 0.62, 62.0, 5300)
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


def eyeball(centre, radius, look):
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
            uv = (0.5 + 0.5 * ny * reach, 0.5 - 0.5 * nz * reach) if nx >= 0 else (0.02, 0.02)
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
            position = (cx + nx * lid * look.depth, cy + ny * lid, cz + nz * lid * look.height)
            vertices.append((position, (nx, ny, nz), SKIN_UV))
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


def eye_texture(look, path):
    size = TEXTURE_SIZE
    yy, xx = np.mgrid[0:size, 0:size].astype(np.float32)
    r = np.hypot(xx - size / 2 + 0.5, yy - size / 2 + 0.5) / (size / 2)
    iris_outer = np.array([24, 64, 170], np.float32)
    iris_inner = np.array([110, 190, 250], np.float32)
    image = np.zeros((size, size, 3), np.float32)
    image[:] = np.array(look.sclera, np.float32) * np.clip(1.15 - 0.45 * r, 0.55, 1.0)[..., None]
    iris = r < look.iris
    t = np.clip(r / look.iris, 0, 1)[..., None]
    image[iris] = (iris_inner * (1 - t) + iris_outer * t)[iris]
    ring = (r > look.iris - 0.06) & (r < look.iris + 0.02)
    image[ring] = image[ring] * 0.45
    image[r < look.iris * 0.36] = (8, 12, 30)
    image[np.hypot(xx - size * 0.38, yy - size * 0.36) < size * 0.06] = (245, 250, 255)
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


def blink_bone(blob, m2, parent, pivot, global_sequence, look):
    sequence_count = arr(m2, 28)[0]
    period = look.blink_period
    closed = look.lid_edge - LID_LOWEST
    times = [0, period - BLINK_CLOSE - BLINK_SHUT, period - BLINK_SHUT, period - BLINK_SHUT // 2, period]
    angles = [0.0, 0.0, closed, closed, 0.0]
    time_data = blob.append(struct.pack("<%dI" % len(times), *times), 4)
    value_data = blob.append(b"".join(lid_turn(a) for a in angles), 4)
    time_table = blob.append(struct.pack("<II", len(times), time_data) * sequence_count, 4)
    value_table = blob.append(struct.pack("<II", len(angles), value_data) * sequence_count, 4)
    empty = blob.append(struct.pack("<II", 0, 0) * sequence_count, 4)
    still = struct.pack("<HhIIII", 0, -1, sequence_count, empty, sequence_count, empty)
    turning = struct.pack("<HhIIII", LINEAR, global_sequence, sequence_count, time_table, sequence_count, value_table)
    return (struct.pack("<iIhHI", -1, TRANSFORMED_BONE, parent, 0, 0) + still + turning + still +
            struct.pack("<3f", *pivot))


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

    first_global = append_array(blob, m2, 20, 4,
                                struct.pack("<%dI" % len(heads), *[HEAD_LOOK[h].blink_period for h in heads]))
    bone_count = arr(m2, 44)[0]
    new_bones = b""
    for i, (centre, _, head, look) in enumerate(eyes):
        new_bones += blink_bone(blob, m2, head, centre, first_global + heads.index(head), look)
    append_array(blob, m2, 44, 88, new_bones, 16)

    texture_count = arr(m2, 80)[0]
    texture_records = b""
    for head in heads:
        name = (TEXTURE_FOLDER + HEAD_LOOK[head].texture).encode() + b"\0"
        texture_records += struct.pack("<IIII", 0, 0, len(name), blob.append(name, 4))
    append_array(blob, m2, 80, 16, texture_records, 16)
    first_lookup = append_array(blob, m2, 128, 2,
                                struct.pack("<%dh" % len(heads), *range(texture_count, texture_count + len(heads))))
    eye_lookup = {head: first_lookup + i for i, head in enumerate(heads)}
    eye_material = append_array(blob, m2, 112, 4, struct.pack("<HH", UNLIT, 0))

    bones_used = heads + [bone_count + i for i in range(len(eyes))]
    first_combo = append_array(blob, m2, 120, 2, struct.pack("<%dH" % len(bones_used), *bones_used))
    combo = {bone: first_combo + i for i, bone in enumerate(bones_used)}

    meshes = []
    for i, (centre, radius, head, look) in enumerate(eyes):
        vertices, triangles = eyeball(centre, radius, look)
        meshes.append((vertices, outward(vertices, triangles, centre), head, centre, radius,
                       eye_lookup[head], eye_material))
        vertices, triangles = upper_lid(centre, radius, look)
        meshes.append((vertices, outward(vertices, triangles, centre), bone_count + i, centre, radius * LID_SCALE,
                       body_lookup, body_material))

    vertex_count = arr(m2, 60)[0]
    new_vertices = bytearray()
    for vertices, _, bone, _, _, _, _ in meshes:
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
    for vertices, triangles, bone, centre, radius, lookup, material in meshes:
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
        batches += struct.pack("<BbHHHhHHHHHHH", 16, 0, 0, section, section, -1, material, 0, 1, lookup, 0, 0, 0)
    for header, payload, size in ((4, skin_vertices, 2), (12, skin_triangles, 2), (20, properties, 4),
                                  (28, submeshes, 48), (36, batches, 24)):
        offset = sk.append(bytes(payload), 4)
        struct.pack_into("<II", sk.data, header, len(payload) // size, offset)
    open(out_m2, "wb").write(blob.data)
    open(out_skin, "wb").write(sk.data)
    for look in set(HEAD_LOOK.values()):
        eye_texture(look, Path(out_folder) / look.texture)
    print("eyes", len(eyes), "vertices", sum(len(m[0]) for m in meshes))


if __name__ == "__main__":
    graft(*sys.argv[1:6])
