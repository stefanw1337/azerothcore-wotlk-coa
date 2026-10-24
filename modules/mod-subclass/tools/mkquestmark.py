"""Hang Blizzard's floating quest mark over the Two-Headed Ogre's second head while she wants a word.
    python mkquestmark.py <DBFilesClient folder> <cyclops helmet auras.sql> <quest mark.sql>
Adds one hidden aura to Spell.dbc whose state kit fastens TalkToMe.m2 to the cyclops head's helmet attachment above
the head, and writes the matching spell_dbc row."""
import re
import struct
import sys
from pathlib import Path

from mkcyclops import EFFECT_SCALES, Table, kit_row, visual_row
from mkeyes import CYCLOPS_HELMET_ATTACHMENT

QUEST_MARK = 9530120
TEMPLATE_AURA = 9531000
NAME = "A Word, Please"
MODEL = "Interface\\Buttons\\TalkToMe.m2"
ABOVE_SECOND_HEAD = (-0.085, 0.218, 2.878)
SPELL_VISUAL, SPELL_NAME = 131, 136


def write_visuals(folder):
    tables = {name: Table(Path(folder) / (name + ".dbc")) for name in
              ("SpellVisualEffectName", "SpellVisualKit", "SpellVisualKitModelAttach", "SpellVisual")}
    effects = tables["SpellVisualEffectName"]
    rows = {
        "SpellVisualEffectName": struct.pack("<3I4f", QUEST_MARK, effects.text("SubclassQuestMark"),
                                             effects.text(MODEL), *EFFECT_SCALES),
        "SpellVisualKit": kit_row(QUEST_MARK),
        "SpellVisualKitModelAttach": struct.pack("<4I6f", QUEST_MARK, QUEST_MARK, QUEST_MARK,
                                                 CYCLOPS_HELMET_ATTACHMENT, *ABOVE_SECOND_HEAD, 0, 0, 0),
        "SpellVisual": visual_row(QUEST_MARK, QUEST_MARK),
    }
    for name, row in rows.items():
        tables[name].replace(QUEST_MARK, [row])
        tables[name].save(Path(folder) / (name + ".dbc"))


def write_spell(folder):
    spells = Table(Path(folder) / "Spell.dbc")
    template = next(row for row in spells.rows if struct.unpack_from("<I", row)[0] == TEMPLATE_AURA)
    row = bytearray(template)
    struct.pack_into("<I", row, 0, QUEST_MARK)
    struct.pack_into("<I", row, SPELL_VISUAL * 4, QUEST_MARK)
    struct.pack_into("<I", row, SPELL_NAME * 4, spells.text(NAME))
    spells.replace(QUEST_MARK, [bytes(row)])
    spells.save(Path(folder) / "Spell.dbc")


def write_sql(auras_sql, out_sql):
    text = Path(auras_sql).read_text()
    start = text.index("INSERT INTO `spell_dbc` (")
    values = text.index("VALUES", start)
    columns = text[start:values]
    row = re.compile(r"\(%d, .*?\)(?=,\n\(|;)" % TEMPLATE_AURA, re.S).search(text, values).group(0)
    row = row.replace("(%d, " % TEMPLATE_AURA, "(%d, " % QUEST_MARK, 1)
    row = row.replace(", %d, " % TEMPLATE_AURA, ", %d, " % QUEST_MARK, 1)
    row = row.replace("'Cyclops Helmet'", "'%s'" % NAME, 1)
    with open(out_sql, "w") as out:
        out.write("-- mod-subclass: hidden aura that floats a quest mark over the Two-Headed Ogre's second head "
                  "while she\n-- wants a word.\n\n")
        out.write("DELETE FROM `spell_dbc` WHERE `ID` = %d;\n" % QUEST_MARK)
        out.write(columns + "VALUES\n" + row + ";\n")


if __name__ == "__main__":
    write_visuals(sys.argv[1])
    write_spell(sys.argv[1])
    write_sql(sys.argv[2], sys.argv[3])
