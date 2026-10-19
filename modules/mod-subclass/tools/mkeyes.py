"""Give the two-headed Ogre real eyeballs: calm, half-lidded eyes in the two sockets of one head and a large staring
eye in the cyclops head's single socket. Each head's eyes hang from their own blinking bone, the heads blink out of
step, and the eyes are drawn unlit so they glow a little."""
import math
import struct
import sys
from dataclasses import dataclass
from pathlib import Path

import numpy as np
from PIL import Image

from mkcape import Blob, arr


@dataclass(frozen=True)
class Look:
    texture: str
    height: float
    sunken: float
    sclera: tuple
    iris: float
    lid: float
    blink_period: int


CALM = Look("OgreMagePCEye.blp", 0.62, 0.85, (176, 170, 160), 0.74, 0.55, 4100)
STARE = Look("OgreMagePCCyclopsEye.blp", 0.72, 0.55, (214, 222, 228), 0.62, 0.0, 5300)
TWO_EYED_HEAD, CYCLOPS_HEAD = 21, 20
HEAD_LOOK = {TWO_EYED_HEAD: CALM, CYCLOPS_HEAD: STARE}
EYES = [
    ((-0.273, 2.625), 0.034, TWO_EYED_HEAD),
    ((-0.128, 2.625), 0.034, TWO_EYED_HEAD),
    ((0.198, 2.618), 0.056, CYCLOPS_HEAD),
]
SURFACE_X = {TWO_EYED_HEAD: 0.236, CYCLOPS_HEAD: 0.247}
TEXTURE_FOLDER = "Character\\Ogre\\Female\\"
DEPTH = 0.55
BLINK_CLOSE, BLINK_SHUT = 90, 60
CLOSED = 0.06
LINEAR = 1
TRANSFORMED_BONE = 0x200
UNLIT = 0x01
RINGS, SEGMENTS = 9, 14
TEXTURE_SIZE = 128


def eyeball(centre, radius, height):
    cx, cy, cz = centre
    vertices = []
    for ring in range(RINGS + 1):
        polar = math.pi * ring / RINGS
        for segment in range(SEGMENTS):
            azimuth = 2 * math.pi * segment / SEGMENTS
            nx = math.cos(polar)
            ny = math.sin(polar) * math.cos(azimuth)
            nz = math.sin(polar) * math.sin(azimuth)
            position = (cx + nx * radius * DEPTH, cy + ny * radius, cz + nz * radius * height)
            reach = min(1.0, 1.6 * math.sin(polar))
            uv = (0.5 + 0.5 * ny * reach, 0.5 - 0.5 * nz * reach) if nx >= 0 else (0.02, 0.02)
            vertices.append((position, (nx, ny, nz), uv))
    triangles = []
    for ring in range(RINGS):
        for segment in range(SEGMENTS):
            a = ring * SEGMENTS + segment
            b = ring * SEGMENTS + (segment + 1) % SEGMENTS
            c, d = a + SEGMENTS, b + SEGMENTS
            triangles += [a, b, c, b, d, c]
    return vertices, triangles


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
    lid = np.clip((size * 0.42 - yy) / (size * 0.42), 0, 1)[..., None]
    image *= 1 - look.lid * lid
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


