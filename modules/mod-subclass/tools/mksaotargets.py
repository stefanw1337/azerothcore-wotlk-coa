import struct
import sys
from pathlib import Path

SPELL_ID, SPELL_ATTRIBUTES, SPELL_NAME = 0, 4, 136
SPELL_EFFECTS, SPELL_TARGETS, SPELL_MISC = range(71, 74), range(86, 89), range(110, 113)
PASSIVE = 0x40
INTERRUPT_CAST, DISPEL, STEAL_BENEFICIAL_BUFF = 68, 38, 126
MAGIC = 1
ENEMY_TARGETS = (6, 25)
SKILL_ABILITY_SPELL = 2
ADVANCEMENT_RANK_SPELLS = range(5, 10)
BOOK_COPY_OFFSET = 1100000


def table(path):
    data = Path(path).read_bytes()
    count, fields, size, _ = struct.unpack_from("<4I", data, 4)
    rows = [struct.unpack_from(f"<{fields}I", data, 20 + index * size) for index in range(count)]
    return rows, data[20 + count * size:]


def text(strings, offset):
    return strings[offset:strings.index(b"\0", offset)].decode("utf-8", "replace")


def player_spells(folder, spells):
    known = {row[SKILL_ABILITY_SPELL] for row in table(folder / "SkillLineAbility.dbc")[0]}
    for row in table(folder / "CharacterAdvancement.dbc")[0]:
        known.update(spell for spell in (row[field] for field in ADVANCEMENT_RANK_SPELLS) if spell in spells)
    known.update(spell + BOOK_COPY_OFFSET for spell in list(known) if spell + BOOK_COPY_OFFSET in spells)
    return known


def kinds(row):
    found = set()
    for effect, target, misc in zip((row[i] for i in SPELL_EFFECTS), (row[i] for i in SPELL_TARGETS),
                                    (row[i] for i in SPELL_MISC)):
        if target not in ENEMY_TARGETS:
            continue
        if effect == INTERRUPT_CAST:
            found.add("interrupt")
        elif (effect == DISPEL and misc == MAGIC) or effect == STEAL_BENEFICIAL_BUFF:
            found.add("purge")
    return found


def lua_list(values):
    return "{" + ",".join(values) + "}"


def main(dbc_folder, output):
    folder = Path(dbc_folder)
    rows, strings = table(folder / "Spell.dbc")
    spells = {row[SPELL_ID]: row for row in rows}
    groups = {"interrupt": {}, "purge": {}}
    for spell in sorted(player_spells(folder, spells)):
        row = spells.get(spell)
        if not row or row[SPELL_ATTRIBUTES] & PASSIVE:
            continue
        for kind in kinds(row):
            groups[kind].setdefault(text(strings, row[SPELL_NAME]), []).append(spell)
    lines = ["local AddonName,SAO=...", "SAO.TargetWatchSpells={"]
    for kind, named in groups.items():
        names = [f'"{name}"' for name in sorted(named)]
        ids = [str(spell) for name in sorted(named) for spell in named[name]]
        lines.append(f"{kind}={{names={lua_list(names)},ids={lua_list(ids)}}},")
    lines.append("}")
    Path(output).write_text("\n".join(lines) + "\n", encoding="utf-8")
    for kind, named in groups.items():
        print(kind, len(named), "names", sum(len(ids) for ids in named.values()), "spells")


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
