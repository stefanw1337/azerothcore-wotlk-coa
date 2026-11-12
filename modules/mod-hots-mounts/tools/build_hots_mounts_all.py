import argparse
import json
import os
import sys
import time
import traceback
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

import hots_m3  # noqa: E402
import m2_model_writer  # noqa: E402
import scene_preview  # noqa: E402
from blp import texture as write_blp  # noqa: E402

SCALE = 0.75
REPLACEABLE = (11, 12, 13)
VISIBLE = 0x80000000
TWO_SIDED, UNSHADED = 0x8, 0x10
MAX_TEXTURE = 1024
TUNING = {}


def log(text):
    print(text, flush=True)


def tex_key(path):
    return path.replace('\\', '/').split('/')[-1].lower() if path else ''


def kind(name):
    for suffix in ('_diff.dds', '_emis.dds'):
        if name.endswith(suffix):
            return suffix
    return ''


def common_prefix(a, b):
    n = 0
    for x, y in zip(a, b):
        if x != y:
            break
        n += 1
    return n


def swapped(base, swaps, bases=()):
    candidates = [b for b in (bases or [base]) if kind(b) == kind(base)]
    for s in swaps:
        if not kind(s) or kind(s) != kind(base):
            continue
        stem = s[:-len(kind(s))]
        owners = [b for b in candidates if stem.startswith(b[:-len(kind(b))])]
        if owners:
            owner = max(owners, key=len)
        else:
            owner = max(candidates, key=lambda b: common_prefix(s, b))
            if common_prefix(s, owner) < 10:
                continue
        if owner == base:
            return s
    return base


def pick_animations(names):
    names = list(names)

    def first(*wanted, starts=None, avoid=('Start', 'End', 'Ridenone', 'Death', 'Dead')):
        for w in wanted:
            if w in names:
                return w
        if starts:
            for n in names:
                if n.startswith(starts) and not any(a in n for a in avoid):
                    return n
        return None

    stand = first('Stand', starts='Stand')
    walk = first('Walk', 'Walk Fast', starts='Walk')
    fidgets = [n for n in names if n.startswith('Stand ') and n[6:].strip().isdigit()]
    anims = []
    if stand:
        anims.append(dict(id=0, name='Stand', hots=[stand], loop=True, variations=fidgets, frequency=30000,
                          variation_frequency=900))
    if walk:
        anims.append(dict(id=4, name='Walk', hots=[walk], loop=True, movespeed=4.0))
        anims.append(dict(id=5, name='Run', hots=[walk], loop=True, movespeed=10.0))
        air = first('Custom S', 'Fly', starts='Fly') or walk
        anims.append(dict(id=38, name='Jump', hots=[air], loop=True))
        anims.append(dict(id=40, name='Fall', hots=[air], loop=True))
    if first('Walk Start'):
        anims.append(dict(id=37, name='JumpStart', hots=['Walk Start'], loop=False))
    if first('Stand Start'):
        anims.append(dict(id=39, name='JumpEnd', hots=['Stand Start'], loop=False))
    return anims


