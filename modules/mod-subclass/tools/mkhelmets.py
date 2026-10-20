"""Fit the Orc-female helmets onto the Two-Headed Ogre and check every one of them offline.
    python mkhelmets.py check <helmets.zip> <ogre.m2> <ogre00.skin> <ogre skin.png> <out folder>
    python mkhelmets.py copy <helmets.zip> <out folder>
check renders every helmet on her (front and side), measures how much of her face it covers against the Orc
female it was made for and whether it cuts into the cyclops head, and writes contact sheets plus report.json.
copy writes her own helmet files (prefix Tw) with the per-armor-type and per-helmet corrections baked in."""
import io
import json
import math
import struct
import sys
import zipfile
from dataclasses import dataclass
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw

from mkcape import arr
from mkeyes import HELMET_ATTACHMENT, HELMET_SCALE, HELMET_SHIFT, ATTACHMENTS, ATTACHMENT_SIZE, ATTACHMENT_POSITION

HEAD = "item/objectcomponents/head/"
SOURCE_SUFFIX, OWN_SUFFIX = "_orf", "_twf"
ORC_FEMALE = "character/orc/female/orcfemale"
VERTEX_SIZE = 48
TWO_EYED_HEAD, CYCLOPS_HEAD = 21, 20
ARMOR = {0: "misc", 1: "cloth", 2: "leather", 3: "mail", 4: "plate"}
CORRECTIONS = {
    "cloth": {"shift": (0.0, 0.0, 0.0), "scale": (1.0, 1.0, 1.0)},
    "leather": {"shift": (0.0, 0.0, 0.0), "scale": (1.0, 1.0, 1.0)},
    "mail": {"shift": (0.0, 0.0, 0.0), "scale": (1.0, 1.0, 1.0)},
    "plate": {"shift": (0.0, 0.0, 0.0), "scale": (1.0, 1.0, 1.0)},
    "misc": {"shift": (0.0, 0.0, 0.0), "scale": (1.0, 1.0, 1.0)},
}
HELMET_CORRECTIONS = {
    "armor_diversuit_d_01_helm": {"shift": (0.0, -0.05, 0.0), "scale": (0.9, 0.9, 0.9)},
    "helm_engi_c_01diver": {"shift": (0.0, -0.05, 0.0), "scale": (0.85, 0.85, 0.85)},
    "helm_engi_c_02space": {"shift": (0.0, -0.1, 0.0), "scale": (0.85, 0.85, 0.85)},
    "helm_mask_pumpkin": {"shift": (0.0, -0.05, 0.0), "scale": (0.9, 0.9, 0.9)},
    "helm_robe_raidwarlock_e_01": {"shift": (0.0, -0.05, 0.0), "scale": (0.9, 0.9, 0.9)},
    "helm_plate_raidwarrior_h_01": {"shift": (0.0, -0.05, 0.0), "scale": (0.85, 0.85, 0.85)},
}
COVERAGE_SHORTFALL = 0.15
CLIP_LIMIT = 0.02
THUMB = 256
THUMB_SPAN = 1.4
THUMB_FRONT = (0.0, 0.25, -0.15)
THUMB_SIDE = (0.0, 0.0, -0.15)
FRAME_MARGIN = 2.0
HEAD_MARGIN = 0.02
BONE_SCALE_VALUES = 68
NECK_DROP = 0.3
SHEET_COLUMNS = 6


@dataclass
class Mesh:
    positions: np.ndarray
    normals: np.ndarray
    uvs: np.ndarray
    bones: np.ndarray
    triangles: np.ndarray


