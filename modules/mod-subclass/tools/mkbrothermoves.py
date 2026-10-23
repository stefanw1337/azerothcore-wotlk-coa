"""Bind the class spells that leap, jump, charge or blink their caster to the script that moves the Thin Twins'
brother instead of the player when the brother casts them.
    python mkbrothermoves.py <client root> <output.sql>"""
import struct
import sys
from pathlib import Path

from mkcyclops import Table
from mkotherhead import CLASS_FAMILIES, values

SCRIPT = "spell_subclass_brother_moves"
ATTRIBUTES, ATTR0_PASSIVE = 4, 0x40
EFFECTS, TARGETS_A = 71, 86
SPELL_FAMILY = 208
OWN_SPELLS = 9000000
TARGET_CASTER = 1
MOVES_CASTER = {41, 42, 96, 149}
MOVES_TARGET = {29, 138}


def moves_caster(spell):
    return any(spell[EFFECTS + index] in MOVES_CASTER or
               (spell[EFFECTS + index] in MOVES_TARGET and spell[TARGETS_A + index] == TARGET_CASTER)
               for index in range(3))


def main(client_root, output_sql):
    spells = Table(Path(client_root) / "DBFilesClient" / "Spell.dbc")
    moving = sorted(spell[0] for spell in (values(row, spells.fields) for row in spells.rows)
                    if spell[0] < OWN_SPELLS and spell[SPELL_FAMILY] in CLASS_FAMILIES and
                    not spell[ATTRIBUTES] & ATTR0_PASSIVE and moves_caster(spell))
    with open(output_sql, "w", newline="\n") as out:
        out.write("-- mod-subclass: class spells that leap, jump, charge or blink their caster. When the Thin Twins'\n"
                  "-- brother casts one, the script moves him instead of the player.\n\n")
        out.write("DELETE FROM `spell_script_names` WHERE `ScriptName` = '%s';\n" % SCRIPT)
        out.write("INSERT INTO `spell_script_names` (`spell_id`, `ScriptName`) VALUES\n")
        out.write(",\n".join("(%d, '%s')" % (spell_id, SCRIPT) for spell_id in moving) + ";\n")
    print(len(moving), "spells move their caster")


if __name__ == "__main__":
    main(*sys.argv[1:])
