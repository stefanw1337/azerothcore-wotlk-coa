import argparse
import struct as S
from pathlib import Path

import numpy as np
from PIL import Image


def arr(d, o):
    return S.unpack_from('<II', d, o)


def render(model, out, skins=(), size=320, view='side'):
    model = Path(model)
    d = model.read_bytes()
    n, o = arr(d, 60)
    vt = np.frombuffer(d, dtype=np.dtype([('p', '<3f'), ('w', '4u1'), ('b', '4u1'), ('n', '<3f'), ('t', '<2f'),
                                          ('t2', '<2f')]), count=n, offset=o)
    nt, ot = arr(d, 80)
    textures = []
    for i in range(nt):
        kind, flags, ln, off = S.unpack_from('<4I', d, ot + 16 * i)
        path = d[off:off + ln].split(b'\0')[0].decode()
        if kind in (11, 12, 13):
            name = skins[kind - 11] if len(skins) > kind - 11 else ''
            path = str(model.parent / (name + '.blp')) if name else ''
        elif path:
            root = model
            while root.name.lower() != 'creature' and root.parent != root:
                root = root.parent
            path = str(root.parent / path.replace('\\', '/'))
        img = None
        if path and Path(path).exists():
            img = np.asarray(Image.open(path).convert('RGBA'), dtype=np.float32) / 255
        textures.append(img)
    nl, ol = arr(d, 128)
    lookup = [S.unpack_from('<H', d, ol + 2 * i)[0] for i in range(nl)]
    nm, om = arr(d, 112)
    blends = [S.unpack_from('<HH', d, om + 4 * i)[1] for i in range(nm)]
    k = model.read_bytes() if False else model.with_name(model.stem + '00.skin').read_bytes()
    ni, oi = arr(k, 4)
    index = np.frombuffer(k, '<u2', ni, oi)
    nf, of = arr(k, 12)
    faces = np.frombuffer(k, '<u2', nf, of)
    ns, os_ = arr(k, 28)
    sections = [S.unpack_from('<I8H', k, os_ + 48 * i) for i in range(ns)]
    nb, ob = arr(k, 36)
    batches = [S.unpack_from('<HHHHh7H', k, ob + 24 * i) for i in range(nb)]

    pos = vt['p']
    a, b, depth_axis, sign = {'side': (0, 2, 1, 1), 'front': (1, 2, 0, -1), 'top': (0, 1, 2, 1)}[view]
    lo, hi = pos.min(0), pos.max(0)
    span = float((hi - lo)[[a, b]].max()) or 1
    scale = (size - 20) / span
    px = (pos[:, a] - lo[a]) * scale + 10
    py = size - 10 - (pos[:, b] - lo[b]) * scale
    pz = {'side': -pos[:, 1], 'front': pos[:, 0], 'top': -pos[:, 2]}[view]
    color = np.zeros((size, size, 3), np.float32) + 0.16
    zbuf = np.full((size, size), np.inf, np.float32)
    order = sorted(batches, key=lambda bt: blends[bt[5]] in (2, 3, 4, 5, 6))
    for bt in order:
        sec = sections[bt[2]]
        level, vstart, vcount, istart, icount = sec[0] >> 16, sec[1], sec[2], sec[3], sec[4]
        istart += level << 16
        tex = textures[lookup[bt[8]]] if bt[8] < len(lookup) else None
        blend = blends[bt[5]]
        tri = index[faces[istart:istart + icount]].reshape(-1, 3)
        for t in tri:
            xs, ys, zs = px[t], py[t], pz[t]
            x0, x1 = int(max(0, xs.min())), int(min(size - 1, xs.max()) + 1)
            y0, y1 = int(max(0, ys.min())), int(min(size - 1, ys.max()) + 1)
            if x1 <= x0 or y1 <= y0:
                continue
            gx, gy = np.meshgrid(np.arange(x0, x1) + .5, np.arange(y0, y1) + .5)
            den = (ys[1] - ys[2]) * (xs[0] - xs[2]) + (xs[2] - xs[1]) * (ys[0] - ys[2])
            if abs(den) < 1e-9:
                continue
            w0 = ((ys[1] - ys[2]) * (gx - xs[2]) + (xs[2] - xs[1]) * (gy - ys[2])) / den
            w1 = ((ys[2] - ys[0]) * (gx - xs[2]) + (xs[0] - xs[2]) * (gy - ys[2])) / den
            w2 = 1 - w0 - w1
            inside = (w0 >= 0) & (w1 >= 0) & (w2 >= 0)
            if not inside.any():
                continue
            z = w0 * zs[0] + w1 * zs[1] + w2 * zs[2]
            region = zbuf[y0:y1, x0:x1]
            uv = vt['t'][t]
            if tex is not None:
                u = (w0 * uv[0, 0] + w1 * uv[1, 0] + w2 * uv[2, 0]) % 1
                v = (w0 * uv[0, 1] + w1 * uv[1, 1] + w2 * uv[2, 1]) % 1
                th, tw = tex.shape[:2]
                c = tex[(v * th).astype(int).clip(0, th - 1), (u * tw).astype(int).clip(0, tw - 1)]
            else:
                c = np.zeros(gx.shape + (4,), np.float32) + [1, 0, 1, 1]
            if blend in (0, 1):
                keep = inside & (z < region) & ((c[..., 3] > 0.5) if blend == 1 else True)
                region[keep] = z[keep]
                color[y0:y1, x0:x1][keep] = c[..., :3][keep]
            else:
                keep = inside & (z < region)
                alpha = c[..., 3:4] if blend in (2, 4) else 1
                target = color[y0:y1, x0:x1]
                if blend in (3, 4):
                    target[keep] = np.minimum(1, target[keep] + (c[..., :3] * alpha)[keep])
                else:
                    target[keep] = target[keep] * (1 - alpha[keep] if not np.isscalar(alpha) else 0) + \
                        (c[..., :3] * alpha)[keep]
    draw_particles(d, textures, color, lambda q: ((q[a] - lo[a]) * scale + 10, size - 10 - (q[b] - lo[b]) * scale),
                   scale, view)
    Image.fromarray((color * 255).clip(0, 255).astype(np.uint8)).save(out)