def load_mesh(m2, skin, geosets=None):
    count, offset = arr(m2, 60)
    raw = np.frombuffer(m2, np.uint8, count * VERTEX_SIZE, offset).reshape(-1, VERTEX_SIZE)
    positions = raw[:, 0:12].copy().view(np.float32).reshape(-1, 3).astype(np.float64)
    weights = raw[:, 12:16].astype(np.int32)
    indices = raw[:, 16:20].astype(np.int32)
    normals = raw[:, 20:32].copy().view(np.float32).reshape(-1, 3).astype(np.float64)
    uvs = raw[:, 32:40].copy().view(np.float32).reshape(-1, 2).astype(np.float64)
    bones = indices[np.arange(len(indices)), weights.argmax(axis=1)]
    lookup_count, lookup_offset = arr(skin, 4)
    lookup = np.frombuffer(skin, np.uint16, lookup_count, lookup_offset).astype(np.int64)
    triangle_count, triangle_offset = arr(skin, 12)
    corners = np.frombuffer(skin, np.uint16, triangle_count, triangle_offset).astype(np.int64)
    submesh_count, submesh_offset = arr(skin, 28)
    triangles = []
    for index in range(submesh_count):
        geoset, level, _, _, start, size = struct.unpack_from("<6H", skin, submesh_offset + index * 48)
        if geosets is not None and geoset not in geosets:
            continue
        start |= level << 16
        triangles.append(lookup[corners[start:start + size]].reshape(-1, 3))
    return Mesh(positions, normals, uvs, bones, np.concatenate(triangles) if triangles else np.zeros((0, 3), int))


def bone_parents(m2):
    count, offset = arr(m2, 44)
    return [struct.unpack_from("<h", m2, offset + index * 88 + 8)[0] for index in range(count)]


def subtree(parents, roots):
    inside = set(roots)
    changed = True
    while changed:
        changed = False
        for bone, parent in enumerate(parents):
            if parent in inside and bone not in inside:
                inside.add(bone)
                changed = True
    return inside


def pivots(m2):
    count, offset = arr(m2, 44)
    return np.array([struct.unpack_from("<3f", m2, offset + index * 88 + 76) for index in range(count)])


def resting_scale(m2, bone):
    animations, offset = struct.unpack_from("<II", m2, arr(m2, 44)[1] + bone * 88 + BONE_SCALE_VALUES)
    if not animations:
        return 1.0
    keys, values = struct.unpack_from("<II", m2, offset)
    return np.array(struct.unpack_from("<3f", m2, values)) if keys else 1.0


def seated(m2, bone, position, scale):
    pivot = pivots(m2)[bone]
    grown = resting_scale(m2, bone)
    return pivot + grown * (position - pivot), grown * np.array(scale)


def helmet_attachment(m2):
    count, offset = arr(m2, ATTACHMENTS)
    for index in range(count):
        attachment = offset + index * ATTACHMENT_SIZE
        attachment_id, bone = struct.unpack_from("<IH", m2, attachment)
        if attachment_id == HELMET_ATTACHMENT:
            return bone, np.array(struct.unpack_from("<3f", m2, attachment + ATTACHMENT_POSITION))
    raise ValueError("no helmet attachment")


def placed(mesh, origin, scale=(1.0, 1.0, 1.0), shift=(0.0, 0.0, 0.0)):
    return Mesh(origin + np.array(shift) + mesh.positions * np.array(scale), mesh.normals, mesh.uvs, mesh.bones,
                mesh.triangles)


VIEWS = {"front": (0, 1, 1.0), "side": (1, 0, -1.0)}


