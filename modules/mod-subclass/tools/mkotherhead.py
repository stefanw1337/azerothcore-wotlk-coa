import struct
import sys
from pathlib import Path

from mkcyclops import Table

FIRST_SPELL = 9700000
FIRST_VISUAL = 9700000
FIRST_KIT = 9700000
LAST_ID = 9800000
CLASS_FAMILIES = {3, 4, 5, 6, 7, 8, 9, 10, 11, 15}
HUNTER_FAMILY = 9
ATTRIBUTES, ATTRIBUTES_EX2 = 4, 6
USES_RANGED_SLOT, AUTO_REPEAT = 0x2, 0x20
SPELL_VISUAL, SPELL_FAMILY, DAMAGE_CLASS = 131, 208, 213
MELEE, RANGED = 2, 3
VISUAL_ARM_KITS = (1, 2, 6)
VISUAL_CAST_KIT = 2
VISUAL_HAS_MISSILE, VISUAL_MISSILE_MODEL, VISUAL_MISSILE_DESTINATION = 7, 8, 10
VISUAL_MISSILE_ATTACHMENT = 16
VISUAL_FIELDS = 32
KIT_FIELDS = 38
KIT_START_ANIMATION, KIT_ANIMATION, KIT_SOUND = 1, 2, 15
KIT_PROCS = range(17, 21)
ARM_ANIMATIONS = {16, 17, 18, 19, 57, 58, 31, 32, 33, 51, 52, 53, 54, 107, 108, 124, 125, 126}
NO_ANIMATION = -1
OFF_HAND_SWING = 87
BULLET_MISSILE = 4328
GUN_RELEASE_SOUND = 1148
HAND_LEFT = 2
TARGET_CHEST = 1


def values(row, count):
    return list(struct.unpack(f"<{count}i", row))


def packed(fields):
    return struct.pack(f"<{len(fields)}i", *fields)


def moves_an_arm(kit):
    return kit[KIT_START_ANIMATION] in ARM_ANIMATIONS or kit[KIT_ANIMATION] in ARM_ANIMATIONS


def is_gun_shot(spell):
    return spell[SPELL_FAMILY] == HUNTER_FAMILY and spell[DAMAGE_CLASS] == RANGED and \
        spell[ATTRIBUTES] & USES_RANGED_SLOT


def gunshot_kit(kit_id):
    kit = [0] * KIT_FIELDS
    kit[0] = kit_id
    kit[KIT_START_ANIMATION] = kit[KIT_ANIMATION] = NO_ANIMATION
    kit[KIT_SOUND] = GUN_RELEASE_SOUND
    for field in KIT_PROCS:
        kit[field] = -1
    return kit


def shot_visual(visual_id, original, kit_id):
    visual = list(original) if original else [0] * VISUAL_FIELDS
    visual[0] = visual_id
    visual[VISUAL_CAST_KIT] = kit_id
    visual[VISUAL_HAS_MISSILE] = 1
    if not visual[VISUAL_MISSILE_MODEL]:
        visual[VISUAL_MISSILE_MODEL] = BULLET_MISSILE
        visual[VISUAL_MISSILE_DESTINATION] = TARGET_CHEST
    visual[VISUAL_MISSILE_ATTACHMENT] = HAND_LEFT
    return visual


