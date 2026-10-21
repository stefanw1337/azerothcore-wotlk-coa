import struct
import sys
from pathlib import Path

from mkcyclops import Table

FIRST_SPELL = 9700000
FIRST_VISUAL = 9700000
FIRST_KIT = 9700000
LAST_ID = 9800000
CLASS_FAMILIES = {3, 4, 5, 6, 7, 8, 9, 10, 11, 15}
SPELL_VISUAL, SPELL_FAMILY, DAMAGE_CLASS = 131, 208, 213
MELEE = 2
VISUAL_ARM_KITS = (1, 2, 6)
KIT_START_ANIMATION, KIT_ANIMATION = 1, 2
ARM_ANIMATIONS = {16, 17, 18, 19, 57, 58, 31, 32, 33, 51, 52, 53, 54, 107, 108, 124, 125, 126}
OFF_HAND_SWING = 87
NO_ANIMATION = -1


def values(row, count):
    return list(struct.unpack(f"<{count}i", row))


def packed(fields):
    return struct.pack(f"<{len(fields)}i", *fields)


def moves_an_arm(kit):
    return kit[KIT_START_ANIMATION] in ARM_ANIMATIONS or kit[KIT_ANIMATION] in ARM_ANIMATIONS


def main(client_root, mapping_sql):
    folder = Path(client_root) / "DBFilesClient"
    tables = {name: Table(folder / (name + ".dbc")) for name in ("Spell", "SpellVisual", "SpellVisualKit")}
    for table in tables.values():
        table.rows = [row for row in table.rows if not FIRST_SPELL <= struct.unpack_from("<I", row)[0] < LAST_ID]
    spells, visuals, kits = tables["Spell"], tables["SpellVisual"], tables["SpellVisualKit"]
    visual_rows = {struct.unpack_from("<I", row)[0]: values(row, visuals.fields) for row in visuals.rows}
    kit_rows = {struct.unpack_from("<I", row)[0]: values(row, kits.fields) for row in kits.rows}
    new_kits, new_visuals, new_spells, mapping = {}, {}, [], []
    for row in list(spells.rows):
        spell = values(row, spells.fields)
        visual = visual_rows.get(spell[SPELL_VISUAL])
        if spell[SPELL_FAMILY] not in CLASS_FAMILIES or not visual:
            continue
        arm_kits = [visual[slot] for slot in VISUAL_ARM_KITS if visual[slot] in kit_rows and
                    moves_an_arm(kit_rows[visual[slot]])]
        if not arm_kits:
            continue
        swing = spell[DAMAGE_CLASS] == MELEE
        key = (spell[SPELL_VISUAL], swing)
        if key not in new_visuals:
            clone = list(visual)
            clone[0] = FIRST_VISUAL + len(new_visuals)
            for slot in VISUAL_ARM_KITS:
                original = visual[slot]
                if original not in arm_kits:
                    continue
                kit_key = (original, swing)
                if kit_key not in new_kits:
                    kit = list(kit_rows[original])
                    kit[0] = FIRST_KIT + len(new_kits)
                    kit[KIT_START_ANIMATION] = NO_ANIMATION
                    kit[KIT_ANIMATION] = OFF_HAND_SWING if swing else NO_ANIMATION
                    new_kits[kit_key] = kit
                clone[slot] = new_kits[kit_key][0]
            new_visuals[key] = clone
        copy = list(spell)
        copy[0] = FIRST_SPELL + len(new_spells)
        copy[SPELL_VISUAL] = new_visuals[key][0]
        new_spells.append(packed(copy))
        mapping.append((spell[0], copy[0]))
    spells.rows += new_spells
    visuals.rows += [packed(row) for row in new_visuals.values()]
    kits.rows += [packed(row) for row in new_kits.values()]
    for name, table in tables.items():
        table.save(folder / (name + ".dbc"))
    with open(mapping_sql, "w") as out:
        out.write("-- mod-subclass: copies of class spells whose animation leaves the casting arm alone, sent when the "
                  "other head casts them.\n\n")
        out.write("DROP TABLE IF EXISTS `mod_subclass_other_head_spell`;\n")
        out.write("CREATE TABLE `mod_subclass_other_head_spell` (\n    `SpellId` INT UNSIGNED NOT NULL,\n"
                  "    `ShownId` INT UNSIGNED NOT NULL,\n    PRIMARY KEY (`SpellId`)\n) ENGINE=InnoDB "
                  "DEFAULT CHARSET=utf8mb4;\n")
        for start in range(0, len(mapping), 500):
            chunk = mapping[start:start + 500]
            out.write("INSERT INTO `mod_subclass_other_head_spell` (`SpellId`, `ShownId`) VALUES\n")
            out.write(",\n".join("(%d, %d)" % pair for pair in chunk) + ";\n")
    print(len(new_spells), "spells,", len(new_visuals), "visuals,", len(new_kits), "kits")


if __name__ == "__main__":
    main(*sys.argv[1:])
