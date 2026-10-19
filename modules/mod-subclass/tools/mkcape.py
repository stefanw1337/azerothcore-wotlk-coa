import struct, sys
import numpy as np

CAPE_GEOSETS = range(1501, 1511)
CAPE_BONES = [25, 16, 9, 3, 1]
COLUMNS, ROWS = 9, 14
TOP, BOTTOM = 1.60, 0.42
TEXTURE_CAPE = 2
TWO_SIDED_MATERIAL = 0

def arr(data, offset):
    return struct.unpack_from('<II', data, offset)

def back_profile(m2, skin):
    vertex_count, vertex_offset = arr(m2, 60)
    raw = np.frombuffer(m2, np.uint8, vertex_count * 48, vertex_offset).reshape(-1, 48)
    positions = raw[:, :12].copy().view(np.float32).reshape(-1, 3)
    index_count, index_offset = arr(skin, 4)
    lookup = np.frombuffer(skin, np.uint16, index_count, index_offset)
    count, offset = arr(skin, 28)
    body = []
    for i in range(count):
        geoset, level, start, size = struct.unpack_from('<4H', skin, offset + i * 48)
        if geoset == 10000:
            body.extend(lookup[start:start + size])
    p = positions[body]
    p = p[np.abs(p[:, 1]) < 0.3]
    def back(z):
        window = p[np.abs(p[:, 2] - z) < 0.12]
        return float(window[:, 0].min()) if len(window) else -0.1
    return back

def bone_weights(z):
    stops = [(1.50, {0: 255}), (1.38, {0: 128, 1: 127}), (1.30, {1: 128, 2: 127}),
             (1.20, {3: 255}), (-9, {4: 255})]
    for height, weights in stops:
        if z >= height:
            return weights
    return {4: 255}

def smooth_back(back):
    heights = np.linspace(BOTTOM, TOP, 40)
    envelope = [min(back(h) for h in np.linspace(max(z - 0.2, BOTTOM), min(z + 0.2, TOP), 9)) for z in heights]
    fit = np.polyfit(heights, envelope, 2)
    lowest = max(np.polyval(fit, z) - e for z, e in zip(heights, envelope))
    return lambda z: float(np.polyval(fit, z) - lowest)

def cape_mesh(back):
    back = smooth_back(back)
    vertices = []
    for row in range(ROWS):
        v = row / (ROWS - 1)
        z = TOP - v * (TOP - BOTTOM)
        half = 0.20 + 0.10 * v
        centre = back(z) - 0.035 - 0.10 * v ** 1.6
        for column in range(COLUMNS):
            u = column / (COLUMNS - 1)
            side = (u - 0.5) * 2
            y = -side * half
            x = centre + 0.05 * side * side
            vertices.append(((x, y, z), (u, v), bone_weights(z)))
    triangles = []
    for row in range(ROWS - 1):
        for column in range(COLUMNS - 1):
            a = row * COLUMNS + column
            b, c, d = a + 1, a + COLUMNS, a + COLUMNS + 1
            triangles += [a, c, b, b, c, d]
    return vertices, triangles

def normals(vertices, triangles):
    p = np.array([v[0] for v in vertices])
    n = np.zeros_like(p)
    t = np.array(triangles).reshape(-1, 3)
    face = np.cross(p[t[:, 1]] - p[t[:, 0]], p[t[:, 2]] - p[t[:, 0]])
    for k in range(3):
        np.add.at(n, t[:, k], face)
    n /= np.linalg.norm(n, axis=1, keepdims=True) + 1e-9
    if n[:, 0].mean() > 0:
        n = -n
    return n

class Blob:
    def __init__(self, data):
        self.data = bytearray(data)
    def append(self, payload, align=16):
        while len(self.data) % align:
            self.data.append(0)
        offset = len(self.data)
        self.data += payload
        return offset