def blink_bone(blob, m2, parent, pivot, global_sequence, period):
    sequence_count = arr(m2, 28)[0]
    times = [0, period - BLINK_CLOSE - BLINK_SHUT, period - BLINK_SHUT, period - BLINK_SHUT // 2, period]
    scales = [1.0, 1.0, CLOSED, CLOSED, 1.0]
    time_data = blob.append(struct.pack("<%dI" % len(times), *times), 4)
    value_data = blob.append(b"".join(struct.pack("<3f", 1.0, 1.0, s) for s in scales), 4)
    time_table = blob.append(struct.pack("<II", len(times), time_data) * sequence_count, 4)
    value_table = blob.append(struct.pack("<II", len(scales), value_data) * sequence_count, 4)
    empty = blob.append(struct.pack("<II", 0, 0) * sequence_count, 4)
    still = struct.pack("<HhIIII", 0, -1, sequence_count, empty, sequence_count, empty)
    blinking = struct.pack("<HhIIII", LINEAR, global_sequence, sequence_count, time_table, sequence_count, value_table)
    return (struct.pack("<iIhHI", -1, TRANSFORMED_BONE, parent, 0, 0) + still + still + blinking +
            struct.pack("<3f", *pivot))


def append_array(blob, m2, header, size, payload, align=4):
    count, offset = arr(m2, header)
    new = blob.append(m2[offset:offset + count * size] + payload, align)
    struct.pack_into("<II", blob.data, header, count + len(payload) // size, new)
    return count


def graft(m2_path, skin_path, out_m2, out_skin, out_folder):
    m2 = open(m2_path, "rb").read()
    skin = open(skin_path, "rb").read()
    blob = Blob(m2)
    heads = sorted(HEAD_LOOK)

    first_global = append_array(blob, m2, 20, 4,
                                struct.pack("<%dI" % len(heads), *[HEAD_LOOK[h].blink_period for h in heads]))
    meshes = []
    for (cy, cz), radius, head in EYES:
        look = HEAD_LOOK[head]
        centre = (SURFACE_X[head] - radius * DEPTH * look.sunken, cy, cz)
        meshes.append((*eyeball(centre, radius, look.height), head, centre, radius))
    bone_count = arr(m2, 44)[0]
    blinker = {head: bone_count + i for i, head in enumerate(heads)}
    new_bones = b""
    for i, head in enumerate(heads):
        centres = [m[3] for m in meshes if m[2] == head]
        pivot = tuple(float(np.mean([c[a] for c in centres])) for a in range(3))
        new_bones += blink_bone(blob, m2, head, pivot, first_global + i, HEAD_LOOK[head].blink_period)
    append_array(blob, m2, 44, 88, new_bones, 16)

    vertex_count = arr(m2, 60)[0]
    new_vertices = bytearray()
    for vertices, _, head, _, _ in meshes:
        for position, normal, uv in vertices:
            new_vertices += struct.pack("<3f4B4B3f2f2f", *position, 255, 0, 0, 0, blinker[head], 0, 0, 0, *normal,
                                        *uv, 0.0, 0.0)
    append_array(blob, m2, 60, 48, bytes(new_vertices), 16)

    texture_count = arr(m2, 80)[0]
    texture_records = b""
    for head in heads:
        name = (TEXTURE_FOLDER + HEAD_LOOK[head].texture).encode() + b"\0"
        texture_records += struct.pack("<IIII", 0, 0, len(name), blob.append(name, 4))
    append_array(blob, m2, 80, 16, texture_records, 16)
    first_lookup = append_array(blob, m2, 128, 2,
                                struct.pack("<%dh" % len(heads), *range(texture_count, texture_count + len(heads))))
    texture_lookup = {head: first_lookup + i for i, head in enumerate(heads)}
    eye_material = append_array(blob, m2, 112, 4, struct.pack("<HH", UNLIT, 0))
    first_combo = append_array(blob, m2, 120, 2, struct.pack("<%dH" % len(heads), *[blinker[h] for h in heads]))
    combo = {head: first_combo + i for i, head in enumerate(heads)}

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
    for vertices, triangles, head, centre, radius in meshes:
        first_skin_vertex = len(skin_vertices) // 2
        first_triangle = len(skin_triangles) // 2
        for i in range(len(vertices)):
            skin_vertices += struct.pack("<H", first_vertex + i)
            properties += bytes(4)
        first_vertex += len(vertices)
        skin_triangles += struct.pack("<%dH" % len(triangles), *[first_skin_vertex + t for t in triangles])
        submesh = bytearray(template)
        struct.pack_into("<10H", submesh, 0, 0, first_triangle >> 16, first_skin_vertex, len(vertices),
                         first_triangle & 0xFFFF, len(triangles), 1, combo[head], 1, blinker[head])
        struct.pack_into("<3f", submesh, 20, *centre)
        struct.pack_into("<3f", submesh, 32, *centre)
        struct.pack_into("<f", submesh, 44, radius)
        section = len(submeshes) // 48
        submeshes += submesh
        batches += struct.pack("<BbHHHhHHHHHHH", 16, 0, 0, section, section, -1, eye_material, 0, 1,
                               texture_lookup[head], 0, 0, 0)
    for header, payload, size in ((4, skin_vertices, 2), (12, skin_triangles, 2), (20, properties, 4),
                                  (28, submeshes, 48), (36, batches, 24)):
        offset = sk.append(bytes(payload), 4)
        struct.pack_into("<II", sk.data, header, len(payload) // size, offset)
    open(out_m2, "wb").write(blob.data)
    open(out_skin, "wb").write(sk.data)
    for look in set(HEAD_LOOK.values()):
        eye_texture(look, Path(out_folder) / look.texture)
    print("eyes", len(meshes), "vertices", sum(len(m[0]) for m in meshes))


if __name__ == "__main__":
    graft(*sys.argv[1:6])