def raster(layers, view, width, height, centre, span):
    depth_axis, across_axis, facing = VIEWS[view]
    image = np.zeros((height, width, 3), np.float32)
    image[:] = (28, 28, 36)
    depth = np.full((height, width), -1e9)
    owner = np.full((height, width), -1, np.int64)
    light = np.array([0.7, 0.3, 0.6])
    light /= np.linalg.norm(light)
    scale = height / span
    for layer, (mesh, texture, tags) in enumerate(layers):
        tex_h, tex_w = texture.shape[:2]
        points = mesh.positions - centre
        sx = width / 2 - points[:, across_axis] * scale
        sy = height / 2 - points[:, 2] * scale
        for triangle_index, triangle in enumerate(mesh.triangles):
            a, b, c = triangle
            px, py, pd = sx[triangle], sy[triangle], points[triangle, depth_axis] * facing
            x0, x1 = int(max(0, math.floor(px.min()))), int(min(width - 1, math.ceil(px.max())))
            y0, y1 = int(max(0, math.floor(py.min()))), int(min(height - 1, math.ceil(py.max())))
            if x1 < x0 or y1 < y0:
                continue
            den = (py[1] - py[2]) * (px[0] - px[2]) + (px[2] - px[1]) * (py[0] - py[2])
            if abs(den) < 1e-9:
                continue
            xs, ys = np.meshgrid(np.arange(x0, x1 + 1), np.arange(y0, y1 + 1))
            l0 = ((py[1] - py[2]) * (xs - px[2]) + (px[2] - px[1]) * (ys - py[2])) / den
            l1 = ((py[2] - py[0]) * (xs - px[2]) + (px[0] - px[2]) * (ys - py[2])) / den
            l2 = 1 - l0 - l1
            inside = (l0 >= 0) & (l1 >= 0) & (l2 >= 0)
            if not inside.any():
                continue
            z = l0 * pd[0] + l1 * pd[1] + l2 * pd[2]
            uv = mesh.uvs[triangle]
            u = l0 * uv[0, 0] + l1 * uv[1, 0] + l2 * uv[2, 0]
            v = l0 * uv[0, 1] + l1 * uv[1, 1] + l2 * uv[2, 1]
            colour = texture[np.clip((v * tex_h).astype(int) % tex_h, 0, tex_h - 1),
                             np.clip((u * tex_w).astype(int) % tex_w, 0, tex_w - 1)]
            normal = np.cross(mesh.positions[b] - mesh.positions[a], mesh.positions[c] - mesh.positions[a])
            length = np.linalg.norm(normal)
            shade = 0.45 + 0.55 * abs(float(np.dot(normal / length, light))) if length else 0.6
            window = depth[y0:y1 + 1, x0:x1 + 1]
            nearer = inside & (z > window)
            window[nearer] = z[nearer]
            image[y0:y1 + 1, x0:x1 + 1][nearer] = colour[nearer] * shade
            owner[y0:y1 + 1, x0:x1 + 1][nearer] = tags[triangle_index] if tags is not None else layer * 1000
    return image, owner


def head_region(mesh, head_bones, floor):
    points = mesh.positions[np.isin(mesh.bones, list(head_bones))]
    points = points[points[:, 2] >= floor]
    low = np.percentile(points, 1, axis=0) - HEAD_MARGIN
    high = np.percentile(points, 99, axis=0) + HEAD_MARGIN
    inside = ((mesh.positions >= low) & (mesh.positions <= high)).all(axis=1)
    return inside[mesh.triangles].all(axis=1)


def head_only(mesh, region):
    return Mesh(mesh.positions, mesh.normals, mesh.uvs, mesh.bones, mesh.triangles[region])


HEAD_TAG, BODY_TAG, HELMET_TAG = 1, 2, 3
COVERAGE_SIZE = 200


def framing(head):
    used = head.positions[np.unique(head.triangles)]
    low, high = used.min(axis=0), used.max(axis=0)
    return (low + high) / 2, float((high - low).max()) * FRAME_MARGIN


def coverage(head, helmet, view, centre, span):
    plain = np.full((8, 8, 3), 150, np.float32)
    head_layer = (head, plain, np.full(len(head.triangles), HEAD_TAG))
    bare = raster([head_layer], view, COVERAGE_SIZE, COVERAGE_SIZE, centre, span)[1]
    worn = raster([head_layer, (helmet, plain, np.full(len(helmet.triangles), HELMET_TAG))],
                  view, COVERAGE_SIZE, COVERAGE_SIZE, centre, span)[1]
    silhouette = bare == HEAD_TAG
    return float(((worn == HELMET_TAG) & silhouette).sum()) / max(1, int(silhouette.sum()))


def clipping(helmet, body, head_bones):
    points = body.positions[np.isin(body.bones, list(head_bones))]
    if not len(points):
        return 0.0
    centre = points.mean(axis=0)
    radii = np.maximum((points.max(axis=0) - points.min(axis=0)) / 2 * 0.85, 1e-3)
    inside = (((helmet.positions - centre) / radii) ** 2).sum(axis=1) < 1.0
    return float(inside.mean())


def read_dbc(data):
    _, count, fields, size, _ = struct.unpack_from("<4s4I", data)
    rows = [data[20 + index * size:20 + (index + 1) * size] for index in range(count)]
    strings = data[20 + count * size:]
    return rows, strings


def dbc_text(strings, offset):
    return strings[offset:strings.index(b"\0", offset)].decode("utf-8", "replace")


