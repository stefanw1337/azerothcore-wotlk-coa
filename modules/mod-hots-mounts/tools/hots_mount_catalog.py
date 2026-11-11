import json
import re
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

RIDEABLE = {'Ride', 'Ridebeast', 'Ridebike', 'Ridesurf', 'Ridenone'}


def asset_key(path):
    path = path.replace('\\', '/').lower()
    marker = 'assets/units/mounts/'
    return path[path.index(marker) + len(marker):] if marker in path else path


def texture_key(path_or_id):
    name = path_or_id.replace('\\', '/').split('/')[-1].lower()
    return name if name.endswith('.dds') else name + '.dds'


def load(folder):
    models, mounts, macros, textures = {}, {}, {}, {}
    files = sorted((folder / 'mountdata').rglob('*.xml')) + [folder / 'gamedata' / 'modeldata.xml',
                                                              folder / 'gamedata' / 'mountdata.xml']
    for path in files:
        if not path.exists():
            continue
        root = ET.parse(path).getroot()
        for el in root:
            ident = el.get('id')
            if el.tag == 'CModel' and ident:
                entry = models.setdefault(ident, dict(id=ident, parent=el.get('parent'), anims=[]))
                for child in el:
                    if child.tag == 'Model':
                        entry['model'] = child.get('value')
                    elif child.tag == 'RequiredAnims':
                        entry['anims'].append(child.get('value'))
                    elif child.tag == 'RequiredAnimsEx' and child.get('FilePath'):
                        entry['anims'].append(child.get('FilePath'))
                    elif child.tag == 'TextureDeclares' and child.get('Prefix'):
                        entry['prefix'] = child.get('Prefix')
            elif el.tag == 'CMount' and ident:
                entry = mounts.setdefault(ident, dict(id=ident, parent=el.get('parent'), abstract=el.get('default') == '1',
                                                      variations=[], macros=[], flags={}))
                for child in el:
                    value = child.get('value')
                    if child.tag in ('Model', 'MountCategory', 'HyperlinkId', 'CollectionCategory', 'Rarity',
                                     'Universe', 'AttributeId'):
                        entry[child.tag] = value
                    elif child.tag == 'VariationArray':
                        entry['variations'].append(value)
                    elif child.tag == 'CreationActorMessageArray':
                        send = child.get('Send', '')
                        if send.startswith('EventMacroRun '):
                            entry['macros'].append(send.split(' ', 1)[1])
                    elif child.tag == 'Flags':
                        entry['flags'][child.get('index')] = child.get('value')
            elif el.tag == 'CActorEventMacroRunnable' and ident:
                macros[ident] = [d.get('Send', '') for d in el if d.tag == 'Do']
            elif el.tag == 'CTexture' and ident:
                file = next((c.get('value') for c in el if c.tag == 'File'), None)
                textures[ident.lower()] = texture_key(file) if file else texture_key(ident)
    names = {}
    strings = folder / 'gamestrings.txt'
    if strings.exists():
        for line in strings.read_text('utf-8', errors='replace').splitlines():
            if line.startswith('Mount/Name/') and '=' in line:
                key, value = line[len('Mount/Name/'):].split('=', 1)
                names[key] = value.strip()
    return models, mounts, macros, textures, names


def resolved(table, ident, field, seen=None):
    seen = seen or set()
    entry = table.get(ident)
    if entry is None or ident in seen:
        return None
    if entry.get(field):
        return entry[field]
    seen.add(ident)
    return resolved(table, entry.get('parent'), field, seen) if entry.get('parent') else None


def catalog(folder):
    models, mounts, macros, textures, names = load(folder)
    out = {}
    skipped = []
    for ident, mount in sorted(mounts.items()):
        if mount['abstract'] or ident == 'Random':
            continue
        category = resolved(mounts, ident, 'MountCategory') or 'Ride'
        model_id = resolved(mounts, ident, 'Model')
        flags = mount['flags']
        if category not in RIDEABLE or flags.get('IsMorphMount') == '1':
            skipped.append((ident, f'category {category}'))
            continue
        model_path = resolved(models, model_id, 'model') if model_id else None
        if not model_path:
            skipped.append((ident, f'no model ({model_id})'))
            continue
        anims = []
        cursor, seen = model_id, set()
        while cursor and cursor in models and cursor not in seen:
            seen.add(cursor)
            anims += [a for a in models[cursor]['anims'] if a not in anims]
            cursor = models[cursor].get('parent')
        swaps = []
        for macro in mount['macros']:
            for send in macros.get(macro, []):
                if send.startswith('TextureSelectById '):
                    tex = send.split(' ', 1)[1].strip()
                    swaps.append(textures.get(tex.lower(), texture_key(tex)))
        group = out.setdefault(model_id, dict(model_id=model_id, model=asset_key(model_path),
                                              anims=[asset_key(a) for a in anims], category=category,
                                              prefix=resolved(models, model_id, 'prefix'), mounts=[]))
        group['mounts'].append(dict(id=ident, name=names.get(ident, ident), swaps=swaps,
                                    hidden=flags.get('Hidden') == '1', rarity=resolved(mounts, ident, 'Rarity'),
                                    collection=resolved(mounts, ident, 'CollectionCategory')))
    return dict(models=list(out.values()), skipped=skipped)


def main():
    folder, target = Path(sys.argv[1]), Path(sys.argv[2])
    result = catalog(folder)
    target.write_text(json.dumps(result, indent=1))
    mounts = sum(len(m['mounts']) for m in result['models'])
    print(f'{len(result["models"])} models, {mounts} mounts, {len(result["skipped"])} skipped -> {target}')


if __name__ == '__main__':
    main()
