import json
import shutil
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ADDONS = HERE.parent / 'addons'
sys.path.insert(0, str(HERE))
from mkmergepatch import LISTFILE, written  # noqa: E402
from mpqpull import Archive  # noqa: E402


def lua_string(text):
    return '"' + text.replace('\\', '\\\\').replace('"', '\\"') + '"'


def write_addons(mounts, target):
    sys.path.insert(0, str(HERE))
    from install_hotsmounts import assign_ids
    mounts = json.loads(json.dumps(mounts))
    assign_ids(mounts)
    lines = ['-- Written by the HotS mounts package: spell id -> mount. Do not edit; rebuild the package instead.',
             'CompanionJournalHotSData = {']
    for mount in mounts:
        for d in mount['displays']:
            lines.append(f"  [{d['spell']}] = {{ name = {lua_string(d['name'])}, creature = {d['creature']}, "
                         f"collection = {lua_string(d.get('collection') or 'Other')}, "
                         f"rarity = {lua_string(d.get('rarity') or 'Common')}, category = {lua_string(mount['category'])} }},")
    lines.append('}')
    for name in ('CompanionJournal', 'CompanionJournal_HotS'):
        shutil.copytree(ADDONS / name, target / name)
    (target / 'CompanionJournal_HotS' / 'Data.lua').write_text('\n'.join(lines) + '\n', 'utf-8')


def main(build, package):
    build, package = Path(build), Path(package)
    if package.exists():
        shutil.rmtree(package)
    package.mkdir(parents=True)
    report = json.loads((build / 'build-report.json').read_text())
    built = {r['model_id'] for r in report['built']}
    mounts = [json.loads(p.read_text()) for p in sorted((build / 'rows').glob('*-rows.json'))]
    mounts = [m for m in mounts if m['name'] in built]
    files = {}
    for m in mounts:
        folder = build / 'Creature' / 'HotSMounts' / m['name']
        for path in sorted(folder.iterdir()):
            files[str(path.relative_to(build)).replace('/', '\\')] = path.read_bytes()
    listing = '\r\n'.join(sorted(files)).encode('latin1')
    entries = sorted(list(files.items()) + [(LISTFILE, listing)], key=lambda e: e[0].lower())
    print(f'{len(mounts)} models, {sum(len(m["displays"]) for m in mounts)} mounts, {len(files)} files, '
          f'{sum(len(d) for d in files.values()) / 2**20:.0f} MB before packing', flush=True)
    blob = written(entries)
    mpq = package / 'patch-ZZZZZZ-HOTS.MPQ'
    mpq.write_bytes(blob)
    archive = Archive(str(mpq))
    try:
        for name, data in entries:
            if archive.extract(name) != data:
                raise SystemExit(f'{name} reads back wrong')
    finally:
        archive.close()
    print(f'MPQ {len(blob) / 2**20:.0f} MB, every file read back intact', flush=True)
    (package / 'mounts.json').write_text(json.dumps(mounts, indent=1))
    write_addons(mounts, package / 'AddOns')
    print(f'package: {package}')


if __name__ == '__main__':
    main(*sys.argv[1:])