def main(client_root, mapping_sql):
    folder = Path(client_root) / "DBFilesClient"
    tables = {name: Table(folder / (name + ".dbc")) for name in ("Spell", "SpellVisual", "SpellVisualKit")}
    for table in tables.values():
        table.rows = [row for row in table.rows if not FIRST_SPELL <= struct.unpack_from("<I", row)[0] < LAST_ID]
    spells, visuals, kits = tables["Spell"], tables["SpellVisual"], tables["SpellVisualKit"]
    visual_rows = {struct.unpack_from("<I", row)[0]: values(row, visuals.fields) for row in visuals.rows}
    kit_rows = {struct.unpack_from("<I", row)[0]: values(row, kits.fields) for row in kits.rows}
    new_kits, new_visuals, new_spells, mapping = {}, {}, [], []

    def kit_id(key, make):
        if key not in new_kits:
            new_kits[key] = make(FIRST_KIT + len(new_kits))
        return new_kits[key][0]

    def visual_id(key, make):
        if key not in new_visuals:
            new_visuals[key] = make(FIRST_VISUAL + len(new_visuals))
        return new_visuals[key][0]

    def arm_free_kit(original, animation):
        def make(new_id):
            kit = list(kit_rows[original])
            kit[0] = new_id
            kit[KIT_START_ANIMATION] = NO_ANIMATION
            kit[KIT_ANIMATION] = animation
            return kit
        return kit_id((original, animation), make)

    def arm_free_copy(spell, visual, arm_kits, animation):
        def make(new_id):
            clone = list(visual)
            clone[0] = new_id
            for slot in VISUAL_ARM_KITS:
                if visual[slot] in arm_kits:
                    clone[slot] = arm_free_kit(visual[slot], animation)
            return clone
        copy = list(spell)
        copy[SPELL_VISUAL] = visual_id((spell[SPELL_VISUAL], animation), make)
        return copy

    def add(copy):
        copy[0] = FIRST_SPELL + len(new_spells)
        new_spells.append(packed(copy))
        return copy[0]

    for row in list(spells.rows):
        spell = values(row, spells.fields)
        if spell[SPELL_FAMILY] not in CLASS_FAMILIES:
            continue
        visual = visual_rows.get(spell[SPELL_VISUAL])
        if is_gun_shot(spell):
            copy = list(spell)
            shot_kit = kit_id("gunshot", gunshot_kit)
            copy[SPELL_VISUAL] = visual_id(("shot", spell[SPELL_VISUAL]),
                                           lambda new_id: shot_visual(new_id, visual, shot_kit))
            copy[ATTRIBUTES] &= ~USES_RANGED_SLOT
            copy[ATTRIBUTES_EX2] &= ~AUTO_REPEAT
            mapping.append((spell[0], add(copy)))
            continue
        if not visual:
            continue
        arm_kits = [visual[slot] for slot in VISUAL_ARM_KITS if visual[slot] in kit_rows and
                    moves_an_arm(kit_rows[visual[slot]])]
        if not arm_kits:
            continue
        animation = OFF_HAND_SWING if spell[DAMAGE_CLASS] == MELEE else NO_ANIMATION
        mapping.append((spell[0], add(arm_free_copy(spell, visual, arm_kits, animation))))
    spells.rows += new_spells
    visuals.rows += [packed(row) for row in new_visuals.values()]
    kits.rows += [packed(row) for row in new_kits.values()]
    for name, table in tables.items():
        table.save(folder / (name + ".dbc"))
    with open(mapping_sql, "w") as out:
        out.write("-- mod-subclass: copies of class spells the other head casts: their animation leaves the "
                  "casting arm alone, and gun shots fly from her left hand without the ranged slot. A melee strike's "
                  "copy swings the weapon her left hand holds while she casts.\n\n")
        out.write("DROP TABLE IF EXISTS `mod_subclass_other_head_spell`;\n")
        out.write("CREATE TABLE `mod_subclass_other_head_spell` (\n    `SpellId` INT UNSIGNED NOT NULL,\n"
                  "    `ShownId` INT UNSIGNED NOT NULL,\n    PRIMARY KEY (`SpellId`)\n) ENGINE=InnoDB "
                  "DEFAULT CHARSET=utf8mb4;\n")
        for start in range(0, len(mapping), 500):
            chunk = mapping[start:start + 500]
            out.write("INSERT INTO `mod_subclass_other_head_spell` (`SpellId`, `ShownId`) VALUES\n")
            out.write(",\n".join("(%d, %d)" % row for row in chunk) + ";\n")
    print(len(new_spells), "spells,", len(new_visuals), "visuals,", len(new_kits), "kits")


if __name__ == "__main__":
    main(*sys.argv[1:])