def material_passes(m3, used, textures_folder, report):
    out = {}
    have = lambda name: bool(name) and (textures_folder / name).exists()
    for index in used:
        mat = m3['materials'][index]
        diffuse, emissive, alpha = tex_key(mat['diffuse']), tex_key(mat['emissive']), tex_key(mat['alpha'])
        if mat['type'] == 1 and not mat['flags'] & VISIBLE:
            report['skipped_materials'].append(f'{index} {mat["name"]}: not visible')
            continue
        for name in (diffuse, emissive, alpha):
            if name and not have(name):
                report['missing_textures'].append(name)
        blend = {0: 'opaque', 1: 'alpha', 2: 'additive', 3: 'add_alpha', 4: 'mod', 5: 'mod2x'}.get(mat['blend'], 'opaque')
        if blend == 'opaque' and mat['alpha_test']:
            blend = 'cutout'
        if mat['type'] == 12 and mat['transparent']:
            blend = 'alpha'
        material = dict(blend=blend, two_sided=bool(mat['flags'] & TWO_SIDED), unlit=bool(mat['flags'] & UNSHADED))
        bake = None
        if (mat['type'] == 12 and not have(diffuse) and not have(emissive) and not have(alpha)
                and 'shadow' not in mat['name'].lower()):
            effect = next((key for key in map(tex_key, mat.get('paths', []))
                           if have(key) and not key.endswith(('_norm.dds', '_spec.dds'))), '')
            if effect:
                material.update(blend='additive', unlit=True)
                report['notes'].append(f'{index} {mat["name"]}: effect material drawn additive with {effect}')
                out[index] = (effect, '', material, None)
                continue
        if have(alpha) or not have(diffuse):
            colour = None
            if not have(diffuse) and not have(emissive):
                for key in ('diffuse_color', 'emissive_color'):
                    c = mat.get(key)
                    if c and any(c[:3]):
                        colour = c
                        break
                if colour is None or not have(alpha):
                    report['skipped_materials'].append(f'{index} {mat["name"]}: no texture (or colour + mask) we have')
                    continue
            source = diffuse if have(diffuse) else (emissive if have(emissive) else '')
            bake = dict(source=source, colour=colour, alpha=alpha if have(alpha) else '')
            if bake['alpha'] and blend == 'opaque':
                material['blend'] = 'cutout'
            if not have(diffuse) and blend in ('opaque', 'cutout'):
                material['blend'] = 'add_alpha' if bake['alpha'] else 'additive'
                material['unlit'] = True
            elif not have(diffuse) and not bake['alpha'] and have(emissive) and blend == 'alpha':
                material.update(blend='opaque', unlit=True, two_sided=True)
                bake['opaque'] = True
                report['notes'].append(f'{index} {mat["name"]}: emissive-only body drawn as an opaque, unlit base')
            name = f'{m3["name_key"]}_m{index}_baked.dds'
            out[index] = (name, emissive if have(diffuse) and have(emissive) else '', material, bake)
            continue
        out[index] = (diffuse, emissive if have(emissive) else '', material, None)
    return out


def bake_texture(recipe, textures_folder, target_png):
    from PIL import Image
    if recipe['source']:
        image = Image.open(textures_folder / recipe['source']).convert('RGBA')
    else:
        image = Image.new('RGBA', (256, 256), tuple(recipe['colour'][:3]) + (255,))
    if recipe['alpha']:
        mask = Image.open(textures_folder / recipe['alpha'])
        mask = (mask.getchannel('A') if mask.mode == 'RGBA' and mask.getextrema()[3][0] < 255
                else mask.convert('L')).resize(image.size)
        image.putalpha(mask)
    if recipe.get('opaque'):
        image.putalpha(255)
    if recipe.get('normalize') or recipe.get('fade'):
        import numpy as np
        pixels = np.asarray(image).astype(np.float32)
        if recipe.get('normalize'):
            seen = pixels[..., 3] > 16
            top = np.percentile(pixels[seen][:, :3].max(1), 99) if seen.any() else 255.0
            pixels[..., :3] = np.clip(pixels[..., :3] * (255.0 / max(top, 32.0)), 0, 255)
        if recipe.get('fade'):
            width = pixels.shape[1]
            along = np.interp(np.linspace(0, 1, width), *zip(*recipe['fade']))
            pixels[..., 3] *= along[None, :]
            pixels[..., :3] *= np.asarray(recipe.get('tint', (255, 255, 255)), np.float32)[None, None, :] / 255.0
        image = Image.fromarray(pixels.astype(np.uint8), 'RGBA')
    w, h = image.size
    side = lambda v: min(MAX_TEXTURE, 1 << max(2, (v - 1).bit_length()))
    image = image.resize((side(w), side(h)))
    target_png.parent.mkdir(parents=True, exist_ok=True)
    image.save(target_png)
    return target_png


EFFECT_BLENDS = {0: 'alpha', 1: 'alpha', 2: 'additive', 3: 'add_alpha', 4: 'mod', 5: 'mod2x'}


def effect_recipe(mat, textures_folder):
    have = lambda name: bool(name) and (textures_folder / name).exists()
    diffuse, emissive, alpha = tex_key(mat['diffuse']), tex_key(mat['emissive']), tex_key(mat['alpha'])
    if mat['type'] == 12:
        diffuse = next((k for k in map(tex_key, mat.get('paths', []))
                        if have(k) and not k.endswith(('_norm.dds', '_spec.dds'))), '')
    source = diffuse if have(diffuse) else (emissive if have(emissive) else '')
    if not source and not have(alpha):
        return None
    return dict(source=source, colour=(255, 255, 255, 255), alpha=alpha if have(alpha) else '', normalize=True)


