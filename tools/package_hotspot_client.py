import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[1]
TOC = 'Interface/FrameXML/FrameXML.toc'
LUA = 'Interface/FrameXML/XPHotspots.lua'


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def extend_toc(data):
    text = data.decode('utf-8-sig')
    matches = [line for line in text.splitlines() if line.strip().lower() == 'xphotspots.lua']
    if len(matches) > 1:
        raise ValueError('Duplicate hotspot loader entries in the baseline')
    if matches:
        return data
    newline = b'\r\n' if b'\r\n' in data else b'\n'
    return data + (b'' if data.endswith(b'\n') else newline) + b'XPHotspots.lua' + newline


def package(baseline, output, executable):
    baseline, output = Path(baseline).resolve(), Path(output).resolve()
    executable = str(Path(executable).resolve())
    if baseline == output or output.exists():
        raise ValueError('Output must be a new file, separate from the installed client')
    if not baseline.is_file():
        raise ValueError('Baseline archive does not exist')
    output.parent.mkdir(parents=True, exist_ok=True)
    original_hash = sha(baseline)

    def run(*args):
        result = subprocess.run([executable, *map(str, args)], capture_output=True)
        if result.returncode:
            raise RuntimeError(result.stderr.decode('utf-8', 'replace') or 'mpqcli operation failed')
        return result.stdout

    def inventory(directory):
        return {str(p.relative_to(directory)).replace('\\', '/').lower(): sha(p)
                for p in directory.rglob('*') if p.is_file() and not p.name.startswith('(')}

    with tempfile.TemporaryDirectory(prefix='hotspot-package-', dir=output.parent) as temporary:
        temp = Path(temporary)
        before, after = temp / 'before', temp / 'after'
        run('extract', '--keep', '--output', before, baseline)
        toc = next((p for p in before.rglob('*') if p.is_file() and
                    str(p.relative_to(before)).replace('\\', '/').lower() == TOC.lower()), None)
        if toc is None:
            raise ValueError('Baseline must contain the effective FrameXML.toc')
        wanted = inventory(before)
        payload = temp / 'payload'
        payload.mkdir()
        new_toc = payload / 'FrameXML.toc'
        new_toc.write_bytes(extend_toc(toc.read_bytes()))
        candidate = temp / 'candidate.MPQ'
        shutil.copyfile(baseline, candidate)
        for member, source in ((TOC, new_toc), (LUA, ROOT / 'client' / LUA)):
            run('add', '--overwrite', '--game', 'wow-wotlk', '--path', member.replace('/', '\\'),
                candidate, source)
            wanted[member.lower()] = sha(source)
        run('extract', '--keep', '--output', after, candidate)
        actual = inventory(after)
        if actual != wanted:
            raise RuntimeError('Archive readback differed from the expected complete member inventory')
        if sha(baseline) != original_hash:
            raise RuntimeError('Baseline changed while building; discard this candidate and retry')
        report = dict(baseline_sha256=original_hash, candidate_sha256=sha(candidate),
                      mpqcli_sha256=sha(executable), members_checked=len(actual),
                      changed_members=[TOC, LUA], state='built; not installed or published')
        shutil.copyfile(candidate, output)
    output.with_suffix(output.suffix + '.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    return report


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--baseline', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--mpqcli', type=Path, required=True)
    args = parser.parse_args()
    print(json.dumps(package(args.baseline, args.output, args.mpqcli), indent=2))


if __name__ == '__main__':
    main()
