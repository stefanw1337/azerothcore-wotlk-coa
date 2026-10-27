import re
import struct
import sys
from pathlib import Path

from mkcyclops import Table

FIRST_SPELL = 9700000
FIRST_VISUAL = 9700000
FIRST_KIT = 9700000
LAST_ID = 9800000
CLASS_FAMILIES = {3, 4, 5, 6, 7, 8, 9, 10, 11, 15} | set(range(18, 39))
HUNTER_FAMILY = 9
ATTRIBUTES, ATTRIBUTES_EX2 = 4, 6
USES_RANGED_SLOT, AUTO_REPEAT = 0x2, 0x20
KEEPS_SHEATH = 0x40000
SPELL_VISUAL, SPELL_FAMILY, DAMAGE_CLASS = 131, 208, 213
MELEE, RANGED = 2, 3
VISUAL_ARM_KITS = (1, 2, 6)
VISUAL_CAST_KIT = 2
VISUAL_HAS_MISSILE, VISUAL_MISSILE_MODEL, VISUAL_MISSILE_DESTINATION = 7, 8, 10
VISUAL_MISSILE_ATTACHMENT = 16
VISUAL_FIELDS = 32
KIT_FIELDS = 38
KIT_START_ANIMATION, KIT_ANIMATION, KIT_SOUND = 1, 2, 15
KIT_HAND_EFFECTS = (6, 7)
LEFT_ARM_CASTS = {31: 214, 51: 215, 52: 216, 53: 217, 54: 218, 124: 219, 125: 220}
SPELL_EFFECTS, SPELL_TRIGGERS, LEARN_SPELL = range(71, 74), range(116, 119), 36
ADVANCEMENT_RANK_SPELLS = range(20, 40, 4)
CLASS_SPELL = re.compile(r"\{\s*\d+\s*,\s*\d+\s*,\s*(\d+)\s*\}")
KIT_PROCS = range(17, 21)
ARM_ANIMATIONS = {16, 17, 18, 19, 57, 58, 31, 32, 33, 51, 52, 53, 54, 107, 108, 124, 125, 126}
NO_ANIMATION = -1
OFF_HAND_SWING = 87
BULLET_MISSILE = 4328
GUN_RELEASE_SOUND = 1148
HAND_LEFT = 2
CLIENT_LAUNCH_POINT = -1
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
    visual[VISUAL_MISSILE_ATTACHMENT] = CLIENT_LAUNCH_POINT
    return visual


def borrowable_spells(advancement_dbc, class_data_header):
    found = set()
    if class_data_header:
        text = Path(class_data_header).read_text()
        start = text.index("ClassSpells =")
        found |= {int(spell) for spell in CLASS_SPELL.findall(text[start:text.index("}};", start)])}
    if advancement_dbc:
        data = Path(advancement_dbc).read_bytes()
        _, count, _, size, _ = struct.unpack_from("<4s4I", data)
        for index in range(count):
            for offset in ADVANCEMENT_RANK_SPELLS:
                spell = struct.unpack_from("<I", data, 20 + index * size + offset)[0]
                if spell:
                    found.add(spell)
    return found


def taught_spells(spell):
    return {spell[trigger] for effect, trigger in zip(SPELL_EFFECTS, SPELL_TRIGGERS)
            if spell[effect] == LEARN_SPELL and spell[trigger]}


def left_arm(animation, melee):
    if animation not in ARM_ANIMATIONS:
        return animation
    if melee:
        return OFF_HAND_SWING
    return LEFT_ARM_CASTS.get(animation, NO_ANIMATION)


