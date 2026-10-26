import ast
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
FORBIDDEN = {'.mpq', '.dbc', '.wtf', '.wdb', '.pdb', '.dmp', '.pfx', '.pem'}
PATTERN = re.compile(rb'(?:github_pat_|gh[pousr]_|sk-proj-)[A-Za-z0-9_]{20,}')
ACCOUNT = re.compile(rb'(?i)WTF[/\\]+Account[/\\]+[^/\\\s"\x27]+@')
SYNTAX_EXCEPTIONS = {
    '.devcontainer/devcontainer.json': 'Upstream JSON-with-comments configuration',
    '.vscode/extensions.json': 'Upstream JSON-with-comments configuration',
    '.vscode/settings.json': 'Upstream JSON-with-comments configuration',
    'deps/libmpq/bindings/python/mpq.py': 'Preserved upstream Python 2 binding',
}


SHIPPED_DBC_DIR = 'modules/mod-coa-custom-races/data/dbc/'
SHIPPED_DBC_HASHES = {
    "Achievement.dbc": "dc9bcdb7feca2fe8c1fc4b0b6861e6a246c6d4861c3a032b9c6c64636a265e6f",
    "Achievement_Criteria.dbc": "855c80496f57aa6f3540f9d971351d436f8f2700b57171e6ca9fd419a77464f9",
    "CharBaseInfo.dbc": "3c60552274ee4ac74b5c38b90102c5d5f9b5566726dc9af82841575d640f577f",
    "CharHairGeosets.dbc": "0457d0ea4ff8cb99bcb1e6283e8aaed04e96edcd3695ccc405fae977688a7b33",
    "CharSections.dbc": "5683260b25ba59221a57ea914739a7125a8844574b8bf992f525ada7a932803d",
    "CharStartOutfit.dbc": "255722968fbc21e60f8711675ed1b68e6ea45c894161654d40da89fd2c5caae0",
    "CharacterCreationArchetypeDetails.dbc": "9e8f6a27f39b1e1045f2814aac03000d4a7aceb1368e2fb64aaf1bbb052dd8c2",
    "CharacterCreationClassDetails.dbc": "31642ca622c301f2240f00726d35a832bd6cb39da8f146a66c9b41695fccdb03",
    "CharacterCreationPetDetails.dbc": "fbca248ed9654c100f0ae072e862f47b60ff969de1f414865ce94e17456c205b",
    "CharacterCreationShapeshiftDetails.dbc": "d1d906c1711e83edb77f2a79027c8f650d588b350015152cbc431d7e8d28e2d8",
    "CharacterFacialHairStyles.dbc": "0ae4ef9a840fb42cb46df35f34ca4545c37b0c35b2c71512f9140178953ca655",
    "ChrRaces.dbc": "0f9e05112d56609a87f0b1900be88f8130a8fcb24ee4acae585f1fd8d9144bf0",
    "CreatureDisplayInfo.dbc": "a27c139f3cf869c1986c93b28f3b61901ed8858852fa169b211c15f073989b98",
    "CreatureDisplayInfoGeosetData.dbc": "a5864d2505b150d745037dc3f2a85db84e9027238500665bcb7a36004bf7baf0",
    "CreatureModelData.dbc": "9b7cf9a599f6e13c5628f895c813d7746994a05ffb53e34429fb9b3fba67b91a",
    "EmotesTextSound.dbc": "de809a1fa057e3897720e57727732cb66ca13981ad62d273479c880a4bdeb9b5",
    "Faction.dbc": "31c53fe7efa27484c7cbcfe818c7237b90dcf7a5de9e0336fddf148678bb9c7c",
    "HDCharSections.dbc": "92ec4f9d0e2cbf33a2ca99128e05a2bdf560d8fcedd3417d21a2022235244be6",
    "NameGen.dbc": "3ff4d7065ea4a0bd994a8210d5df39e76285ebea83d65089fcd77d66c1930d46",
    "NamesProfanity.dbc": "95907fe85af598465a785437fd8c5d4a2441e18c7109bfa2112d98b7faa23a0b",
    "NamesReserved.dbc": "95907fe85af598465a785437fd8c5d4a2441e18c7109bfa2112d98b7faa23a0b",
    "SkillLineAbility.dbc": "42f8f25bb8ed382ce11db560458fa303b9de80c093e0c98a8be8f1f9a80ce724",
    "SkillRaceClassInfo.dbc": "75fb54625ce0e54940e7029d77e5927eb3f25c842da20649933503328f5aa9dd",
    "Talent.dbc": "b11369fc16b208507ef75f9201fac939fca090380c2eb7a3f5d0cca70500a7a1",
    "UICameraAppearanceChrRaces.dbc": "0975a652ac77a32225239a6d7fa78716bc822200817299600dea24bf9af4782b"
}


