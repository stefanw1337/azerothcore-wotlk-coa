import json, math, struct, sys
import numpy as np

CAPE_GEOSETS = range(1502, 1511)
CHEST_BONE, MIDDLE_SPINE_BONE, LOW_SPINE_BONE, PELVIS_BONE = 16, 9, 3, 2
LEFT_CLAVICLE_BONE, RIGHT_CLAVICLE_BONE = 21, 22
SHOULDER_GIRDLE = {CHEST_BONE, MIDDLE_SPINE_BONE, LOW_SPINE_BONE, LEFT_CLAVICLE_BONE, RIGHT_CLAVICLE_BONE}
UPPER_BACK, MIDDLE_BACK, WAIST = 1.40, 1.30, 1.20
CHEST, MIDDLE_SPINE, LOW_SPINE, CAPE_TOP, CAPE_LOW = CHEST_BONE, MIDDLE_SPINE_BONE, LOW_SPINE_BONE, 'top', 'low'
MIDDLE_BACK_SHARES = {CHEST: 42, MIDDLE_SPINE: 85, LOW_SPINE: 128}
BODY_GEOSET = 10000
BACK_SKIN_NEIGHBOURS = 8
SWING = {
    'Walk': (6, 4, 2), 'Run': (18, 14, 4), 'RunLeft': (15, 12, 4), 'RunRight': (15, 12, 4),
    'Sprint': (26, 20, 5), 'JumpStart': (20, 16, 3), 'Jump': (38, 28, 4), 'Fall': (58, 40, 6),
    'JumpEnd': (0, -2, 1), 'JumpLandRun': (18, 14, 4), 'FlyRun': (28, 22, 5), 'SwimRun': (14, 10, 3),
    'ReadyUnarmed': (-2, -1, 1), 'Stand': (-3, -2, 1)}
COLUMNS, ROWS = 9, 14
TOP, BOTTOM = 1.62, 0.42
HUG_GAP, CLEARANCE = 0.025, 0.012
SURFACE_WINDOW_Y, SURFACE_WINDOW_Z = 0.06, 0.05
COLLAR_V = 0.08
STRAP_FROM = 0.12
SHOULDER_GAP, CHEST_GAP = 0.02, 0.02
CLASP_HEIGHT = 1.55
NECK_TOP = 1.70
BEHIND_NECK = (-0.10, 1.66)
TEXTURE_CAPE = 2
TEXTURE_LEFT, TEXTURE_RIGHT = 0.30, 0.99
TWO_SIDED_MATERIAL = 0
HELMET_ATTACHMENT = 11
HELMET_SHIFT = (0.07, 0.0, 0.18)

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

def back_surface(m2, skin):
    vertex_count, vertex_offset = arr(m2, 60)
    raw = np.frombuffer(m2, np.uint8, vertex_count * 48, vertex_offset).reshape(-1, 48)
    positions = raw[:, :12].copy().view(np.float32).reshape(-1, 3)
    index_count, index_offset = arr(skin, 4)
    lookup = np.frombuffer(skin, np.uint16, index_count, index_offset)
    count, offset = arr(skin, 28)
    body = set()
    for i in range(count):
        geoset, level, start, size = struct.unpack_from('<4H', skin, offset + i * 48)
        if geoset == BODY_GEOSET:
            body.update(lookup[start:start + size].tolist())
    p = positions[sorted(body)]
    p = p[p[:, 0] < 0.05]
    def surface(y, z):
        window = p[(np.abs(p[:, 1] - y) < SURFACE_WINDOW_Y) & (np.abs(p[:, 2] - z) < SURFACE_WINDOW_Z)]
        return float(window[:, 0].min()) if len(window) else None
    return surface

def body_points(m2, skin):
    vertex_count, vertex_offset = arr(m2, 60)
    raw = np.frombuffer(m2, np.uint8, vertex_count * 48, vertex_offset).reshape(-1, 48)
    positions = raw[:, :12].copy().view(np.float32).reshape(-1, 3)
    index_count, index_offset = arr(skin, 4)
    lookup = np.frombuffer(skin, np.uint16, index_count, index_offset)
    count, offset = arr(skin, 28)
    body = set()
    for i in range(count):
        geoset, level, start, size = struct.unpack_from('<4H', skin, offset + i * 48)
        if geoset == BODY_GEOSET:
            body.update(lookup[start:start + size].tolist())
    return positions[sorted(body)]

def collar(points):
    shoulders = points[(points[:, 2] < NECK_TOP) & (points[:, 2] > UPPER_BACK)]
    def over_shoulder(y):
        near = shoulders[np.abs(shoulders[:, 1] - y) < 0.03]
        top = near[np.argmax(near[:, 2])]
        return (float(top[0]), y, float(top[2]) + SHOULDER_GAP)
    def on_chest(y):
        near = points[(np.abs(points[:, 1] - y) < 0.04) & (np.abs(points[:, 2] - CLASP_HEIGHT) < 0.04)]
        return (float(near[:, 0].max()) + CHEST_GAP, y, CLASP_HEIGHT)
    def strap(y):
        if abs(y) < STRAP_FROM:
            behind = (BEHIND_NECK[0], y, BEHIND_NECK[1])
            return behind, behind
        return on_chest(y), over_shoulder(y)
    return strap

