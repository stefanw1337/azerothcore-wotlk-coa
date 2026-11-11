import struct as S
from pathlib import Path

from m2_writer import Buffer, require

FIELDS = (('translation', 16, '3f'), ('rotation', 36, '4h'), ('scale', 56, '3f'))


def channel_track(buffer, offset, channel, fmt, sequence_count):
    if channel is None:
        buffer.put(offset, 'HhIIII', 0, -1, 0, 0, 0, 0)
        return
    keys = channel['keys']
    if channel.get('global', -1) >= 0:
        require(len(keys) == 1, 'A global track has one timeline')
    else:
        require(len(keys) == sequence_count, 'Track sequence count mismatch')
    size = S.calcsize('<' + fmt)
    time_pairs, value_pairs = [], []
    for times, values in keys:
        require(len(times) == len(values), 'Track key count mismatch')
        time_pairs.append((len(times), buffer.add(S.pack(f'<{len(times)}I', *times)) if times else 0))
        payload = b''.join(S.pack('<' + fmt, *v) for v in values)
        value_pairs.append((len(values), buffer.add(payload) if values else 0))
        require(len(payload) == size * len(values), 'Track value size mismatch')
    t = buffer.add(b''.join(S.pack('<II', *p) for p in time_pairs))
    v = buffer.add(b''.join(S.pack('<II', *p) for p in value_pairs))
    buffer.put(offset, 'HhIIII', channel['interp'], channel.get('global', -1), len(time_pairs), t, len(value_pairs), v)


def constant_track(buffer, offset, fmt, value, sequence_count):
    keys = [([0], [value]) for _ in range(sequence_count)]
    channel_track(buffer, offset, dict(interp=0, keys=keys), fmt, sequence_count)


def lookup_table(pairs, length):
    table = [-1] * length
    for key, index in pairs:
        if 0 <= key < length and table[key] < 0:
            table[key] = index
    return table


def fblock(buffer, offset, keys, fmt):
    times = b''.join(S.pack('<H', int(round(max(0.0, min(1.0, t)) * 32767))) for t, _ in keys)
    values = b''.join(S.pack('<' + fmt, *v) for _, v in keys)
    buffer.put(offset, 'IIII', len(keys), buffer.add(times), len(keys), buffer.add(values))


def write_particle(b, o, e, n):
    tracks = e.get('tracks', {})
    b.put(o, 'iI3fHH', -1, e.get('flags', 0), *e.get('position', (0.0, 0.0, 0.0)), e['bone'], e['texture'])
    b.put(o + 24, 'IIII', 0, 0, 0, 0)
    b.put(o + 40, 'BBHBBHHH', e.get('blend', 4), e.get('emitter_type', 1), 0, e.get('particle_type', 0),
          e.get('head_or_tail', 0), 0, e.get('rows', 1), e.get('cols', 1))
    floats = (('emission_speed', 52), ('speed_variation', 72), ('vertical_range', 92), ('horizontal_range', 112),
              ('gravity', 132), ('lifespan', 152), ('emission_rate', 176), ('area_length', 200),
              ('area_width', 220), ('z_source', 240))
    for name, at in floats:
        if name in tracks:
            channel_track(b, o + at, tracks[name], 'f', n)
        else:
            constant_track(b, o + at, 'f', (float(e.get(name, 0.0)),), n)
    b.put(o + 172, 'f', e.get('lifespan_vary', 0.0))
    b.put(o + 196, 'f', e.get('emission_rate_vary', 0.0))
    fblock(b, o + 260, e['colors'], '3f')
    fblock(b, o + 276, e['alphas'], 'h')
    fblock(b, o + 292, e['scales'], '2f')
    b.put(o + 308, '2f', *e.get('scale_vary', (0.0, 0.0)))
    fblock(b, o + 316, e.get('head_cells', [(0.0, (0,))]), 'H')
    fblock(b, o + 332, e.get('tail_cells', [(0.0, (0,))]), 'H')
    b.put(o + 348, '3f2f2f', e.get('tail_length', 0.0), 0.0, 1.0, 1.0, 1.0, e.get('burst_multiplier', 1.0),
          e.get('drag', 0.0))
    b.put(o + 376, '4f', e.get('base_spin', 0.0), e.get('base_spin_vary', 0.0), e.get('spin', 0.0),
          e.get('spin_vary', 0.0))
    b.put(o + 392, '6f3f5f', *([0.0] * 6), 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0)
    b.put(o + 448, 'II', 0, 0)
    if 'enabled' in tracks:
        channel_track(b, o + 456, tracks['enabled'], 'B', n)
    else:
        constant_track(b, o + 456, 'B', (1,), n)


