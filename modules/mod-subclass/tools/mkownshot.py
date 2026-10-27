import struct
import sys
from pathlib import Path

from mkcyclops import Table
from mkotherhead import FIRST_SPELL as FIRST_COPY, ATTRIBUTES, ATTRIBUTES_EX2, AUTO_REPEAT, BULLET_MISSILE, \
    CLASS_FAMILIES, DAMAGE_CLASS, KEEPS_SHEATH, RANGED, SPELL_FAMILY, SPELL_VISUAL, TARGET_CHEST, USES_RANGED_SLOT, \
    VISUAL_FIELDS, VISUAL_HAS_MISSILE, VISUAL_MISSILE_ATTACHMENT, VISUAL_MISSILE_DESTINATION, VISUAL_MISSILE_MODEL, \
    packed, values

FIRST_ID = 9840000
LAST_ID = 9850000
SPELL_RIGHT_HAND = 4
SHOOT = 3018
EQUIPPED_ITEM_CLASS, EQUIPPED_ITEM_SUBCLASSES = 68, 69
ANY_ITEM, WEAPON = -1, 2
GUN_OR_CROSSBOW = (1 << 3) | (1 << 18)
TABLE = "mod_subclass_own_shot"


def own(row):
    return FIRST_ID <= struct.unpack_from("<I", row)[0] < LAST_ID


def fires_a_gun(spell):
    gun_fits = spell[EQUIPPED_ITEM_CLASS] == ANY_ITEM or \
        (spell[EQUIPPED_ITEM_CLASS] == WEAPON and spell[EQUIPPED_ITEM_SUBCLASSES] & GUN_OR_CROSSBOW)
    return spell[ATTRIBUTES] & USES_RANGED_SLOT and spell[DAMAGE_CLASS] == RANGED and gun_fits and \
        (spell[SPELL_FAMILY] in CLASS_FAMILIES or spell[0] == SHOOT) and not FIRST_COPY <= spell[0] < LAST_ID


def right_hand_visual(visual_id, original):
    visual = list(original) if original else [0] * VISUAL_FIELDS
    visual[0] = visual_id
    visual[VISUAL_HAS_MISSILE] = 1
    if not visual[VISUAL_MISSILE_MODEL]:
        visual[VISUAL_MISSILE_MODEL] = BULLET_MISSILE
        visual[VISUAL_MISSILE_DESTINATION] = TARGET_CHEST
    visual[VISUAL_MISSILE_ATTACHMENT] = SPELL_RIGHT_HAND
    return visual


def right_hand_copy(spell, copy_id, visual_id):
    copy = list(spell)
    copy[0] = copy_id
    copy[SPELL_VISUAL] = visual_id
    if not spell[SPELL_VISUAL]:
        copy[ATTRIBUTES] = (copy[ATTRIBUTES] & ~USES_RANGED_SLOT) | KEEPS_SHEATH
        copy[ATTRIBUTES_EX2] &= ~AUTO_REPEAT
    return copy


def main(client_root, mapping_sql):
    folder = Path(client_root) / "DBFilesClient"
    tables = {name: Table(folder / (name + ".dbc")) for name in ("Spell", "SpellVisual", "SpellVisualKit")}
    for table in tables.values():
        table.rows = [row for row in table.rows if not own(row)]
    spells, visuals = tables["Spell"], tables["SpellVisual"]
    visual_rows = {struct.unpack_from("<I", row)[0]: values(row, visuals.fields) for row in visuals.rows}
    new_visuals, new_spells, mapping = {}, [], []
    for row in list(spells.rows):
        spell = values(row, spells.fields)
        if not fires_a_gun(spell):
            continue
        original = spell[SPELL_VISUAL]
        if original not in new_visuals:
            new_visuals[original] = right_hand_visual(FIRST_ID + len(new_visuals), visual_rows.get(original))
        copy = right_hand_copy(spell, FIRST_ID + len(new_spells), new_visuals[original][0])
        new_spells.append(packed(copy))
        mapping.append((spell[0], copy[0]))
    if FIRST_ID + max(len(new_spells), len(new_visuals)) > LAST_ID:
        raise SystemExit("too many gun shots for the id range")
    spells.rows += new_spells
    visuals.rows += [packed(visual) for visual in new_visuals.values()]
    for name, table in tables.items():
        table.save(folder / (name + ".dbc"))
    with open(mapping_sql, "w") as out:
        out.write("-- mod-subclass: copies of the gun shots a Two-Headed Ogre fires with her own gun in her right "
                  "hand. Shots with their own visual are shown as the copy; shots without one (Auto Shot, Shoot) keep "
                  "the original without its ammo projectile and add the copy as a bullet-only missile.\n\n")
        out.write(f"DROP TABLE IF EXISTS `{TABLE}`;\n")
        out.write(f"CREATE TABLE `{TABLE}` (\n    `SpellId` INT UNSIGNED NOT NULL,\n"
                  "    `ShownId` INT UNSIGNED NOT NULL,\n    PRIMARY KEY (`SpellId`)\n) ENGINE=InnoDB "
                  "DEFAULT CHARSET=utf8mb4;\n")
        out.write(f"DELETE FROM `{TABLE}`;\n")
        out.write(f"INSERT INTO `{TABLE}` (`SpellId`, `ShownId`) VALUES\n")
        out.write(",\n".join("(%d, %d)" % pair for pair in mapping) + ";\n")
    print(len(new_spells), "shots,", len(new_visuals), "visuals")


if __name__ == "__main__":
    main(*sys.argv[1:])