def main(client_root, mapping_sql, advancement_dbc=None, class_data_header=None):
    folder = Path(client_root) / "DBFilesClient"
    tables = {name: Table(folder / (name + ".dbc")) for name in ("Spell", "SpellVisual", "SpellVisualKit",
                                                                 "SpellVisualKitModelAttach")}
    for table in tables.values():
        table.rows = [row for row in table.rows if not FIRST_SPELL <= struct.unpack_from("<I", row)[0] < LAST_ID]
    spells, visuals, kits = tables["Spell"], tables["SpellVisual"], tables["SpellVisualKit"]
    attachments = tables["SpellVisualKitModelAttach"]
    visual_rows = {struct.unpack_from("<I", row)[0]: values(row, visuals.fields) for row in visuals.rows}
    kit_rows = {struct.unpack_from("<I", row)[0]: values(row, kits.fields) for row in kits.rows}
    new_kits, new_visuals, new_spells, new_attachments, mapping = {}, {}, [], [], []
    borrowable = borrowable_spells(advancement_dbc, class_data_header)
    spell_rows = {struct.unpack_from("<I", row)[0]: row for row in spells.rows}
    for spell_id in list(borrowable):
        if spell_id in spell_rows:
            borrowable |= taught_spells(values(spell_rows[spell_id], spells.fields))

    def kit_id(key, make):
        if key not in new_kits:
            new_kits[key] = make(FIRST_KIT + len(new_kits))
        return new_kits[key][0]

    def visual_id(key, make):
        if key not in new_visuals:
            new_visuals[key] = make(FIRST_VISUAL + len(new_visuals))
        return new_visuals[key][0]

    def left_arm_kit(original, melee):
        def make(new_id):
            kit = list(kit_rows[original])
            kit[0] = new_id
            start = kit[KIT_START_ANIMATION]
            kit[KIT_START_ANIMATION] = NO_ANIMATION if melee and start in ARM_ANIMATIONS else left_arm(start, melee)
            kit[KIT_ANIMATION] = left_arm(kit[KIT_ANIMATION], melee)
            for effect in dict.fromkeys(kit[field] for field in KIT_HAND_EFFECTS if kit[field] > 0):
                new_attachments.append(struct.pack("<4I6f", FIRST_SPELL + len(new_attachments), new_id, effect,
                                                   HAND_LEFT, 0, 0, 0, 0, 0, 0))
            for field in KIT_HAND_EFFECTS:
                kit[field] = 0
            return kit
        return kit_id((original, melee), make)

    def left_arm_copy(spell, visual, melee):
        def make(new_id):
            clone = list(visual)
            clone[0] = new_id
            for slot in VISUAL_ARM_KITS:
                if visual[slot] in kit_rows:
                    clone[slot] = left_arm_kit(visual[slot], melee)
            return clone
        copy = list(spell)
        copy[SPELL_VISUAL] = visual_id((spell[SPELL_VISUAL], melee), make)
        return copy

    def acts_with_an_arm(visual):
        return any(visual[slot] in kit_rows and (moves_an_arm(kit_rows[visual[slot]]) or
                                                 any(kit_rows[visual[slot]][field] > 0 for field in KIT_HAND_EFFECTS))
                   for slot in VISUAL_ARM_KITS)

    def add(copy):
        copy[0] = FIRST_SPELL + len(new_spells)
        copy[ATTRIBUTES] |= KEEPS_SHEATH
        new_spells.append(packed(copy))
        return copy[0]

    for row in list(spells.rows):
        spell = values(row, spells.fields)
        if spell[SPELL_FAMILY] not in CLASS_FAMILIES and spell[0] not in borrowable:
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
        if not visual or not (acts_with_an_arm(visual) or spell[0] in borrowable):
            continue
        mapping.append((spell[0], add(left_arm_copy(spell, visual, spell[DAMAGE_CLASS] == MELEE))))
    spells.rows += new_spells
    visuals.rows += [packed(row) for row in new_visuals.values()]
    kits.rows += [packed(row) for row in new_kits.values()]
    attachments.rows += new_attachments
    for name, table in tables.items():
        table.save(folder / (name + ".dbc"))
    with open(mapping_sql, "w") as out:
        out.write("-- mod-subclass: copies of class and borrowable spells the other head casts with her left arm: "
                  "spells use the left-arm cast animations and glow at the left hand, melee strikes swing the "
                  "weapon her left hand holds, gun shots fly from her left hand without the ranged slot, and no "
                  "copy puts the main hand's weapon away.\n\n")
        out.write("DROP TABLE IF EXISTS `mod_subclass_other_head_spell`;\n")
        out.write("CREATE TABLE `mod_subclass_other_head_spell` (\n    `SpellId` INT UNSIGNED NOT NULL,\n"
                  "    `ShownId` INT UNSIGNED NOT NULL,\n    PRIMARY KEY (`SpellId`)\n) ENGINE=InnoDB "
                  "DEFAULT CHARSET=utf8mb4;\n")
        out.write("DELETE FROM `mod_subclass_other_head_spell`;\n")
        out.write("INSERT INTO `mod_subclass_other_head_spell` (`SpellId`, `ShownId`) VALUES\n")
        out.write(",\n".join("(%d, %d)" % row for row in mapping) + ";\n")
    print(len(new_spells), "spells,", len(new_visuals), "visuals,", len(new_kits), "kits,", len(new_attachments),
          "hand attachments")


if __name__ == "__main__":
    main(*sys.argv[1:])
