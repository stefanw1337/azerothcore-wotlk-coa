"""Let the Thin Twins' brother wear the visuals of the forms his twin keeps his own look in: a plain copy of each
class form spell without its spell visual stands in for the original on the player, and a visual copy, a dummy aura
with the original visual, name and icon, flashes and lingers on the brother. Writes both copies into Spell.dbc and
the SQL with the server's rows for the visual copies and the mapping.
    python mktwinforms.py <client root> <mapping.sql>"""
import struct
import sys
from pathlib import Path

from mkcyclops import Table
from mkotherhead import CLASS_FAMILIES, packed, values

FIRST_SPELL = 9800000
LAST_ID = 9820000
ATTRIBUTES, ATTR0_PASSIVE = 4, 0x40
CASTING_TIME, DURATION, RANGE, EQUIPPED_ITEM_CLASS = 28, 40, 46, 68
EFFECT, TARGET_A, EFFECT_AURA = 71, 86, 95
SPELL_VISUALS = (131, 132)
ICON, NAME = 133, 136
SPELL_FAMILY, SCHOOL_MASK = 208, 225
DAMAGE_MULTIPLIERS, BONUS_MULTIPLIERS = range(216, 219), range(229, 232)
FORM_AURAS = {36, 56}
INSTANT, FOREVER, SELF, NO_ITEM = 1, 21, 1, -1
APPLY_AURA, TARGET_CASTER, DUMMY_AURA = 6, 1, 4
ONE = struct.unpack("<i", struct.pack("<f", 1.0))[0]


def is_form(spell):
    return spell[SPELL_FAMILY] in CLASS_FAMILIES and not spell[ATTRIBUTES] & ATTR0_PASSIVE and \
        any(spell[EFFECT_AURA + index] in FORM_AURAS and spell[TARGET_A + index] == TARGET_CASTER
            for index in range(3)) and any(spell[visual] for visual in SPELL_VISUALS)


def plain_copy(spell, spell_id):
    copy = list(spell)
    copy[0] = spell_id
    for visual in SPELL_VISUALS:
        copy[visual] = 0
    return copy


def visual_copy(spell, spell_id):
    copy = [0] * len(spell)
    copy[0] = spell_id
    copy[CASTING_TIME], copy[DURATION], copy[RANGE] = INSTANT, FOREVER, SELF
    copy[EQUIPPED_ITEM_CLASS] = NO_ITEM
    copy[EFFECT], copy[TARGET_A], copy[EFFECT_AURA] = APPLY_AURA, TARGET_CASTER, DUMMY_AURA
    for visual in SPELL_VISUALS:
        copy[visual] = spell[visual]
    copy[ICON] = spell[ICON]
    copy[NAME:NAME + 17] = spell[NAME:NAME + 17]
    copy[SCHOOL_MASK] = spell[SCHOOL_MASK] or 1
    for field in (*DAMAGE_MULTIPLIERS, *BONUS_MULTIPLIERS):
        copy[field] = ONE
    return copy


def text(table, offset):
    return table.strings[offset:table.strings.index(b"\0", offset)].decode("utf-8")


def quoted(value):
    return "'" + value.replace("\\", "\\\\").replace("'", "''") + "'"


def main(client_root, mapping_sql):
    folder = Path(client_root) / "DBFilesClient"
    spells = Table(folder / "Spell.dbc")
    spells.rows = [row for row in spells.rows if not FIRST_SPELL <= struct.unpack_from("<I", row)[0] < LAST_ID]
    forms = [values(row, spells.fields) for row in spells.rows]
    forms = [spell for spell in forms if is_form(spell)]
    mapping, server_rows = [], []
    for index, spell in enumerate(forms):
        plain = plain_copy(spell, FIRST_SPELL + 2 * index)
        visual = visual_copy(spell, plain[0] + 1)
        assert visual[0] < LAST_ID
        spells.rows += [packed(plain), packed(visual)]
        mapping.append((spell[0], plain[0], visual[0]))
        server_rows.append("(%d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d,\n    %s)" % (
            visual[0], INSTANT, FOREVER, SELF, NO_ITEM, APPLY_AURA, TARGET_CASTER, DUMMY_AURA,
            visual[SPELL_VISUALS[0]], visual[SPELL_VISUALS[1]], visual[ICON], visual[SCHOOL_MASK],
            quoted(text(spells, visual[NAME]))))
    spells.save(folder / "Spell.dbc")
    with open(mapping_sql, "w", newline="\n") as out:
        out.write("-- mod-subclass: the Thin Twins' brother shows the visuals of the forms his twin keeps his own\n"
                  "-- look in. Each class form has a plain copy without its visual that the player is shown with,\n"
                  "-- and a visual copy the brother wears as a dummy aura with the form's visual, name and icon.\n\n")
        out.write("DROP TABLE IF EXISTS `mod_subclass_twin_form_spell`;\n")
        out.write("CREATE TABLE `mod_subclass_twin_form_spell` (\n    `SpellId` INT UNSIGNED NOT NULL,\n"
                  "    `PlainId` INT UNSIGNED NOT NULL,\n    `VisualId` INT UNSIGNED NOT NULL,\n"
                  "    PRIMARY KEY (`SpellId`)\n) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;\n")
        out.write("DELETE FROM `mod_subclass_twin_form_spell`;\n")
        out.write("INSERT INTO `mod_subclass_twin_form_spell` (`SpellId`, `PlainId`, `VisualId`) VALUES\n")
        out.write(",\n".join("(%d, %d, %d)" % row for row in mapping) + ";\n\n")
        out.write("DELETE FROM `spell_dbc` WHERE `ID` BETWEEN %d AND %d;\n" % (FIRST_SPELL, LAST_ID - 1))
        out.write("INSERT INTO `spell_dbc` (`ID`, `CastingTimeIndex`, `DurationIndex`, `RangeIndex`, "
                  "`EquippedItemClass`, `Effect_1`,\n    `ImplicitTargetA_1`, `EffectAura_1`, `SpellVisualID_1`, "
                  "`SpellVisualID_2`, `SpellIconID`, `SchoolMask`,\n    `Name_Lang_enUS`) VALUES\n")
        out.write(",\n".join(server_rows) + ";\n")
    print(len(mapping), "forms with a plain and a visual copy")


if __name__ == "__main__":
    main(*sys.argv[1:])
