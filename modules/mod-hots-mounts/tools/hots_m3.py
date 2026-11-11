import math
import struct
import sys
from pathlib import Path

import numpy as np

M3STUDIO = Path(__file__).resolve().parent / 'm3studio'


def require(ok, message):
    if not ok:
        raise ValueError(message)


def _io_m3():
    require((M3STUDIO / 'io_m3.py').exists(),
            f'm3studio not found at {M3STUDIO}; run: python setup_m3studio.py')
    if str(M3STUDIO) not in sys.path:
        sys.path.insert(0, str(M3STUDIO))
    import io_m3
    return io_m3


def quat_to_mat(q):
    x, y, z, w = q
    n = math.sqrt(x * x + y * y + z * z + w * w) or 1.0
    x, y, z, w = x / n, y / n, z / n, w / n
    return np.array([
        [1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
        [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
        [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]])


def mat_to_quat(m):
    t = m[0, 0] + m[1, 1] + m[2, 2]
    if t > 0:
        s = math.sqrt(t + 1.0) * 2
        q = ((m[2, 1] - m[1, 2]) / s, (m[0, 2] - m[2, 0]) / s, (m[1, 0] - m[0, 1]) / s, 0.25 * s)
    elif m[0, 0] > m[1, 1] and m[0, 0] > m[2, 2]:
        s = math.sqrt(1.0 + m[0, 0] - m[1, 1] - m[2, 2]) * 2
        q = (0.25 * s, (m[0, 1] + m[1, 0]) / s, (m[0, 2] + m[2, 0]) / s, (m[2, 1] - m[1, 2]) / s)
    elif m[1, 1] > m[2, 2]:
        s = math.sqrt(1.0 + m[1, 1] - m[0, 0] - m[2, 2]) * 2
        q = ((m[0, 1] + m[1, 0]) / s, 0.25 * s, (m[1, 2] + m[2, 1]) / s, (m[0, 2] - m[2, 0]) / s)
    else:
        s = math.sqrt(1.0 + m[2, 2] - m[0, 0] - m[1, 1]) * 2
        q = ((m[0, 2] + m[2, 0]) / s, (m[1, 2] + m[2, 1]) / s, 0.25 * s, (m[1, 0] - m[0, 1]) / s)
    n = math.sqrt(sum(c * c for c in q))
    return tuple(c / n for c in q)


def trs(loc, quat, scale):
    m = np.eye(4)
    m[:3, :3] = quat_to_mat(quat) * np.asarray(scale, dtype=float)
    m[:3, 3] = loc
    return m


def slerp(a, b, f):
    a = np.asarray(a, float)
    b = np.asarray(b, float)
    d = float(np.dot(a, b))
    if d < 0:
        b, d = -b, -d
    if d > 0.9995:
        r = a + (b - a) * f
    else:
        th = math.acos(d)
        r = (math.sin((1 - f) * th) * a + math.sin(f * th) * b) / math.sin(th)
    return r / np.linalg.norm(r)


def sample_keys(times, values, t, quaternion=False):
    if len(times) == 1 or t <= times[0]:
        return values[0]
    if t >= times[-1]:
        return values[-1]
    i = int(np.searchsorted(times, t, side='right')) - 1
    t0, t1 = times[i], times[i + 1]
    f = 0.0 if t1 == t0 else (t - t0) / (t1 - t0)
    if quaternion:
        return slerp(values[i], values[i + 1], f)
    return np.asarray(values[i], float) * (1 - f) + np.asarray(values[i + 1], float) * f


def m3_matrix(m):
    out = np.zeros((4, 4))
    for c, col in enumerate((m.x, m.y, m.z, m.w)):
        out[:, c] = (col.x, col.y, col.z, col.w)
    return out


def _string(sl, ref):
    try:
        return sl[ref].content_to_string()
    except Exception:
        return ''


def _vec(v, n=3):
    return tuple(getattr(v, k) for k in 'xyzw'[:n])


def _anim_tables(sl, model):
    out = {}
    stcs = sl[model.sequence_transformation_collections]
    for seq, stg in zip(sl[model.sequences], sl[model.sequence_transformation_groups]):
        name = _string(sl, seq.name)
        data = {}
        stc_list = [stcs[i] for i in sl[stg.stc_indices]]
        stc_list.sort(key=lambda s: (not _string(sl, s.name).endswith('_full'), -s.priority))
        for stc in stc_list:
            kinds = [stc.sdev, stc.sd2v, stc.sd3v, stc.sd4q, stc.sdcc, stc.sdr3, stc.sdu8,
                     stc.sds6, stc.sdu6, stc.sds3, stc.sdu3, stc.sdfg, stc.sdmb]
            for aid, ref in zip(sl[stc.anim_ids], sl[stc.anim_refs]):
                if aid in data:
                    continue
                kind, index = ref >> 16, ref & 0xffff
                if kind not in (2, 3, 5):
                    continue
                block = sl[kinds[kind]][index]
                frames = list(sl[block.frames])
                keys = [float(k) if kind == 5 else _vec(k, 4 if kind == 3 else 3) for k in sl[block.keys]]
                if frames and keys:
                    order = np.argsort(frames, kind='stable')
                    data[aid] = (np.asarray(frames, float)[order] - seq.anim_ms_start,
                                 [keys[i] for i in order], kind)
        prev = out.get(name)
        if prev is None or len(data) > len(prev['data']):
            out[name] = dict(duration=max(1, seq.anim_ms_end - seq.anim_ms_start),
                             speed=float(seq.movement_speed), flags=int(seq.flags), data=data)
    return out


def _material_bitmaps(sl, model):
    result = []
    for ref in sl[model.material_references]:
        entry = dict(type=int(ref.type), name='', diffuse='', emissive='', alpha='', blend=0, flags=0,
                     alpha_test=0, transparent=False)
        if ref.type == 1:
            mat = sl[model.materials_standard][ref.material_index]
            entry['name'] = _string(sl, mat.name)
            entry['blend'] = int(mat.blend_mode)
            entry['flags'] = int(mat.flags)
            entry['alpha_test'] = int(getattr(mat, 'alpha_test_threshold', 0))
            for key, field in (('diffuse', 'layer_diff'), ('emissive', 'layer_emis1'), ('alpha', 'layer_alpha1')):
                layers = sl[getattr(mat, field)]
                if len(layers):
                    entry[key] = _string(sl, layers[0].color_bitmap)
                    rows, cols = int(getattr(layers[0], 'uv_flipbook_rows', 0)), int(getattr(layers[0], 'uv_flipbook_cols', 0))
                    if rows > 0 and cols > 0 and 'flipbook' not in entry:
                        entry['flipbook'] = (rows, cols, int(layers[0].uv_flipbook_frame.default))
                    try:
                        c = layers[0].color_value.default
                        entry[key + '_color'] = (int(c.r), int(c.g), int(c.b), int(c.a))
                    except AttributeError:
                        pass
        elif ref.type == 12:
            mat = sl[model.materials_buffer][ref.material_index]
            entry['name'] = _string(sl, mat.name)
            entry['flags'] = 0x80000000
            entry['transparent'] = bool(getattr(mat, 'unknown_16', 0))
            paths = [_string(sl, e.path) for e in sl[mat.texture_paths]]
            entry['paths'] = paths
            for key, suffix in (('diffuse', '_diff.dds'), ('emissive', '_emis.dds')):
                entry[key] = next((p for p in paths if p.lower().endswith(suffix)), '')
        result.append(entry)
    return result


def _value(ref):
    v = getattr(ref, 'default', ref)
    for keys in ('rgba', 'xyzw', 'xyz', 'xy'):
        if all(hasattr(v, k) for k in keys):
            return tuple(float(getattr(v, k)) for k in keys)
    return float(v)


def _anim_id(ref):
    header = getattr(ref, 'header', None)
    return int(getattr(header, 'id', 0) or 0)


def _effects(sl, model):
    particles = []
    for r in sl[model.particle_systems]:
        particles.append(dict(
            bone=int(r.bone), material=int(r.material_reference_index), type=int(r.particle_type),
            shape=int(r.emit_shape), emit_type=int(r.emit_type), rate=_value(r.emit_rate),
            rate_id=_anim_id(r.emit_rate), count=_value(r.emit_count), max=int(r.emit_max),
            life=_value(r.lifespan), life2=_value(r.lifespan_random), speed=_value(r.emit_speed),
            speed2=_value(r.emit_speed_random), angle=(_value(r.emit_angle_x), _value(r.emit_angle_y)),
            spread=(_value(r.emit_spread_x), _value(r.emit_spread_y)), radius=_value(r.emit_shape_radius),
            box=_value(r.emit_shape_size), gravity=float(r.gravity), drag=float(r.drag),
            size=_value(r.size), size2=_value(r.size2), size_mid=float(r.size_anim_mid),
            colors=(_value(r.color_init), _value(r.color_mid), _value(r.color_end)),
            color_mid=float(r.color_anim_mid), alpha_mid=float(r.alpha_anim_mid),
            rotation=_value(r.rotation), rotation2=_value(r.rotation2),
            rows=int(r.uv_flipbook_rows), cols=int(r.uv_flipbook_cols), flags=int(r.flags),
            extra_flags=int(r.additional_flags)))
    ribbons = []
    for r in sl[model.ribbons]:
        ribbons.append(dict(
            bone=int(r.bone), material=int(r.material_reference_index), type=int(r.ribbon_type),
            length=_value(r.length), speed=_value(r.speed), life=_value(r.lifespan), gravity=float(r.gravity),
            drag=float(r.drag), scale=_value(r.scale), scale_mid=float(r.scale_anim_mid),
            colors=(_value(r.color_base), _value(r.color_mid), _value(r.color_tip)),
            color_mid=float(r.color_anim_mid), alpha_mid=float(r.alpha_anim_mid),
            yaw=_value(r.yaw), pitch=_value(r.pitch), divisions=float(r.divisions), sides=int(r.sides),
            flags=int(r.flags), active_id=_anim_id(r.active), active=_value(r.active)))
    return particles, ribbons


def read(m3_path, m3a_paths=()):
    io_m3 = _io_m3()
    sl = io_m3.M3SectionList.load(str(m3_path))
    model = sl[sl[0][0].model][0]
    bones = []
    for b in sl[model.bones]:
        bones.append(dict(
            name=_string(sl, b.name), parent=int(b.parent),
            loc=_vec(b.location.default), rot=_vec(b.rotation.default, 4), scale=_vec(b.scale.default),
            ids=(b.location.header.id, b.rotation.header.id, b.scale.header.id)))
    inverse_bind = [m3_matrix(r.matrix) for r in sl[model.bone_rests]]
    require(len(inverse_bind) == len(bones), 'Bone rest count mismatch')

    flags = model.vertex_flags
    vdesc = io_m3.M3StructureDescription.get_vertex_description(flags)
    raw = sl[model.vertices]
    count = len(raw) // vdesc.size
    m3v = vdesc.instances(buffer=raw.raw_bytes, count=count)
    uv_name = 'uv0' if vdesc.fields.get('uv0') else None
    lookups = sum(2 for bit in (0x20, 0x40) if flags & bit)
    lookup_table = list(sl[model.bone_lookup])

    division = sl[model.divisions][0]
    faces = list(sl[division.faces])
    batches = list(sl[division.batches])
    regions = []
    for ri, reg in enumerate(sl[division.regions]):
        used = [bt for bt in batches if bt.region_index == ri]
        if not used:
            continue
        mult = getattr(reg, 'uv_multiply', 16)
        offs = getattr(reg, 'uv_offset', 0)
        verts = []
        for v in m3v[reg.first_vertex_index:reg.first_vertex_index + reg.vertex_count]:
            infl = []
            for k in range(min(lookups, reg.vertex_lookups_used) or 1):
                w = getattr(v, f'weight{k}', 255)
                if w:
                    li = getattr(v, f'lookup{k}', 0)
                    infl.append((lookup_table[reg.first_bone_lookup_index + li], w / 255.0))
            n = getattr(v, 'normal', None)
            normal = (n.x / 255 * 2 - 1, n.y / 255 * 2 - 1, n.z / 255 * 2 - 1) if n is not None else (0, 0, 1)
            uv = getattr(v, uv_name) if uv_name else None
            tex = (uv.x * mult / 32768 + offs, uv.y * mult / 32768 + offs) if uv is not None else (0, 0)
            verts.append(dict(pos=_vec(v.pos), normal=normal, uv=tex, infl=infl))
        tri = faces[reg.first_face_index:reg.first_face_index + reg.face_count]
        if reg.desc.version <= 2:
            tri = [i - reg.first_vertex_index for i in tri]
        regions.append(dict(index=ri, vertices=verts, triangles=[tri[i:i + 3] for i in range(0, len(tri) - 2, 3)],
                            materials=[int(bt.material_reference_index) for bt in used]))

    attachments = [dict(name=_string(sl, a.name), bone=int(a.bone)) for a in sl[model.attachment_points]]
    sequences = _anim_tables(sl, model)
    for path in m3a_paths:
        asl = io_m3.M3SectionList.load(str(path))
        amodel = asl[asl[0][0].model][0]
        for name, seq in _anim_tables(asl, amodel).items():
            sequences.setdefault(name, seq)
    effects = _effects(sl, model)
    billboards = [dict(bone=int(b.bone), type=int(b.billboard_type)) for b in sl[model.billboards]] \
        if hasattr(model, 'billboards') else []
    return dict(name=_string(sl, model.model_name), bones=bones, inverse_bind=inverse_bind, regions=regions,
                materials=_material_bitmaps(sl, model), attachments=attachments, sequences=sequences,
                billboards=billboards, particles=effects[0], ribbons=effects[1])


def pose(m3, seq, t):
    world = []
    data = seq['data'] if seq else {}
    for b in m3['bones']:
        vals = []
        for slot, (aid, default) in enumerate(zip(b['ids'], (b['loc'], b['rot'], b['scale']))):
            entry = data.get(aid)
            vals.append(sample_keys(entry[0], entry[1], t, slot == 1) if entry else default)
        local = trs(*vals)
        world.append(world[b['parent']] @ local if b['parent'] >= 0 else local)
    return world


def skin_matrices(m3, seq, t):
    return [w @ ib for w, ib in zip(pose(m3, seq, t), m3['inverse_bind'])]


def conversion_matrix(rotation_z_degrees=90.0, scale=1.0, offset=(0.0, 0.0, 0.0), pitch_degrees=0.0):
    a = math.radians(rotation_z_degrees)
    c = np.eye(4)
    c[:3, :3] = np.array([[math.cos(a), -math.sin(a), 0], [math.sin(a), math.cos(a), 0], [0, 0, 1]]) * scale
    b = math.radians(pitch_degrees)
    c[:3, :3] = np.array([[math.cos(b), 0, math.sin(b)], [0, 1, 0], [-math.sin(b), 0, math.cos(b)]]) @ c[:3, :3]
    c[:3, 3] = offset
    return c


def flat_mesh(m3, keep_region=None):
    vertices, groups = [], []
    for reg in m3['regions']:
        if keep_region and not keep_region(reg):
            continue
        base = len(vertices)
        cell = None
        for mi in reg['materials']:
            if mi < len(m3['materials']) and m3['materials'][mi].get('flipbook'):
                cell = m3['materials'][mi]['flipbook']
                break
        for v in reg['vertices']:
            infl = sorted(v['infl'], key=lambda x: -x[1])[:4] or [(0, 1.0)]
            total = sum(w for _, w in infl)
            weights = [int(round(w / total * 255)) for _, w in infl]
            weights[0] += 255 - sum(weights)
            uv = v['uv']
            if cell:
                rows, cols, frame = cell
                frame %= rows * cols
                uv = ((min(max(uv[0], 0.0), 1.0) + frame % cols) / cols, (min(max(uv[1], 0.0), 1.0) + frame // cols) / rows)
            vertices.append(dict(pos=v['pos'], normal=v['normal'], uv=uv,
                                 bones=[b for b, _ in infl] + [0] * (4 - len(infl)),
                                 weights=weights + [0] * (4 - len(infl))))
        groups.append(dict(region=reg['index'], material=reg['materials'][0],
                           triangles=[[base + i for i in t] for t in reg['triangles']]))
    return vertices, groups


def skin_positions(vertices, skins):
    pos = np.array([v['pos'] + (1.0,) for v in vertices])
    out = np.zeros((len(vertices), 3))
    stack = np.stack(skins)
    for k in range(4):
        ids = np.array([v['bones'][k] for v in vertices])
        w = np.array([v['weights'][k] for v in vertices]) / 255.0
        moved = np.einsum('nij,nj->ni', stack[ids], pos)[:, :3]
        out += moved * w[:, None]
    return out


def _track(local, p):
    a = local[:3, :3]
    scale = np.linalg.norm(a, axis=0)
    rot = a / np.where(scale == 0, 1, scale)
    if np.linalg.det(rot) < 0:
        scale[0] = -scale[0]
        rot[:, 0] = -rot[:, 0]
    err = float(np.abs(rot @ rot.T - np.eye(3)).max())
    translation = local[:3, 3] - p + a @ p
    return dict(translation=tuple(translation), rotation=mat_to_quat(rot), scale=tuple(scale)), err


def bake_sequence(m3, seq, conv, pivots, fps=30, extra=()):
    duration = seq['duration'] if seq else 1000
    step = 1000.0 / fps
    times = [int(round(i * step)) for i in range(int(duration // step) + 1)]
    if times[-1] != duration:
        times.append(duration)
    inv_conv = np.linalg.inv(conv)
    frames, worst = [], 0.0
    all_skins = [[conv @ s @ inv_conv for s in skin_matrices(m3, seq, t)] for t in times]
    corrections = []
    for source, p, turn in extra:
        if turn is not None:
            correction = np.eye(4)
            correction[:3, :3] = turn
            correction[:3, 3] = p - turn @ p
            corrections.append(correction)
            continue
        total = np.zeros((3, 3))
        for skins in all_skins:
            a = skins[source][:3, :3]
            total += a / np.where(np.linalg.norm(a, axis=0) == 0, 1, np.linalg.norm(a, axis=0))
        u, _, vt = np.linalg.svd(total)
        mean = u @ vt
        if np.linalg.det(mean) < 0:
            mean = u @ np.diag([1, 1, -1]) @ vt
        correction = np.eye(4)
        correction[:3, :3] = mean.T
        correction[:3, 3] = p - mean.T @ p
        corrections.append(correction)
    for t, skins in zip(times, all_skins):
        tracks = []
        for i, b in enumerate(m3['bones']):
            local = np.linalg.inv(skins[b['parent']]) @ skins[i] if b['parent'] >= 0 else skins[i]
            track, err = _track(local, pivots[i])
            worst = max(worst, err)
            tracks.append(track)
        for (source, p, _), correction in zip(extra, corrections):
            track, _ = _track(skins[source] @ correction, p)
            tracks.append(track)
        frames.append((t, tracks, skins))
    return times, frames, worst


DEFAULT_ANIMATIONS = [
    (0, 'Stand', ['Stand', 'Stand 01', 'Stand Ready'], True),
    (4, 'Walk', ['Walk', 'Walk A', 'Walk 01'], True),
    (5, 'Run', ['Walk Fast', 'Walk A Superior', 'Walk Superior', 'Walk', 'Walk A'], True),
    (13, 'WalkBackwards', ['Walk Back', 'Walk A Back'], True),
    (37, 'JumpStart', ['Jump Start', 'Stand Ready Start'], False),
    (38, 'Jump', ['Jump', 'Flail'], True),
    (39, 'JumpEnd', ['Jump End', 'Stand Ready End'], False),
    (40, 'Fall', ['Fall', 'Flail'], True),
    (1, 'Death', ['Death'], False),
    (6, 'Dead', ['Dead'], True),
    (60, 'SpellPrecast', ['Spell A Start'], False),
    (69, 'CustomSpell01', ['Taunt', 'Stand Victory'], False),
]


def face_billboards(m3, bones, vertices):
    children = {}
    for i, b in enumerate(m3['bones']):
        children.setdefault(b['parent'], []).append(i)
    moved = set()
    for board in m3.get('billboards', []):
        root = board['bone']
        family, stack = set(), [root]
        while stack:
            b = stack.pop()
            family.add(b)
            stack.extend(children.get(b, []))
        bones[root]['flags'] |= 0x8
        ids = [i for i, v in enumerate(vertices)
               if i not in moved and v['bones'][int(np.argmax(v['weights']))] in family]
        if len(ids) < 3:
            continue
        pts = np.array([vertices[i]['position'] for i in ids])
        centre = np.array(bones[root]['pivot'])
        _, spread, axes = np.linalg.svd(pts - pts.mean(0))
        if spread[2] > 0.05 * spread[1]:
            continue
        normal = axes[2] if axes[2][0] >= 0 else -axes[2]
        up = axes[0] if abs(axes[0][2]) >= abs(axes[1][2]) else axes[1]
        up = up - normal * (up @ normal)
        up = up / (np.linalg.norm(up) or 1)
        if up[2] < 0:
            up = -up
        side = np.cross(up, normal)
        turn = np.array([[1.0, 0, 0], [0, 1.0, 0], [0, 0, 1.0]]) @ np.linalg.inv(np.stack([normal, side, up]).T)
        for i in ids:
            v = vertices[i]
            v['position'] = list(centre + turn @ (np.array(v['position']) - centre))
            n = turn @ np.array(v['normal'])
            v['normal'] = list(n / (np.linalg.norm(n) or 1))
            moved.add(i)
    return moved


M2_PARTICLE_BLEND = {'opaque': 0, 'cutout': 1, 'alpha': 2, 'additive': 4, 'add_alpha': 4, 'mod': 5, 'mod2x': 6}


def m2_particle(ps, bone, pivot, scale, chosen, sequences):
    rates = []
    for c in chosen:
        seq = sequences.get(c['source']) if c['source'] else None
        entry = seq['data'].get(ps['rate_id']) if seq and ps['rate_id'] else None
        rates.append(float(np.mean(entry[1])) if entry else float(ps['rate']))
    if max(rates) <= 0:
        return None
    lo, hi = sorted((ps['life'], ps['life2'] if ps['life2'] > 0 else ps['life']))
    life = max((lo + hi) / 2, 0.05)
    if ps['max'] > 0:
        rates = [min(r, ps['max'] / life) for r in rates]
    s1, s2 = ps['speed'], ps['speed2']
    speed = (s1 + s2) / 2 if s2 else s1
    variation = min(1.0, abs(s2 - s1) / 2 / abs(speed)) if s2 and speed else 0.0
    radial = ps['emit_type'] not in (0, 2)
    round_shape = ps['shape'] in (2, 4, 5)
    radius = ps['radius'] * scale
    box = [abs(x) * scale for x in ps['box']]
    if radial:
        emitter_type, length, width = 2, 0.0, radius if round_shape else max(box[:2])
        vertical = math.pi / 2 if ps['shape'] == 2 else 0.35
        horizontal = math.pi
    else:
        emitter_type = 1
        length, width = ((2 * radius, 2 * radius) if round_shape else
                         (box[0], box[1]) if ps['shape'] in (1, 3) else (0.0, 0.0))
        vertical, horizontal = max(abs(x) for x in ps['spread']), math.pi
    sizes = []
    for k in range(3):
        a, b = ps['size'][k], (ps['size2'][k] if any(ps['size2']) else ps['size'][k])
        sizes.append(max((a + b) / 2, 0.0) * scale * 0.5)
    vary = 0.0
    if any(ps['size2']) and any(ps['size']):
        vary = min(1.0, abs(ps['size2'][0] - ps['size'][0]) / max(ps['size'][0] + ps['size2'][0], 1e-3))
    mid = min(max(ps['size_mid'], 0.01), 0.99)
    colours = [tuple(255.0 * (max(x, 0.0) / 255.0) ** (1 / 2.2) for x in c[:3]) for c in ps['colors']]
    top = max(max(c) for c in colours) or 255.0
    colours = [tuple(min(255.0, x * 255.0 / top) for x in c) for c in colours] if top < 255 else colours
    cmid = min(max(ps['color_mid'], 0.01), 0.99)
    amid = min(max(ps['alpha_mid'], 0.01), 0.99)
    alphas = [int(round(max(0.0, min(255.0, c[3])) / 255.0 * 32767)) for c in ps['colors']]
    if max(alphas) <= 0:
        return None
    cells = max(ps['rows'], 1) * max(ps['cols'], 1)
    flags = 0x10 | 0x20000
    if not ps['extra_flags'] & 0x8:
        flags |= 0x80
    if ps['type'] == 3:
        flags |= 0x1000
    spin = abs(ps['rotation'][1] - ps['rotation2'][1]) / 2 if len(ps['rotation']) > 1 else 0.0
    return dict(bone=bone, position=tuple(float(x) for x in pivot), texture=ps['texture'],
                blend=M2_PARTICLE_BLEND.get(ps['blend'], 2), emitter_type=emitter_type,
                rows=max(ps['cols'], 1), cols=max(ps['rows'], 1),
                emission_speed=speed * scale, speed_variation=variation, vertical_range=vertical,
                horizontal_range=horizontal, gravity=ps['gravity'] * scale * 2.0, lifespan=life,
                lifespan_vary=(hi - lo) / 2, area_length=length, area_width=width,
                tracks=dict(emission_rate=dict(interp=0, keys=[([0], [(r,)]) for r in rates])),
                colors=[(0.0, colours[0]), (cmid, colours[1]), (1.0, colours[2])],
                alphas=[(0.0, (alphas[0],)), (amid, (alphas[1],)), (1.0, (alphas[2],))],
                scales=[(0.0, (sizes[0], sizes[0])), (mid, (sizes[1], sizes[1])), (1.0, (sizes[2], sizes[2]))],
                scale_vary=(vary, 0.0),
                head_cells=[(0.0, (0,)), (1.0, (cells - 1,))] if cells > 1 else [(0.0, (0,))],
                drag=ps['drag'], base_spin=0.0, base_spin_vary=math.pi, spin=0.0, spin_vary=spin, flags=flags)


def to_scene(m3, cfg):
    conv = conversion_matrix(cfg.get('rotation_z_degrees', 90.0), cfg.get('scale', 1.0), cfg.get('offset', (0, 0, 0)),
                             cfg.get('pitch_degrees', 0.0))
    rot3 = conv[:3, :3] / cfg.get('scale', 1.0)
    skip = set(cfg.get('skip_regions', []))
    vertices, groups = flat_mesh(m3, lambda r: r['index'] not in skip)
    require(vertices, 'No mesh regions left after skip_regions')
    rest_world = [np.linalg.inv(ib) for ib in m3['inverse_bind']]
    pivots = [(conv @ w)[:3, 3] for w in rest_world]

    chosen = []
    for entry in cfg.get('animations', DEFAULT_ANIMATIONS):
        if not isinstance(entry, dict):
            wow_id, wow_name, names, loop = entry
            entry = dict(id=wow_id, name=wow_name, hots=names, loop=loop)
        name = next((n for n in entry['hots'] if n in m3['sequences']), None)
        if name is None:
            continue
        chosen.append(dict(entry, source=name, variation=0, frequency=entry.get('frequency', 32767)))
        for extra in entry.get('variations', []):
            if extra in m3['sequences']:
                chosen.append(dict(entry, source=extra, variation=1, frequency=entry.get('variation_frequency', 2000)))
    if not any(c['id'] == 0 for c in chosen):
        chosen.insert(0, dict(id=0, name='Stand', source=None, loop=True, variation=0, frequency=32767))
    chosen.sort(key=lambda c: (c['id'], c['variation']))
    for i, c in enumerate(chosen):
        same = [k for k, d in enumerate(chosen) if d['id'] == c['id']]
        c['variation'] = same.index(i)
        nxt = same.index(i) + 1
        c['variation_next'] = same[nxt] if nxt < len(same) else -1

    fps = cfg.get('fps', 30)
    names = {a['name']: a for a in m3['attachments']}
    extra, level_bone = [], {}
    for wow_id, hots_name in cfg.get('attachments', []):
        a = names.get(hots_name)
        if wow_id not in cfg.get('level_attachments', ()) or a is None:
            continue
        level_bone[wow_id] = len(m3['bones']) + len(extra)
        extra.append((a['bone'], np.array(pivots[a['bone']]), None))
    emitters = []
    rot3n = conv[:3, :3] / np.linalg.norm(conv[:3, :3], axis=0)
    for ps in cfg.get('particles', []):
        aim = rot3n @ rest_world[ps['bone']][:3, :3]
        aim = aim / np.where(np.linalg.norm(aim, axis=0) == 0, 1, np.linalg.norm(aim, axis=0))
        u, _, vt = np.linalg.svd(aim)
        aim = u @ vt
        if ps['emit_type'] in (0, 2):
            ax, ay = (math.radians(a) for a in ps['angle'])
            rx = np.array([[1, 0, 0], [0, math.cos(ax), -math.sin(ax)], [0, math.sin(ax), math.cos(ax)]])
            ry = np.array([[math.cos(ay), 0, math.sin(ay)], [0, 1, 0], [-math.sin(ay), 0, math.cos(ay)]])
            aim = aim @ rx @ ry
        emitters.append((len(m3['bones']) + len(extra), ps))
        extra.append((ps['bone'], np.array(pivots[ps['bone']]), aim))
    bone_count = len(m3['bones']) + len(extra)
    sequences, per_bone, worst = [], [dict(translation=[], rotation=[], scale=[]) for _ in range(bone_count)], 0.0
    report = []
    scale = cfg.get('scale', 1.0)
    conv_vertices = [dict(v, pos=tuple((conv @ np.array(v['pos'] + (1.0,)))[:3])) for v in vertices]
    for c in chosen:
        seq = m3['sequences'].get(c['source']) if c['source'] else None
        times, frames, err = bake_sequence(m3, seq, conv, pivots, fps, extra)
        worst = max(worst, err)
        sample = [frames[0][2], frames[len(frames) // 2][2], frames[-1][2]]
        pts = np.concatenate([skin_positions(conv_vertices, s) for s in sample])
        lo, hi = pts.min(0), pts.max(0)
        radius = float(np.linalg.norm(pts - (lo + hi) / 2, axis=1).max())
        natural = (seq['speed'] / 100.0 if seq else 0.0) * scale
        speed = float(c.get('movespeed', natural))
        sequences.append(dict(id=c['id'], name=c['name'], source=c['source'], duration=times[-1] or 1000,
                              movespeed=speed, flags=32 if c['loop'] else 33, variation=c['variation'],
                              variation_next=c['variation_next'], frequency=c['frequency'],
                              minimum=list(lo), maximum=list(hi), radius=radius))
        for bi in range(bone_count):
            per_bone[bi]['translation'].append((times, [f[1][bi]['translation'] for f in frames]))
            qs, prev = [], None
            for f in frames:
                q = f[1][bi]['rotation']
                if prev is not None and sum(a * b for a, b in zip(q, prev)) < 0:
                    q = tuple(-x for x in q)
                qs.append(q)
                prev = q
            per_bone[bi]['rotation'].append((times, qs))
            per_bone[bi]['scale'].append((times, [f[1][bi]['scale'] for f in frames]))
        report.append(dict(wow_id=c['id'], wow=c['name'], variation=c['variation'], hots=c['source'], ms=times[-1],
                           movespeed=round(speed, 3), natural_speed=round(natural, 3)))

    from m2_writer import encode_quaternion

    def channel(keys, fmt, encode=None):
        out = []
        for times, values in keys:
            values = [tuple(encode(v)) if encode else tuple(v) for v in values]
            if all(v == values[0] for v in values):
                times, values = [0], values[:1]
            out.append((list(times), values))
        return dict(interp=1, keys=out)

    bones = []
    for bi, b in enumerate(m3['bones']):
        bones.append(dict(name=b['name'], parent=b['parent'], pivot=list(pivots[bi]), flags=0x200, key=-1,
                          channels=dict(translation=channel(per_bone[bi]['translation'], '3f'),
                                        rotation=channel(per_bone[bi]['rotation'], '4h', encode_quaternion),
                                        scale=channel(per_bone[bi]['scale'], '3f'))))
    for k, (source, p, turn) in enumerate(extra):
        bi = len(m3['bones']) + k
        bones.append(dict(name=m3['bones'][source]['name'] + ('_Level' if turn is None else f'_Emitter{k}'), parent=-1, pivot=list(p), flags=0x200, key=-1,
                          channels=dict(translation=channel(per_bone[bi]['translation'], '3f'),
                                        rotation=channel(per_bone[bi]['rotation'], '4h', encode_quaternion),
                                        scale=channel(per_bone[bi]['scale'], '3f'))))

    out_vertices = []
    for v in vertices:
        p = conv @ np.array(v['pos'] + (1.0,))
        n = rot3 @ np.array(v['normal'])
        n = n / (np.linalg.norm(n) or 1)
        out_vertices.append(dict(position=list(p[:3]), normal=list(n), uv=list(v['uv']), weights=v['weights'],
                                 bones=v['bones']))

    billboard_vertices = face_billboards(m3, bones, out_vertices)

    textures = [dict(path=p, type=0, flags=0) if isinstance(p, str) else p for p in cfg['textures']]
    out_groups = []
    if 'passes' in cfg:
        materials, by_region = [], {g['region']: g for g in groups}
        for p in cfg['passes']:
            g = by_region.get(p['region'])
            if g is None:
                continue
            key = dict(p['material'])
            if key not in materials:
                materials.append(key)
            first = next((i for i, o in enumerate(out_groups) if o['region'] == p['region'] and o.get('share') is None),
                         None)
            out_groups.append(dict(triangles=g['triangles'], texture=p['texture'], material=materials.index(key),
                                   geoset=0, batch_flags=p.get('batch_flags', 16), region=p['region'], share=first))
        require(out_groups, 'No render pass left')
    else:
        region_texture = {int(k): v for k, v in cfg.get('region_texture', {}).items()}
        materials = [dict(blend='opaque'), dict(blend='cutout', two_sided=True)]
        cutout = set(cfg.get('cutout_regions', []))
        for g in groups:
            out_groups.append(dict(triangles=g['triangles'], texture=region_texture.get(g['region'], 0),
                                   material=1 if g['region'] in cutout else 0, geoset=0))

    if billboard_vertices:
        for g in out_groups:
            if any(vi in billboard_vertices for t in g['triangles'] for vi in t):
                key = dict(materials[g['material']], two_sided=True)
                if key not in materials:
                    materials.append(key)
                g['material'] = materials.index(key)

    particles = [m2_particle(ps, bone, pivots[ps['bone']], cfg.get('scale', 1.0), chosen, m3['sequences'])
                 for bone, ps in emitters]
    particles = [p for p in particles if p]

    attachments = []
    for wow_id, hots_name in cfg.get('attachments', []):
        a = names.get(hots_name)
        if a is None:
            continue
        attachments.append(dict(id=wow_id, bone=level_bone.get(wow_id, a['bone']), position=list(pivots[a['bone']]),
                                source=hots_name))

    lo = np.min([s['minimum'] for s in sequences], axis=0)
    hi = np.max([s['maximum'] for s in sequences], axis=0)
    scene = dict(name=cfg.get('name', 'model'), vertices=out_vertices, bones=bones, sequences=sequences,
                 textures=textures, materials=materials, groups=out_groups, attachments=attachments,
                 particles=particles,
                 bounds=dict(minimum=list(lo), maximum=list(hi), radius=float(max(s['radius'] for s in sequences))))
    return scene, dict(animations=report, worst_rotation_error=worst, bones=len(bones), vertices=len(out_vertices),
                       attachments=[(a['id'], a['source']) for a in attachments])