def graft(m2_path, skin_path, out_m2, out_skin):
    m2 = open(m2_path, 'rb').read()
    skin = open(skin_path, 'rb').read()
    vertices, triangles = cape_mesh(back_profile(m2, skin))
    vertex_normals = normals(vertices, triangles)
    blob = Blob(m2)

    vertex_count, vertex_offset = arr(m2, 60)
    new_vertices = bytearray()
    for copy in CAPE_GEOSETS:
        for (position, uv, weights), normal in zip(vertices, vertex_normals):
            slots = sorted(weights.items())
            bones = [s[0] for s in slots] + [0] * (4 - len(slots))
            amounts = [s[1] for s in slots] + [0] * (4 - len(slots))
            bones = [CAPE_BONES[b] if a else 0 for b, a in zip(bones, amounts)]
            new_vertices += struct.pack('<3f4B4B3f2f2f', *position, *amounts, *bones, *normal, *uv, 0.0, 0.0)
    offset = blob.append(m2[vertex_offset:vertex_offset + vertex_count * 48] + new_vertices)
    struct.pack_into('<II', blob.data, 60, vertex_count + len(CAPE_GEOSETS) * len(vertices), offset)

    texture_count, texture_offset = arr(m2, 80)
    textures = m2[texture_offset:texture_offset + texture_count * 16]
    blank_name = textures[4:16]
    offset = blob.append(textures + struct.pack('<I', TEXTURE_CAPE) + blank_name)
    struct.pack_into('<II', blob.data, 80, texture_count + 1, offset)

    lookup_count, lookup_offset = arr(m2, 128)
    lookups = m2[lookup_offset:lookup_offset + lookup_count * 2]
    offset = blob.append(lookups + struct.pack('<h', texture_count), 4)
    struct.pack_into('<II', blob.data, 128, lookup_count + 1, offset)
    cape_texture_lookup = lookup_count

    bone_lookup_count, bone_lookup_offset = arr(m2, 120)
    bone_lookup = m2[bone_lookup_offset:bone_lookup_offset + bone_lookup_count * 2]
    offset = blob.append(bone_lookup + struct.pack('<%dH' % len(CAPE_BONES), *CAPE_BONES), 4)
    struct.pack_into('<II', blob.data, 120, bone_lookup_count + len(CAPE_BONES), offset)
    cape_bone_combo = bone_lookup_count

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
    template = submeshes[-48:]
    for copy, geoset in enumerate(CAPE_GEOSETS):
        first_vertex = vertex_count + copy * len(vertices)
        first_skin_vertex = len(skin_vertices) // 2
        first_triangle = len(skin_triangles) // 2
        for i, (_, _, weights) in enumerate(vertices):
            skin_vertices += struct.pack('<H', first_vertex + i)
            slots = sorted(weights)
            properties += bytes(slots + [0] * (4 - len(slots)))
        skin_triangles += struct.pack('<%dH' % len(triangles), *[first_skin_vertex + t for t in triangles])
        centre = np.mean([v[0] for v in vertices], axis=0)
        submesh = bytearray(template)
        struct.pack_into('<10H', submesh, 0, geoset, (first_triangle >> 16), first_skin_vertex, len(vertices),
                         first_triangle & 0xFFFF, len(triangles), len(CAPE_BONES), cape_bone_combo, 2, 0)
        struct.pack_into('<3f', submesh, 20, *centre)
        struct.pack_into('<3f', submesh, 32, *centre)
        struct.pack_into('<f', submesh, 44, 0.8)
        section = len(submeshes) // 48
        submeshes += submesh
        batches += struct.pack('<BbHHHhHHHHHHH', 16, 0, 0, section, section, -1, TWO_SIDED_MATERIAL, 0, 1,
                               cape_texture_lookup, 0, 0, 0)
    for header, payload, size in ((4, skin_vertices, 2), (12, skin_triangles, 2), (20, properties, 4),
                                  (28, submeshes, 48), (36, batches, 24)):
        offset = sk.append(bytes(payload), 4)
        struct.pack_into('<II', sk.data, header, len(payload) // size, offset)
    open(out_m2, 'wb').write(blob.data)
    open(out_skin, 'wb').write(sk.data)
    print('cape vertices', len(vertices), 'triangles', len(triangles) // 3, 'copies', len(CAPE_GEOSETS))

if __name__ == '__main__':
    graft(*sys.argv[1:5])