def armor_of_models(bundle):
    items, _ = read_dbc(bundle.read("DBFilesClient/Item.dbc"))
    displays, strings = read_dbc(bundle.read("DBFilesClient/ItemDisplayInfo.dbc"))
    head_displays = {}
    for row in items:
        _, item_class, subclass, _, _, display, inventory_type, _ = struct.unpack_from("<8i", row)
        if item_class == 4 and inventory_type == 1:
            head_displays.setdefault(display, subclass)
    models, textures = {}, {}
    for row in displays:
        display, model_name, _, model_texture = struct.unpack_from("<4I", row)
        if display not in head_displays:
            continue
        stem = Path(dbc_text(strings, model_name)).stem.lower()
        if not stem:
            continue
        models.setdefault(stem, ARMOR.get(head_displays[display], "misc"))
        textures.setdefault(stem, dbc_text(strings, model_texture).lower())
    return models, textures


def entries(bundle):
    names = {name.lower(): name for name in bundle.namelist()}
    return names


def helmet_texture(bundle, names, texture):
    name = names.get(HEAD + texture + ".blp")
    if not name:
        return np.full((8, 8, 3), 150, np.float32)
    return np.array(Image.open(io.BytesIO(bundle.read(name))).convert("RGB")).astype(np.float32)


def correction(stem, armor):
    fix = dict(CORRECTIONS[armor])
    fix.update(HELMET_CORRECTIONS.get(stem, {}))
    return fix


def check(zip_path, ogre_m2, ogre_skin, ogre_texture, out):
    out = Path(out)
    out.mkdir(parents=True, exist_ok=True)
    bundle = zipfile.ZipFile(zip_path)
    names = entries(bundle)
    models, textures = armor_of_models(bundle)
    ogre_data, ogre_skin_data = Path(ogre_m2).read_bytes(), Path(ogre_skin).read_bytes()
    ogre = load_mesh(ogre_data, ogre_skin_data, {0})
    ogre_parents = bone_parents(ogre_data)
    head_bone, attach = helmet_attachment(ogre_data)
    ogre_head = head_only(ogre, head_region(ogre, subtree(ogre_parents, {TWO_EYED_HEAD}),
                                            pivots(ogre_data)[TWO_EYED_HEAD][2] - NECK_DROP))
    cyclops = subtree(ogre_parents, {CYCLOPS_HEAD})
    ogre_tex = np.array(Image.open(ogre_texture).convert("RGB")).astype(np.float32)
    orc_data = bundle.read(names[ORC_FEMALE + ".m2"])
    orc = load_mesh(orc_data, bundle.read(names[ORC_FEMALE + "00.skin"]), {0})
    orc_bone, orc_attach = helmet_attachment(orc_data)
    orc_parents = bone_parents(orc_data)
    orc_skull = orc_parents[orc_bone]
    orc_head = head_only(orc, head_region(orc, subtree(orc_parents, {orc_skull}),
                                          pivots(orc_data)[orc_skull][2] - NECK_DROP))
    ogre_attach = attach.copy()
    ogre_attach[1] = pivots(ogre_data)[TWO_EYED_HEAD][1]
    orc_centre, orc_span = framing(orc_head)
    ogre_centre, ogre_span = framing(ogre_head)
    report, thumbs = [], []
    for key in sorted(names):
        if not key.startswith(HEAD) or not key.endswith(SOURCE_SUFFIX + ".m2"):
            continue
        stem = Path(key).stem[:-len(SOURCE_SUFFIX)]
        skin_name = names.get(key[:-3] + "00.skin")
        if not skin_name:
            continue
        armor = models.get(stem, "misc")
        fix = correction(stem, armor)
        helmet = load_mesh(bundle.read(names[key]), bundle.read(skin_name))
        texture = helmet_texture(bundle, names, textures.get(stem, ""))
        on_orc = placed(helmet, *seated(orc_data, orc_bone, orc_attach, (1.0, 1.0, 1.0)))
        origin, size = seated(ogre_data, head_bone, ogre_attach + np.array(HELMET_SHIFT), HELMET_SCALE)
        on_ogre = placed(placed(helmet, np.array(fix["shift"]), fix["scale"]), origin, size)
        row = {"model": stem, "armor": armor}
        for view in VIEWS:
            row["orc_" + view] = round(coverage(orc_head, on_orc, view, orc_centre, orc_span), 3)
            row["ogre_" + view] = round(coverage(ogre_head, on_ogre, view, ogre_centre, ogre_span), 3)
        row["cyclops_clip"] = round(clipping(on_ogre, ogre, cyclops), 3)
        row["flags"] = [view for view in VIEWS if row["ogre_" + view] < row["orc_" + view] - COVERAGE_SHORTFALL]
        if row["cyclops_clip"] > CLIP_LIMIT:
            row["flags"].append("cyclops")
        report.append(row)
        layers = [(ogre, ogre_tex, None), (on_ogre, texture, None)]
        front = raster(layers, "front", THUMB, THUMB, ogre_attach + THUMB_FRONT, THUMB_SPAN)[0]
        side = raster(layers, "side", THUMB, THUMB, ogre_attach + THUMB_SIDE, THUMB_SPAN)[0]
        thumbs.append((stem, armor, row["flags"], np.hstack([front, side])))
        print(stem, armor, row["flags"], flush=True)
    write_sheets(thumbs, out)
    (out / "report.json").write_text(json.dumps(report, indent=1))
    flagged = [row for row in report if row["flags"]]
    print(len(report), "helmets,", len(flagged), "flagged")