def is_shipped_dbc(name, raw):
    if not name.startswith(SHIPPED_DBC_DIR):
        return False
    return SHIPPED_DBC_HASHES.get(name.removeprefix(SHIPPED_DBC_DIR)) == hashlib.sha256(raw).hexdigest()


def main():
    git = subprocess.run(['git', 'ls-files', '--cached', '--others', '--exclude-standard', '-z'], cwd=ROOT, capture_output=True)
    if git.returncode == 0:
        paths = [ROOT / p.decode('utf-8') for p in git.stdout.split(b'\0') if p]
    else:
        paths = [p for p in ROOT.rglob('*') if p.is_file() and not {'.git', '.local', '__pycache__'} & set(p.relative_to(ROOT).parts)]
    issues = []
    checked = 0
    for path in paths:
        name = path.relative_to(ROOT).as_posix()
        if path.is_symlink():
            raw = os.readlink(path).encode('utf-8')
            target = path.resolve()
            if not target.is_relative_to(ROOT) or not target.exists():
                issues.append((name, 'broken or external symbolic link'))
        elif not path.is_file():
            issues.append((name, 'tracked file missing'))
            continue
        else:
            raw = path.read_bytes()
        if (path.suffix.lower() in FORBIDDEN and not is_shipped_dbc(name, raw)) or path.name == '.env' or '/WTF/' in '/' + name:
            issues.append((name, 'private/generated artifact'))
        if PATTERN.search(raw):
            issues.append((name, 'credential pattern'))
        if ACCOUNT.search(raw):
            issues.append((name, 'personal account path'))
        if path.suffix == '.ps1':
            lines = raw.decode('utf-8-sig').splitlines()
            for index, line in enumerate(lines):
                if re.search(r'(?i)password', line):
                    for follow in lines[index + 1:index + 4]:
                        if re.search(r'\.WriteLine\(\s*[\x27\x22][^\x27\x22]+[\x27\x22]\s*\)', follow):
                            issues.append((name, 'literal administrator credential after password prompt'))
                            break
        if path.suffix == '.py' and name not in SYNTAX_EXCEPTIONS:
            try:
                ast.parse(raw.decode('utf-8-sig'), filename=name)
            except (SyntaxError, UnicodeError) as error:
                issues.append((name, 'Python syntax: ' + type(error).__name__))
        if path.suffix == '.json' and name not in SYNTAX_EXCEPTIONS:
            try:
                json.loads(raw.decode('utf-8-sig'))
            except (ValueError, UnicodeError):
                issues.append((name, 'invalid JSON'))
        checked += 1
    print(json.dumps({'checkedFiles': checked, 'issues': issues,
                      'reviewedShippedDbc': [SHIPPED_DBC_DIR + name for name in sorted(SHIPPED_DBC_HASHES)],
                      'reviewedSyntaxExceptions': {name: reason for name, reason in SYNTAX_EXCEPTIONS.items() if (ROOT / name).is_file()},
                      'scope': 'tracked source artifact/credential patterns and Python/JSON syntax; not a complete secret/license audit'}, indent=2))
    return bool(issues)


if __name__ == '__main__':
    sys.exit(main())