def ribbon_regions(m3, scale_unused=None):
    import numpy as np
    rest = [np.linalg.inv(ib) for ib in m3['inverse_bind']]
    out = []
    for k, rb in enumerate(m3['ribbons']):
        if rb['active'] <= 0 and not rb['active_id']:
            continue
        w = rest[rb['bone']]
        axes = w[:3, :3] / np.where(np.linalg.norm(w[:3, :3], axis=0) == 0, 1, np.linalg.norm(w[:3, :3], axis=0))
        origin, forward, side = w[:3, 3], axes[:, 2], axes[:, 1]
        life = max(rb['life'], 0.05)
        reach = min(rb['speed'] * life, rb['length']) if rb['length'] > 0 else rb['speed'] * life
        widths = [max(x, 0.0) for x in rb['scale']] if isinstance(rb['scale'], tuple) else [rb['scale']] * 3
        steps = 10
        verts, tris = [], []
        for i in range(steps + 1):
            f = i / steps
            t = f * life
            centre = origin + forward * reach * f + np.array([0, 0, -0.1 * rb['gravity'] * t * t])
            mid = min(max(rb['scale_mid'], 0.01), 0.99)
            width = np.interp(f, [0, mid, 1], widths) * 0.5
            for side_sign, v in ((-1, 0.0), (1, 1.0)):
                pos = centre + side * width * side_sign
                verts.append(dict(pos=tuple(float(x) for x in pos), normal=tuple(float(x) for x in axes[:, 0]),
                                  uv=(f, v), infl=[(rb['bone'], 1.0)]))
            if i:
                a = 2 * (i - 1)
                tris += [[a, a + 1, a + 2], [a + 1, a + 3, a + 2]]
        region = dict(index=1000 + k, vertices=verts, triangles=tris, materials=[rb['material']])
        out.append((region, rb))
    return out


PRINTED_BOTH_SIDES = {'MountDollarBill'}


def print_both_sides(m3):
    import numpy as np
    for region in m3['regions']:
        top = [v for v in region['vertices'] if v['normal'][2] > 0.5]
        under = [v for v in region['vertices'] if v['normal'][2] < -0.5]
        if len(top) < 3 or not under:
            continue
        where = np.array([[v['pos'][0], v['pos'][1], 1.0] for v in top])
        fit, *_ = np.linalg.lstsq(where, np.array([v['uv'] for v in top]), rcond=None)
        middle = (min(v['pos'][1] for v in top) + max(v['pos'][1] for v in top)) / 2
        for v in under:
            u, w = np.array([v['pos'][0], 2 * middle - v['pos'][1], 1.0]) @ fit
            v['uv'] = (float(u), float(w))