def hug_share(z):
    return min(1.0, max(0.0, (z - WAIST) / (UPPER_BACK - WAIST)))

def mixed(first, second, share):
    blend = {}
    for slot in set(first) | set(second):
        blend[slot] = first.get(slot, 0) * (1.0 - share) + second.get(slot, 0) * share
    kept = sorted(((amount, slot) for slot, amount in blend.items() if amount > 0.5), key=lambda kept: kept[0],
                  reverse=True)[:4]
    total = sum(amount for amount, _ in kept)
    weights = {slot: int(round(255 * amount / total)) for amount, slot in kept}
    largest = max(weights, key=weights.get)
    weights[largest] += 255 - sum(weights.values())
    return {slot: amount for slot, amount in weights.items() if amount}

def hanging_weights(z):
    v = (WAIST - z) / (WAIST - BOTTOM)
    return mixed({CAPE_TOP: 255}, {CAPE_LOW: 255}, min(1.0, max(0.0, (v - 0.3) / 0.5)))

def girdle_bone(bone, parents):
    while bone >= 0 and bone not in SHOULDER_GIRDLE:
        bone = parents[bone]
    return bone if bone >= 0 else CHEST_BONE

def back_skin(m2, skin):
    vertex_count, vertex_offset = arr(m2, 60)
    raw = np.frombuffer(m2, np.uint8, vertex_count * 48, vertex_offset).reshape(-1, 48)
    positions = raw[:, :12].copy().view(np.float32).reshape(-1, 3)
    index_count, index_offset = arr(skin, 4)
    lookup = np.frombuffer(skin, np.uint16, index_count, index_offset)
    count, offset = arr(skin, 28)
    body = set()
    for i in range(count):
        geoset, level, start, size = struct.unpack_from('<4H', skin, offset + i * 48)
        if geoset == BODY_GEOSET:
            body.update(lookup[start:start + size].tolist())
    body = np.array(sorted(body))
    back = body[positions[body, 2] > WAIST]
    bone_count, bone_offset = arr(m2, 44)
    parents = [struct.unpack_from('<h', m2, bone_offset + i * 88 + 8)[0] for i in range(bone_count)]
    return positions[back], raw[back, 16:20], raw[back, 12:16], parents

def skin_weights(point, skin):
    positions, bones, amounts, parents = skin
    distances = np.linalg.norm(positions - np.array(point), axis=1)
    blend = {}
    for vertex in np.argsort(distances)[:BACK_SKIN_NEIGHBOURS]:
        share = 1.0 / (distances[vertex] + 0.02)
        for bone, amount in zip(bones[vertex], amounts[vertex]):
            if amount:
                girdle = girdle_bone(int(bone), parents)
                blend[girdle] = blend.get(girdle, 0.0) + share * amount
    return mixed(blend, blend, 0.0)

def bone_weights(z, point=None, skin=None):
    if z >= UPPER_BACK:
        return skin_weights(point, skin) if skin else {CHEST: 255}
    if z >= MIDDLE_BACK:
        upper = skin_weights(point, skin) if skin else {CHEST: 255}
        return mixed(upper, MIDDLE_BACK_SHARES, (UPPER_BACK - z) / (UPPER_BACK - MIDDLE_BACK))
    if z >= WAIST:
        return mixed(MIDDLE_BACK_SHARES, hanging_weights(WAIST), (MIDDLE_BACK - z) / (MIDDLE_BACK - WAIST))
    return hanging_weights(z)

def smooth_back(back):
    heights = np.linspace(BOTTOM, TOP, 40)
    envelope = [min(back(h) for h in np.linspace(max(z - 0.2, BOTTOM), min(z + 0.2, TOP), 9)) for z in heights]
    fit = np.polyfit(heights, envelope, 2)
    lowest = max(np.polyval(fit, z) - e for z, e in zip(heights, envelope))
    return lambda z: float(np.polyval(fit, z) - lowest)