def write(scene, path):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    require(not path.exists(), 'Refusing to overwrite model')
    vertices, bones, seqs = scene['vertices'], scene['bones'], scene['sequences']
    textures, materials, groups = scene['textures'], scene['materials'], scene['groups']
    require(vertices and bones and seqs and textures and materials and groups, 'Incomplete scene')
    require(len(bones) <= 256 and len(vertices) <= 65535, 'Model limits exceeded')
    n = len(seqs)

    b = Buffer(304)
    b.put(0, '4sI', b'MD20', 264)
    name = scene.get('name', path.stem).encode() + b'\0'
    b.put(8, 'II', len(name), b.add(name))
    b.put(16, 'I', scene.get('flags', 0))
    b.array(20, 'I', [(d,) for d in scene.get('global_sequences', [])])

    rows = []
    for s in seqs:
        flags = (s.get('flags', 32) | 32) & ~64
        rows.append((s['id'], s.get('variation', 0), s['duration'], s['movespeed'], flags,
                     s.get('frequency', 32767), 0, s.get('replay_min', 0), s.get('replay_max', 0),
                     s.get('blend_time', 150), *s['minimum'], *s['maximum'], s['radius'],
                     s.get('variation_next', -1), 0))
    b.array(28, 'HHIfIhHIII7fhH', rows)
    firsts = [(s['id'], i) for i, s in enumerate(seqs) if s.get('variation', 0) == 0]
    b.array(36, 'h', [(x,) for x in lookup_table(firsts, max(s['id'] for s in seqs) + 1)])

    bone_offset = b.add(bytes(88 * len(bones)))
    b.put(44, 'II', len(bones), bone_offset)
    for i, bone in enumerate(bones):
        o = bone_offset + i * 88
        b.put(o, 'iIhHI', bone.get('key', -1), bone.get('flags', 0), bone['parent'], 0, 0)
        b.put(o + 76, '3f', *bone['pivot'])
        for field, delta, fmt in FIELDS:
            channel_track(b, o + delta, bone['channels'].get(field), fmt, n)
    keyed = [(bone['key'], i) for i, bone in enumerate(bones) if bone.get('key', -1) >= 0]
    b.array(52, 'h', [(x,) for x in lookup_table(keyed, max([k for k, _ in keyed], default=-1) + 1)])

    b.array(60, '3f4B4B3f4f', [(*v['position'], *v['weights'], *v['bones'], *v['normal'], *v['uv'], 0., 0.)
                                for v in vertices])
    b.put(68, 'I', 1)
    colors = scene.get('colors', [])
    color_offset = b.add(bytes(40 * len(colors))) if colors else 0
    b.put(72, 'II', len(colors), color_offset)
    for i, color in enumerate(colors):
        channel_track(b, color_offset + i * 40, color['color'], '3f', n)
        channel_track(b, color_offset + i * 40 + 20, color['alpha'], 'h', n)

    texture_rows = []
    for t in textures:
        raw = t.get('path', '').replace('/', '\\').encode()
        raw = raw + b'\0' if raw else b'\0'
        texture_rows.append((t.get('type', 0), t.get('flags', 0), len(raw), b.add(raw)))
    b.array(80, '4I', texture_rows)
    weight = b.add(bytes(20))
    b.put(88, 'II', 1, weight)
    constant_track(b, weight, 'h', (32767,), n)
    replaceable = [(t['type'], i) for i, t in enumerate(textures) if t.get('type', 0)]
    b.array(104, 'h', [(x,) for x in lookup_table(replaceable, max([k for k, _ in replaceable], default=-1) + 1)])
    blends = {'opaque': 0, 'cutout': 1, 'alpha': 2, 'additive': 3, 'add_alpha': 4, 'mod': 5, 'mod2x': 6}
    material_rows = []
    for m in materials:
        flags = (4 if m.get('two_sided') else 0) | (1 if m.get('unlit') else 0)
        flags |= (2 if m.get('unfogged') else 0) | (16 if m.get('no_depth_write') else 0)
        material_rows.append((flags, blends[m.get('blend', 'opaque')]))
    b.array(112, 'HH', material_rows)

    sections = []
    for gi, group in enumerate(groups):
        if group.get('share') is not None:
            continue
        for triangle in group['triangles']:
            needed = {bid for vi in triangle for bid, w in zip(vertices[vi]['bones'], vertices[vi]['weights']) if w}
            section = next((s for s in sections if s['group'] == gi and len(s['bones'] | needed) <= 64), None)
            if section is None:
                section = dict(bones=set(), triangles=[], group=gi)
                sections.append(section)
            section['bones'].update(needed)
            section['triangles'].append(triangle)
    palette, skin_indices, skin_faces, skin_properties, section_rows, batches = [], [], [], [], [], []
    for si, section in enumerate(sections):
        group = groups[section['group']]
        palette_start = len(palette)
        bone_list = sorted(section['bones'])
        palette.extend(bone_list)
        bone_map = {bid: k for k, bid in enumerate(bone_list)}
        vertex_start, face_start, vertex_map = len(skin_indices), len(skin_faces), {}
        for triangle in section['triangles']:
            for vi in triangle:
                if vi not in vertex_map:
                    vertex_map[vi] = len(skin_indices)
                    skin_indices.append(vi)
                    v = vertices[vi]
                    skin_properties.append(tuple(bone_map[bid] if w else 0 for bid, w in zip(v['bones'], v['weights'])))
                skin_faces.append(vertex_map[vi])
        used = [vertices[i]['position'] for i in vertex_map]
        center = [(min(p[k] for p in used) + max(p[k] for p in used)) / 2 for k in range(3)]
        radius = max(sum((p[k] - center[k]) ** 2 for k in range(3)) ** .5 for p in used)
        influences = max(sum(w > 0 for w in vertices[vi]['weights']) for vi in vertex_map)
        section_id = (group.get('geoset', 0) & 0xFFFF) | ((face_start >> 16) << 16)
        section_rows.append((section_id, vertex_start, len(vertex_map), face_start & 0xFFFF,
                             len(skin_faces) - face_start, len(bone_list), palette_start, influences, 0,
                             *center, *center, radius))
        layers = [(gi, g) for gi, g in enumerate(groups) if gi == section['group'] or g.get('share') == section['group']]
        for layer, (gi, g) in enumerate(layers):
            batches.append((g.get('batch_flags', 16), g.get('shader', 0), si, si, g.get('color', -1),
                            g['material'], layer, 1, g['texture'], 0, 0, 0))
    require(len(skin_indices) <= 65535 and max(r[4] for r in section_rows) <= 65535, 'SKIN index limit exceeded')
    b.array(120, 'H', [(i,) for i in palette])
    b.array(128, 'H', [(i,) for i in range(len(textures))])
    b.array(136, 'H', [(0,)])
    b.array(144, 'H', [(0,)])
    b.array(152, 'h', [(-1,)])

    bounds = scene['bounds']
    b.put(160, '7f', *bounds['minimum'], *bounds['maximum'], bounds['radius'])
    b.put(188, '7f', *bounds['minimum'], *bounds['maximum'], bounds['radius'])

    attachments = scene.get('attachments', [])
    attach_offset = b.add(bytes(40 * len(attachments))) if attachments else 0
    b.put(240, 'II', len(attachments), attach_offset)
    for i, a in enumerate(attachments):
        o = attach_offset + i * 40
        b.put(o, 'IHH3f', a['id'], a['bone'], 0, *a['position'])
        constant_track(b, o + 20, 'B', (1,), n)
    ids = [(a['id'], i) for i, a in enumerate(attachments)]
    b.array(248, 'h', [(x,) for x in lookup_table(ids, max([k for k, _ in ids], default=-1) + 1)])

    events = scene.get('events', [])
    event_offset = b.add(bytes(36 * len(events))) if events else 0
    b.put(256, 'II', len(events), event_offset)
    for i, event in enumerate(events):
        o = event_offset + i * 36
        b.put(o, '4sII3f', event['identifier'], event.get('data', 0), event['bone'], *event['position'])
        timelines = event['times']
        require(len(timelines) == n, 'Event sequence count mismatch')
        pairs = [(len(t), b.add(S.pack(f'<{len(t)}I', *t)) if t else 0) for t in timelines]
        b.put(o + 24, 'HhII', 0, -1, len(pairs), b.add(b''.join(S.pack('<II', *pair) for pair in pairs)))

    cameras = scene.get('cameras', [])
    camera_offset = b.add(bytes(100 * len(cameras))) if cameras else 0
    b.put(272, 'II', len(cameras), camera_offset)
    for i, camera in enumerate(cameras):
        o = camera_offset + i * 100
        b.put(o, 'I3f', camera['type'], camera['fov'], camera['far'], camera['near'])
        for at, floats in ((16, 9), (48, 9), (80, 3)):
            times = b.add(S.pack('<II', 1, b.add(S.pack('<I', 0))))
            values = b.add(S.pack('<II', 1, b.add(bytes(4 * floats))))
            b.put(o + at, 'HhIIII', 0, -1, 1, times, 1, values)
        b.put(o + 36, '3f', *camera['position'])
        b.put(o + 68, '3f', *camera['target'])
    camera_ids = [(camera['type'], i) for i, camera in enumerate(cameras)]
    if camera_ids:
        b.array(280, 'h', [(x,) for x in lookup_table(camera_ids, max(k for k, _ in camera_ids) + 1)])

    particles = scene.get('particles', [])
    particle_offset = b.add(bytes(476 * len(particles))) if particles else 0
    b.put(296, 'II', len(particles), particle_offset)
    for i, e in enumerate(particles):
        write_particle(b, particle_offset + i * 476, e, n)

    skin = Buffer(48)
    skin.put(0, '4s', b'SKIN')
    skin.put(44, 'I', 64)
    skin.array(4, 'H', [(i,) for i in skin_indices])
    skin.array(12, 'H', [(v,) for v in skin_faces])
    skin.array(20, '4B', skin_properties)
    skin.array(28, 'I8H7f', section_rows)
    skin.array(36, 'HHHHh7H', batches)
    path.write_bytes(b.data)
    path.with_name(path.stem + '00.skin').write_bytes(skin.data)
    return dict(vertices=len(vertices), sections=len(sections), bones=len(bones), sequences=n,
                attachments=len(attachments), textures=len(textures))