def build_model(group, models_folder, textures_folder, out, previews):
    started = time.time()
    model_id = group['model_id']
    report = dict(model_id=model_id, model=group['model'], skipped_materials=[], missing_textures=[], notes=[])
    m3 = hots_m3.read(models_folder / group['model'], [models_folder / a for a in group['anims']])
    if model_id in PRINTED_BOTH_SIDES:
        print_both_sides(m3)
    m3['name_key'] = model_id.lower()
    used = sorted({mi for r in m3['regions'] for mi in r['materials']})
    mats = material_passes(m3, used, textures_folder, report)
    if not mats:
        raise ValueError('no drawable material')

    variants = []
    base_names = sorted({n for d, e, _, _ in mats.values() for n in (d, e) if n})
    baked = {d: b for d, _, _, b in mats.values() if b}
    for mount in group['mounts']:
        chosen = {n: swapped(n, mount['swaps'], base_names) for n in base_names}
        chosen = {n: (c if (textures_folder / c).exists() or n in baked else n) for n, c in chosen.items()}
        chosen.update({n: n for n in baked})
        variants.append(chosen)
    varying = [n for n in base_names if len({v[n] for v in variants}) > 1]
    if len(varying) > len(REPLACEABLE):
        report['notes'].append(f'{len(varying)} textures vary between colours; only the first 3 can (rest fixed)')
        varying = varying[:len(REPLACEABLE)]
    folder = Path('Creature') / 'HotSMounts' / model_id
    target = out / folder
    target.mkdir(parents=True, exist_ok=True)
    tex_entries, tex_index = [], {}
    for n in base_names:
        if n in varying:
            tex_index[n] = len(tex_entries)
            tex_entries.append(dict(path='', type=REPLACEABLE[varying.index(n)], flags=3))
        else:
            tex_index[n] = len(tex_entries)
            tex_entries.append(dict(path=str(folder / (n[:-4] + '.blp')).replace('/', '\\'), type=0, flags=3))

    passes = []
    regions_used = set()
    alpha_textures = set()

    effect_textures = {}

    def effect_texture(material_index, fade=None, tint=None):
        recipe = effect_recipe(m3['materials'][material_index], textures_folder)
        if recipe is None:
            return None
        if fade:
            recipe.update(fade=fade, tint=tint)
        key = json.dumps(recipe, sort_keys=True)
        if key not in effect_textures:
            name = f'{m3["name_key"]}_fx{len(effect_textures)}.dds'
            effect_textures[key] = name
            baked[name] = recipe
            alpha_textures.add(name)
            base_names.append(name)
            tex_index[name] = len(tex_entries)
            tex_entries.append(dict(path=str(folder / (name[:-4] + '.blp')).replace('/', '\\'), type=0, flags=0))
        return tex_index[effect_textures[key]]

    for region, rb in ribbon_regions(m3):
        colours = rb['colors']
        mid = min(max(rb['alpha_mid'], 0.01), 0.99)
        fade = [(0.0, colours[0][3] / 255.0), (mid, colours[1][3] / 255.0), (1.0, colours[2][3] / 255.0)]
        if max(a for _, a in fade) <= 0:
            continue
        tint = tuple(int(c) for c in colours[0][:3]) if any(colours[0][:3]) else (255, 255, 255)
        texture = effect_texture(rb['material'], fade, tint)
        if texture is None:
            report['notes'].append(f'ribbon on bone {rb["bone"]}: no texture')
            continue
        m3['regions'].append(region)
        regions_used.add(region['index'])
        mat = m3['materials'][rb['material']]
        blend = EFFECT_BLENDS.get(mat['blend'], 'additive')
        passes.append(dict(region=region['index'], texture=texture,
                           material=dict(blend=blend if blend != 'alpha' else 'add_alpha', two_sided=True,
                                         unlit=True, no_depth_write=True)))
    effect_particles = []
    for ps in m3['particles']:
        texture = effect_texture(ps['material'])
        if texture is None:
            continue
        effect_particles.append(dict(ps, texture=texture,
                                     blend=EFFECT_BLENDS.get(m3['materials'][ps['material']]['blend'], 'alpha')))
    if m3['particles'] or m3['ribbons']:
        report['notes'].append(f'effects: {len(effect_particles)}/{len(m3["particles"])} particle systems, '
                               f'{sum(1 for p in passes if p["region"] >= 1000)}/{len(m3["ribbons"])} ribbons')
    for region in m3['regions']:
        if region['index'] >= 1000:
            continue
        for mi in region['materials']:
            if mi not in mats:
                continue
            diffuse, emissive, material, _ = mats[mi]
            regions_used.add(region['index'])
            if diffuse:
                passes.append(dict(region=region['index'], texture=tex_index[diffuse], material=material))
                if material['blend'] not in ('opaque', 'additive', 'mod', 'mod2x'):
                    alpha_textures.add(diffuse)
            if emissive and material['blend'] in ('opaque', 'cutout', 'alpha'):
                passes.append(dict(region=region['index'], texture=tex_index[emissive],
                                   material=dict(blend='additive', two_sided=material['two_sided'], unlit=True,
                                                 no_depth_write=True)))
            elif emissive and not diffuse:
                passes.append(dict(region=region['index'], texture=tex_index[emissive], material=material))
    skip = [r['index'] for r in m3['regions'] if r['index'] not in regions_used]

    attachments = [[0, 'Ref_Mount'], [18, 'Ref_Center'], [28, 'Ref_Overhead']]
    names = {a['name'] for a in m3['attachments']}
    if 'Ref_Mount' not in names:
        report['notes'].append('no Ref_Mount: rider on Ref_Center')
        attachments[0][1] = 'Ref_Center' if 'Ref_Center' in names else 'Ref_Origin'
    anims = pick_animations(m3['sequences'])
    if not anims:
        report['notes'].append('no Stand/Walk sequences: still model')
    cfg = dict(name=model_id, scale=SCALE, rotation_z_degrees=90, attachments=attachments, level_attachments=[0],
               animations=anims,
               textures=tex_entries, passes=passes, skip_regions=skip, particles=effect_particles,
               **TUNING.get(model_id, {}))
    scene, info = hots_m3.to_scene(m3, cfg)
    if info['worst_rotation_error'] > 0.002:
        report['notes'].append(f'bone shear {info["worst_rotation_error"]:.4f}')
    model_path = target / f'{model_id}.m2'
    model_path.unlink(missing_ok=True)
    model_path.with_name(f'{model_id}00.skin').unlink(missing_ok=True)
    m2_model_writer.write(scene, model_path)

    needed = set(base_names) | {v[n] for v in variants for n in varying}
    for n in sorted(needed):
        blp = target / (n[:-4] + '.blp')
        blp.unlink(missing_ok=True)
        from PIL import Image
        if n in baked:
            source = bake_texture(baked[n], textures_folder, out / '_tmp' / (n[:-4] + '.png'))
        else:
            source = textures_folder / n
        with Image.open(source) as im:
            size = im.size
        if max(size) > MAX_TEXTURE:
            small = out / '_tmp' / (n[:-4] + '.png')
            small.parent.mkdir(parents=True, exist_ok=True)
            with Image.open(source) as im:
                im.convert('RGBA').resize((min(size[0], MAX_TEXTURE), min(size[1], MAX_TEXTURE)),
                                          Image.Resampling.LANCZOS).save(small)
            source = small
        alpha = n in alpha_textures or any(v.get(k) == n for v in variants for k in alpha_textures)
        write_blp(source, blp, 'alpha' if alpha else 'opaque')

    displays = []
    for mount, chosen in zip(group['mounts'], variants):
        slots = [chosen[n][:-4] for n in varying] + [''] * (3 - len(varying))
        displays.append(dict(hots_id=mount['id'], name=mount['name'], textures=slots, texture=slots[0],
                             hidden=mount['hidden'], rarity=mount['rarity'], collection=mount['collection']))
    if previews:
        markers = [('rider', (a['bone'], a['position'])) for a in scene['attachments'] if a['id'] == 0]
        stand = next((i for i, s in enumerate(scene['sequences']) if s['id'] == 0 and s.get('variation', 0) == 0), 0)
        scene_preview.render(scene, stand, 0, out / 'previews' / f'{model_id}.png', size=320, markers=markers)
    rider = [a['position'] for a in scene['attachments'] if a['id'] == 0]
    rows = dict(name=model_id, model=str(folder / f'{model_id}.m2').replace('/', '\\'), bounds=scene['bounds'],
                rider=rider[0] if rider else None, category=group['category'], displays=displays,
                animations=info['animations'], source=group['model'])
    (out / 'rows' / f'{model_id}-rows.json').write_text(json.dumps(rows, indent=1))
    report.update(seconds=round(time.time() - started, 1), displays=len(displays), textures=len(needed),
                  vertices=info['vertices'], bones=info['bones'],
                  animations=[f'{a["wow_id"]}<-{a["hots"]}' for a in info['animations']])
    return report


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('catalog', type=Path)
    parser.add_argument('--models', type=Path, required=True)
    parser.add_argument('--textures', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--only', default='')
    parser.add_argument('--previews', action='store_true')
    args = parser.parse_args()
    catalog = json.loads(args.catalog.read_text())
    groups = catalog['models']
    if args.only:
        wanted = set(args.only.split(','))
        groups = [g for g in groups if g['model_id'] in wanted]
    (args.out / 'rows').mkdir(parents=True, exist_ok=True)
    (args.out / 'previews').mkdir(parents=True, exist_ok=True)
    reports, failed = [], []
    for number, group in enumerate(groups, 1):
        try:
            report = build_model(group, args.models, args.textures, args.out, args.previews)
            reports.append(report)
            log(f'[{number}/{len(groups)}] {group["model_id"]}: {report["displays"]} colour(s), '
                f'{report["textures"]} texture(s), {report["seconds"]}s'
                + (f'  NOTE {report["notes"]}' if report['notes'] else ''))
        except Exception as error:
            failed.append(dict(model_id=group['model_id'], error=repr(error), trace=traceback.format_exc()))
            log(f'[{number}/{len(groups)}] {group["model_id"]}: FAILED {error!r}')
    (args.out / 'build-report.json').write_text(json.dumps(dict(built=reports, failed=failed), indent=1))
    log(f'done: {len(reports)} built, {len(failed)} failed')


if __name__ == '__main__':
    main()