def cape_mesh(back, skin=None, surface=None, strap=None):
    back = smooth_back(back)
    vertices = []
    if strap:
        for row in range(2):
            for column in range(COLUMNS):
                u = column / (COLUMNS - 1)
                y = -(u - 0.5) * 2 * 0.20
                point = strap(y)[row]
                vertices.append((point, (TEXTURE_LEFT + u * (TEXTURE_RIGHT - TEXTURE_LEFT), COLLAR_V * row / 2),
                                 bone_weights(UPPER_BACK, point, skin) if skin else {CHEST: 255}))
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
            skin_x = surface(y, z) if surface else None
            if skin_x is not None:
                share = hug_share(z)
                x = min(share * (skin_x - HUG_GAP) + (1.0 - share) * x, skin_x - CLEARANCE)
            texture_v = COLLAR_V + v * (1.0 - COLLAR_V) if strap else v
            vertices.append(((x, y, z), (TEXTURE_LEFT + u * (TEXTURE_RIGHT - TEXTURE_LEFT), texture_v),
                             bone_weights(z, (x, y, z), skin)))
    triangles = []
    for row in range(len(vertices) // COLUMNS - 1):
        for column in range(COLUMNS - 1):
            a = row * COLUMNS + column
            b, c, d = a + 1, a + COLUMNS, a + COLUMNS + 1
            triangles += [a, c, b, b, c, d]
    def surface(z):
        v = (TOP - z) / (TOP - BOTTOM)
        return (back(z) - 0.035 - 0.10 * v ** 1.6, 0.0, z)
    return vertices, triangles, surface(WAIST), surface(WAIST - 0.45 * (WAIST - BOTTOM))

def compress(f):
    v = int(round(f * 32767))
    return v - 32768 if f >= 0 else v + 32767

def swing_key(degrees):
    half = math.radians(degrees) / 2
    return struct.pack('<4h', compress(0.0), compress(math.sin(half)), compress(0.0), compress(math.cos(half)))

def cape_bone(blob, m2, parent, pivot, part, names):
    sequence_count, sequence_offset = arr(m2, 28)
    times, values = bytearray(), bytearray()
    for i in range(sequence_count):
        animation, sub, length = struct.unpack_from('<HHI', m2, sequence_offset + i * 64)
        lifted = SWING.get(names.get(animation, ''), (0, 0, 0))
        angle, flutter = lifted[part], lifted[2] * (1 if part == 0 else 1.6)
        if flutter and length > 1:
            keys = [(0, angle), (length // 2, angle + flutter), (length, angle)]
        else:
            keys = [(0, angle)]
        time_offset = blob.append(struct.pack('<%dI' % len(keys), *[k[0] for k in keys]), 4)
        value_offset = blob.append(b''.join(swing_key(k[1]) for k in keys), 4)
        times += struct.pack('<II', len(keys), time_offset)
        values += struct.pack('<II', len(keys), value_offset)
    empty = blob.append(struct.pack('<II', 0, 0) * sequence_count, 4)
    time_table = blob.append(bytes(times), 4)
    value_table = blob.append(bytes(values), 4)
    still = struct.pack('<HhIIII', 0, -1, sequence_count, empty, sequence_count, empty)
    turning = struct.pack('<HhIIII', 1, -1, sequence_count, time_table, sequence_count, value_table)
    return (struct.pack('<iIhHI', -1, 0x200, parent, 0, 0) + still + turning + still +
            struct.pack('<3f', *pivot))

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

def seat_helmet(data):
    attachment_count, attachment_offset = arr(data, 240)
    for index in range(attachment_count):
        record = attachment_offset + index * 40
        if struct.unpack_from('<I', data, record)[0] == HELMET_ATTACHMENT:
            position = struct.unpack_from('<3f', data, record + 8)
            struct.pack_into('<3f', data, record + 8, *(p + d for p, d in zip(position, HELMET_SHIFT)))

def graft(m2_path, skin_path, out_m2, out_skin, names_path):
    m2 = open(m2_path, 'rb').read()
    skin = open(skin_path, 'rb').read()
    vertices, triangles, top_pivot, low_pivot = cape_mesh(back_profile(m2, skin), back_skin(m2, skin),
                                                          back_surface(m2, skin), collar(body_points(m2, skin)))
    vertex_normals = normals(vertices, triangles)
    blob = Blob(m2)
    names = {int(k): v for k, v in json.load(open(names_path)).items()}
    bone_count, bone_offset = arr(m2, 44)
    cape_top, cape_low = bone_count, bone_count + 1
    body_bones = sorted({bone for _, _, weights in vertices for bone in weights if isinstance(bone, int)})
    CAPE_BONES = body_bones + [cape_top, cape_low]
    slot_of = {bone: slot for slot, bone in enumerate(body_bones)}
    slot_of.update({CAPE_TOP: len(body_bones), CAPE_LOW: len(body_bones) + 1})
    vertices = [(position, uv, {slot_of[bone]: amount for bone, amount in weights.items()})
                for position, uv, weights in vertices]
    new_bones = (cape_bone(blob, m2, PELVIS_BONE, top_pivot, 0, names) +
                 cape_bone(blob, m2, cape_top, low_pivot, 1, names))
    offset = blob.append(m2[bone_offset:bone_offset + bone_count * 88] + new_bones)
    struct.pack_into('<II', blob.data, 44, bone_count + 2, offset)

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
    influences = max(len(weights) for _, _, weights in vertices)
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
                         first_triangle & 0xFFFF, len(triangles), len(CAPE_BONES), cape_bone_combo, influences, 0)
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
    seat_helmet(blob.data)
    open(out_m2, 'wb').write(blob.data)
    open(out_skin, 'wb').write(sk.data)
    print('cape vertices', len(vertices), 'triangles', len(triangles) // 3, 'copies', len(CAPE_GEOSETS))

if __name__ == '__main__':
    graft(*sys.argv[1:6])