def quat_matrix(q):
    x, y, z, w = q
    return np.array([[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
                     [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
                     [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]])


def first_key(d, track, fmt):
    nt, ot, nv, ov = S.unpack_from('<IIII', d, track + 4)
    if not nv:
        return None
    count, at = S.unpack_from('<II', d, ov)
    return S.unpack_from('<' + fmt, d, at) if count else None


def life_key(d, block, fmt, f):
    nt, ot, nv, ov = S.unpack_from('<IIII', d, block)
    size = S.calcsize('<' + fmt)
    times = [S.unpack_from('<H', d, ot + 2 * i)[0] / 32767 for i in range(nt)]
    values = [np.array(S.unpack_from('<' + fmt, d, ov + size * i), float) for i in range(nv)]
    if not values:
        return None
    if f <= times[0] or len(values) == 1:
        return values[0]
    for i in range(1, len(times)):
        if f <= times[i]:
            k = (f - times[i - 1]) / ((times[i] - times[i - 1]) or 1)
            return values[i - 1] * (1 - k) + values[i] * k
    return values[-1]


def draw_particles(d, textures, color, project, scale, view, per_emitter=60):
    n, o = arr(d, 296)
    nb, ob = arr(d, 44)
    rng = np.random.default_rng(7)
    size = color.shape[0]
    sprites = []
    for i in range(n):
        e = o + 476 * i
        flags = S.unpack_from('<I', d, e + 4)[0]
        bone, tex = S.unpack_from('<HH', d, e + 20)
        blend, kind = S.unpack_from('<BB', d, e + 40)
        rows, cols = S.unpack_from('<HH', d, e + 48)
        val = lambda at: (first_key(d, e + at, 'f') or (0.0,))[0]
        speed, vr, hr, grav, life, rate = val(52), val(92), val(112), val(132), max(val(152), 0.05), \
            max(first_key(d, e + 176, 'f') or (0.0,))
        length, width = val(200), val(220)
        drag = S.unpack_from('<f', d, e + 372)[0]
        b = ob + 88 * bone
        pivot = np.array(S.unpack_from('<3f', d, b + 76))
        q = first_key(d, b + 36, '4h')
        rot = quat_matrix([(v + 32768) / 32767 if v < 0 else (v - 32767) / 32767 for v in q]) if q else np.eye(3)
        move = np.array(first_key(d, b + 16, '3f') or (0, 0, 0))
        origin = pivot + move
        img = textures[tex] if tex < len(textures) else None
        if img is None:
            continue
        count = int(min(per_emitter, max(rate * life, 0)))
        for _ in range(count):
            age = rng.random() * life
            polar, azimuth = vr * rng.uniform(-1, 1), hr * rng.uniform(-1, 1)
            if kind == 2:
                direction = np.array([np.cos(polar) * np.cos(azimuth), np.cos(polar) * np.sin(azimuth), np.sin(polar)])
                start = direction * (length + rng.random() * (width - length))
                velocity = direction * speed
            else:
                start = np.array([rng.uniform(-1, 1) * length / 2, rng.uniform(-1, 1) * width / 2, 0.0])
                velocity = np.array([np.cos(azimuth) * np.sin(polar), np.sin(azimuth) * np.sin(polar),
                                     np.cos(polar)]) * speed
            travel = age if drag <= 0 else (1 - np.exp(-drag * age)) / drag
            where = origin + rot @ (start + velocity * travel) - np.array([0, 0, 0.5 * grav * age * age])
            f = age / life
            pick = lambda block, fmt, default: (lambda v: default if v is None else v)(life_key(d, e + block, fmt, f))
            half = pick(292, '2f', np.array([1.0, 1.0]))[0]
            rgb = pick(260, '3f', np.array([255.0, 255.0, 255.0])) / 255
            alpha = pick(276, 'h', np.array([32767.0]))[0] / 32767
            cell = int(pick(316, 'H', np.array([0]))[0])
            depth = {'side': -where[1], 'front': where[0], 'top': -where[2]}[view]
            sprites.append((depth, where, half, rgb, alpha, cell, img, rows, cols, blend))
    sprites.sort(key=lambda s: -s[0])
    for _, where, half, rgb, alpha, cell, img, rows, cols, blend in sprites:
        cx, cy = project(where)
        r = max(1, int(half * scale))
        x0, x1, y0, y1 = int(cx - r), int(cx + r), int(cy - r), int(cy + r)
        if x1 <= 0 or y1 <= 0 or x0 >= size or y0 >= size:
            continue
        th, tw = img.shape[:2]
        cw, ch = tw // max(rows, 1), th // max(cols, 1)
        ox, oy = (cell % max(rows, 1)) * cw, (cell // max(rows, 1)) * ch
        gx, gy = np.meshgrid(np.arange(max(x0, 0), min(x1, size)), np.arange(max(y0, 0), min(y1, size)))
        u = ((gx - x0) / max(x1 - x0, 1) * cw).astype(int).clip(0, cw - 1) + ox
        v = ((gy - y0) / max(y1 - y0, 1) * ch).astype(int).clip(0, ch - 1) + oy
        c = img[v.clip(0, th - 1), u.clip(0, tw - 1)]
        a = c[..., 3:4] * alpha
        target = color[gy, gx]
        if blend in (3, 4):
            color[gy, gx] = np.minimum(1, target + c[..., :3] * rgb * a)
        else:
            color[gy, gx] = target * (1 - a) + c[..., :3] * rgb * a


if __name__ == '__main__':
    p = argparse.ArgumentParser()
    p.add_argument('model')
    p.add_argument('out')
    p.add_argument('--skins', default='')
    p.add_argument('--size', type=int, default=320)
    p.add_argument('--view', choices=('side', 'front', 'top'), default='side')
    a = p.parse_args()
    render(a.model, a.out, [s for s in a.skins.split(',')] if a.skins else [], a.size, a.view)
