import struct
import sys
from pathlib import Path

from mpqpull import Archive

CLASSES = {1: "Warrior", 2: "Paladin", 3: "Hunter", 4: "Rogue", 5: "Priest", 6: "Death Knight", 7: "Shaman",
           8: "Mage", 9: "Warlock", 11: "Druid"}
CLASS_LINES = {1: (26, 256, 257), 2: (594, 267, 184), 3: (50, 163, 51), 4: (253, 38, 39, 633, 40),
               5: (613, 56, 78), 6: (770, 771, 772, 776), 7: (375, 373, 374), 8: (237, 8, 6),
               9: (355, 354, 593), 11: (574, 134, 573)}
SPELL_NAME = 136
SPELL_RANK_FIRST, SPELL_RANK_SPELL = 1, 2
TALENT_TAB, TALENT_RANKS = 1, range(4, 13)
TAB_CLASSES, TAB_PETS = 20, 21
LINE_SKILL, LINE_SPELL = 1, 2
ADVANCEMENT_ID, ADVANCEMENT_SPELLS, ADVANCEMENT_CLASS_TYPE, ADVANCEMENT_NAME = 0, range(5, 10), 32, 47
CLASS_TYPE_CLASS, CLASS_TYPE_COA = 2, 4
REBORN_SKILL_OFFSET = 11000
REBORN_SPELL_OFFSET = 1100000


class Table:
    def __init__(self, data):
        _, count, _, size, _ = struct.unpack_from("<4s4I", data)
        self.rows = [data[20 + index * size:20 + (index + 1) * size] for index in range(count)]
        self.strings = data[20 + count * size:]

    @staticmethod
    def value(row, field):
        return struct.unpack_from("<I", row, field * 4)[0]

    def text(self, row, field):
        start = self.value(row, field)
        return self.strings[start:self.strings.index(b"\0", start)].decode("utf-8", "replace")

    def names(self):
        return {self.value(row, 0): self.text(row, SPELL_NAME) for row in self.rows}


def plain(name):
    return name.lower().replace("'", "").strip()


def wotlk_names(server, stock_spells, stock_lines):
    names = {classId: set() for classId in CLASSES}
    tabs, talents = server("TalentTab"), server("Talent")
    for tab in tabs.rows:
        for classId in CLASSES:
            if Table.value(tab, TAB_PETS) or not Table.value(tab, TAB_CLASSES) & (1 << (classId - 1)):
                continue
            for talent in talents.rows:
                if Table.value(talent, TALENT_TAB) == Table.value(tab, 0):
                    names[classId].update(plain(stock_spells[spell]) for spell in
                                          (Table.value(talent, field) for field in TALENT_RANKS)
                                          if spell in stock_spells)
    for row in stock_lines.rows:
        spell = Table.value(row, LINE_SPELL)
        for classId, lines in CLASS_LINES.items():
            if Table.value(row, LINE_SKILL) in lines and spell in stock_spells:
                names[classId].add(plain(stock_spells[spell]))
    return names


def main(server_dbc, locale_mpq):
    folder = Path(server_dbc)

    def server(name):
        return Table((folder / (name + ".dbc")).read_bytes())

    archive = Archive(locale_mpq)
    stock_spells = Table(archive.extract("DBFilesClient\\Spell.dbc")).names()
    stock_lines = Table(archive.extract("DBFilesClient\\SkillLineAbility.dbc"))
    names = wotlk_names(server, stock_spells, stock_lines)
    spells = server("Spell").names()
    types, advancement = server("CharacterAdvancementClassTypes"), server("CharacterAdvancement")
    reborn = {Table.value(row, 0): Table.value(row, CLASS_TYPE_CLASS) for row in types.rows
              if not Table.value(row, CLASS_TYPE_COA) and Table.value(row, CLASS_TYPE_CLASS) in CLASSES}
    entries, catalog = [], set()
    for row in advancement.rows:
        classId = reborn.get(Table.value(row, ADVANCEMENT_CLASS_TYPE))
        if not classId:
            continue
        ranks = [Table.value(row, field) for field in ADVANCEMENT_SPELLS if Table.value(row, field)]
        catalog.update(ranks)
        if not any(plain(spells.get(spell, "")) in names[classId] or spell in stock_spells for spell in ranks):
            entries.append((classId, Table.value(row, ADVANCEMENT_ID), advancement.text(row, ADVANCEMENT_NAME)))
    for classId, entryId, name in sorted(entries):
        print(f"entry {entryId:>6}  {CLASSES[classId]:<12} {name}")
    lines = {}
    for row in server("SkillLineAbility").rows:
        lines.setdefault(Table.value(row, LINE_SPELL), set()).add(Table.value(row, LINE_SKILL))
    trainable = {Table.value(row, 1) for row in server("NPCTrainer").rows}
    firsts = {Table.value(row, SPELL_RANK_SPELL): Table.value(row, SPELL_RANK_FIRST)
              for row in server("SpellRank").rows}
    extras = {}
    for spell in sorted(trainable):
        if spell < REBORN_SPELL_OFFSET or spell in catalog:
            continue
        for classId in CLASSES:
            reborn_lines = {REBORN_SKILL_OFFSET + line for line in CLASS_LINES[classId]}
            if lines.get(spell, set()) & reborn_lines and plain(spells.get(spell, "")) not in names[classId]:
                first = firsts.get(spell, spell)
                shown = first if spells.get(first) == spells.get(spell) else spell
                extras.setdefault((classId, spells.get(spell, "?").strip()), set()).add(shown)
    for (classId, name), first in sorted(extras.items()):
        print(f"trainer {CLASSES[classId]:<12} {name}: {', '.join(map(str, sorted(first)))}")


if __name__ == "__main__":
    main(*sys.argv[1:])
