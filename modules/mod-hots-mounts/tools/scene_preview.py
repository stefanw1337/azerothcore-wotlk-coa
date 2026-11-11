import math

import numpy as np
from PIL import Image, ImageDraw

from m2_writer import decode_quaternion


def _key(channel, seq, t, kind):
    times, values = channel['keys'][seq]
    if kind == 'rotation':
        values = [decode_quaternion(v) for v in values]
    if len(times) == 1 or t <= times[0]:
        return np.asarray(values[0], float)
    if t >= times[-1]:
        return np.asarray(values[-1], float)
    i = int(np.searchsorted(times, t, side='right')) - 1
    f = (t - times[i]) / max(1, times[i + 1] - times[i])
    a, b = np.asarray(values[i], float), np.asarray(values[i + 1], float)
    if kind == 'rotation':
        if np.dot(a, b) < 0:
            b = -b
        r = a * (1 - f) + b * f
        return r / np.linalg.norm(r)
    return a * (1 - f) + b * f


def _quat(q):
    x, y, z, w = q
    return np.array([[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
                     [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
                     [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]])


def bone_matrices(scene, seq, t):
    out = []
    for b in scene['bones']:
        ch = b['channels']
        tr = _key(ch['translation'], seq, t, 'translation')
        q = _key(ch['rotation'], seq, t, 'rotation')
        s = _key(ch['scale'], seq, t, 'scale')
        p = np.asarray(b['pivot'], float)
        m = np.eye(4)
        m[:3, :3] = _quat(q) * s
        m[:3, 3] = p + tr - m[:3, :3] @ p
        out.append(out[b['parent']] @ m if b['parent'] >= 0 else m)
    return out


def posed_vertices(scene, seq, t):
    mats = np.stack(bone_matrices(scene, seq, t))
    pos = np.array([v['position'] + [1.0] for v in scene['vertices']])
    out = np.zeros((len(pos), 3))
    for k in range(4):
        ids = np.array([v['bones'][k] for v in scene['vertices']])
        w = np.array([v['weights'][k] for v in scene['vertices']]) / 255.0
        out += np.einsum('nij,nj->ni', mats[ids], pos)[:, :3] * w[:, None]
    return out, mats


def render(scene, seq, t, path, size=512, views=('side', 'front', 'top'), markers=()):
    pts, mats = posed_vertices(scene, seq, t)
    tris = np.array([tri for g in scene['groups'] for tri in g['triangles']])
    lo, hi = pts.min(0), pts.max(0)
    span = float((hi - lo).max()) or 1.0
    axes = dict(side=(0, 2, 1), front=(1, 2, 0), top=(0, 1, 2))
    canvas = Image.new('RGB', (size * len(views), size), (40, 40, 48))
    draw = ImageDraw.Draw(canvas)
    light = np.array([0.4, -0.5, 0.75])
    light /= np.linalg.norm(light)
    for vi, view in enumerate(views):
        a, b, depth = axes[view]
        sign = -1 if view == 'front' else 1
        p = pts[tris]
        n = np.cross(p[:, 1] - p[:, 0], p[:, 2] - p[:, 0])
        n /= np.linalg.norm(n, axis=1, keepdims=True) + 1e-9
        shade = np.clip(np.abs(n @ light), 0.15, 1.0)
        key = {'side': -p[:, :, 1], 'front': p[:, :, 0], 'top': p[:, :, 2]}[view].mean(1)
        order = np.argsort(key)
        def xy(q):
            u = (q[a] - lo[a]) if sign > 0 else (hi[a] - q[a])
            return (vi * size + 20 + u / span * (size - 40), size - 20 - (q[b] - lo[b]) / span * (size - 40))
        for i in order:
            c = int(200 * shade[i]) + 30
            draw.polygon([xy(q) for q in p[i]], fill=(c, c, int(c * 0.9)))
        for label, (bone, pos) in markers:
            q = mats[bone] @ np.array(list(pos) + [1.0])
            x, y = xy(q[:3])
            draw.ellipse((x - 5, y - 5, x + 5, y + 5), outline=(255, 60, 60), width=2)
            draw.text((x + 7, y - 7), label, fill=(255, 120, 120))
        draw.text((vi * size + 6, 4), view, fill=(200, 200, 200))
    canvas.save(path)
    return dict(minimum=list(lo), maximum=list(hi))