def write_sheets(thumbs, out):
    per_sheet = SHEET_COLUMNS * 5
    for sheet_index in range(0, len(thumbs), per_sheet):
        batch = thumbs[sheet_index:sheet_index + per_sheet]
        rows = math.ceil(len(batch) / SHEET_COLUMNS)
        sheet = Image.new("RGB", (SHEET_COLUMNS * THUMB * 2, rows * (THUMB + 14)), (12, 12, 16))
        draw = ImageDraw.Draw(sheet)
        for index, (stem, armor, flags, image) in enumerate(batch):
            x, y = (index % SHEET_COLUMNS) * THUMB * 2, (index // SHEET_COLUMNS) * (THUMB + 14)
            sheet.paste(Image.fromarray(np.clip(image, 0, 255).astype(np.uint8)), (x, y + 14))
            draw.text((x + 2, y + 1), "%s %s %s" % (stem[:28], armor, " ".join(flags)),
                      fill=(255, 120, 120) if flags else (220, 220, 220))
        sheet.save(out / ("sheet%02d.png" % (sheet_index // per_sheet)))


def baked(m2, fix):
    data = bytearray(m2)
    count, offset = arr(data, 60)
    scale, shift = np.array(fix["scale"]), np.array(fix["shift"])
    for index in range(count):
        vertex = offset + index * VERTEX_SIZE
        position = np.array(struct.unpack_from("<3f", data, vertex)) * scale + shift
        struct.pack_into("<3f", data, vertex, *position)
        normal = np.array(struct.unpack_from("<3f", data, vertex + 20)) / scale
        length = np.linalg.norm(normal)
        struct.pack_into("<3f", data, vertex + 20, *(normal / length if length else normal))
    for box in (160, 188):
        low = np.array(struct.unpack_from("<3f", data, box)) * scale + shift
        high = np.array(struct.unpack_from("<3f", data, box + 12)) * scale + shift
        struct.pack_into("<6f", data, box, *np.minimum(low, high), *np.maximum(low, high))
    return bytes(data)


def copy(zip_path, out):
    out = Path(out)
    bundle = zipfile.ZipFile(zip_path)
    names = entries(bundle)
    models, _ = armor_of_models(bundle)
    written = 0
    for key, name in sorted(names.items()):
        if not key.startswith(HEAD):
            continue
        stem = Path(key).stem
        if key.endswith(SOURCE_SUFFIX + ".m2"):
            base = stem[:-len(SOURCE_SUFFIX)]
            data = baked(bundle.read(name), correction(base, models.get(base, "misc")))
            target = out / (Path(name).parent / (Path(name).stem[:-len(SOURCE_SUFFIX)] + "_TwF.m2"))
        elif key.endswith(SOURCE_SUFFIX + "00.skin"):
            data = bundle.read(name)
            target = out / (Path(name).parent / (Path(name).stem[:-len(SOURCE_SUFFIX + "00")] + "_TwF00.skin"))
        else:
            continue
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)
        written += 1
    print("wrote", written, "files")


if __name__ == "__main__":
    {"check": check, "copy": copy}[sys.argv[1]](*sys.argv[2:])
