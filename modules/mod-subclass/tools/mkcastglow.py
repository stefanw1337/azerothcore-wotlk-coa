"""Give the Two-Headed Ogre's other head a glowing left hand while she casts. Her casts with a cast time wait on
the server and go off directly, so the precast visual of her spell copies never plays. For every distinct precast
hand glow of those copies this writes a hidden aura whose state visual puts that glow on her left hand, and the SQL
with the server's rows for the auras and the copy-to-glow mapping.
    python mkcastglow.py <client root> <mapping.sql>"""
import struct
import sys
from pathlib import Path

from mkcyclops import Table
from mkotherhead import FIRST_SPELL as FIRST_COPY, LAST_ID as LAST_COPY, KIT_FIELDS, KIT_HAND_EFFECTS, \
    KIT_PROCS, NO_ANIMATION, VISUAL_FIELDS, HAND_LEFT, packed, values

FIRST_ID = 9820000
LAST_ID = 9830000
SPELL_VISUAL, ICON, NAME, SCHOOL_MASK = 131, 133, 136, 225
VISUAL_PRECAST_KIT, VISUAL_STATE_KIT = 1, 4
KIT_START_ANIMATION, KIT_ANIMATION = 1, 2
ATTR0_CANT_CANCEL_HIDDEN = 0x80000080 - (1 << 32)
INSTANT, FOREVER, SELF, NO_ITEM = 1, 21, 1, -1
APPLY_AURA, TARGET_CASTER, DUMMY_AURA = 6, 1, 4
DAMAGE_MULTIPLIERS, BONUS_MULTIPLIERS = range(216, 219), range(229, 232)
ONE = struct.unpack("<i", struct.pack("<f", 1.0))[0]
GLOW_NAME = "Other Head Casting"


def own(row):
    return FIRST_ID <= struct.unpack_from("<I", row)[0] < LAST_ID


def hand_glow(kit_id, kits, attachments):
    for effect, attachment in attachments.get(kit_id, ()):
        if attachment == HAND_LEFT:
            return effect
    kit = kits.get(kit_id)
    return next((kit[field] for field in KIT_HAND_EFFECTS if kit and kit[field] > 0), 0)


def glow_kit(kit_id):
    kit = [0] * KIT_FIELDS
    kit[0] = kit_id
    kit[KIT_START_ANIMATION] = kit[KIT_ANIMATION] = NO_ANIMATION
    for field in KIT_PROCS:
        kit[field] = -1
    return kit


def glow_visual(visual_id, kit_id):
    visual = [0] * VISUAL_FIELDS
    visual[0] = visual_id
    visual[VISUAL_STATE_KIT] = kit_id
    return visual


def glow_spell(spell_id, visual_id, fields, name_offset):
    spell = [0] * fields
    spell[0] = spell_id
    spell[4] = ATTR0_CANT_CANCEL_HIDDEN
    spell[28], spell[40], spell[46], spell[68] = INSTANT, FOREVER, SELF, NO_ITEM
    spell[71], spell[86], spell[95] = APPLY_AURA, TARGET_CASTER, DUMMY_AURA
    spell[SPELL_VISUAL] = visual_id
    spell[ICON] = 1
    spell[NAME] = name_offset
    spell[SCHOOL_MASK] = 1
    for field in (*DAMAGE_MULTIPLIERS, *BONUS_MULTIPLIERS):
        spell[field] = ONE
    return spell


def main(client_root, mapping_sql):
    folder = Path(client_root) / "DBFilesClient"
    tables = {name: Table(folder / (name + ".dbc")) for name in ("Spell", "SpellVisual", "SpellVisualKit",
                                                                 "SpellVisualKitModelAttach")}
    for table in tables.values():
        table.rows = [row for row in table.rows if not own(row)]
    spells, visuals, kits, attach = (tables[name] for name in ("Spell", "SpellVisual", "SpellVisualKit",
                                                               "SpellVisualKitModelAttach"))
    visual_rows = {struct.unpack_from("<I", row)[0]: values(row, visuals.fields) for row in visuals.rows}
    kit_rows = {struct.unpack_from("<I", row)[0]: values(row, kits.fields) for row in kits.rows}
    attachments = {}
    for row in attach.rows:
        _, kit_id, effect, attachment = struct.unpack_from("<4I", row)
        attachments.setdefault(kit_id, []).append((effect, attachment))
    glows, mapping = {}, []
    name_offset = spells.text(GLOW_NAME)
    for row in spells.rows:
        copy_id = struct.unpack_from("<I", row)[0]
        if not FIRST_COPY <= copy_id < LAST_COPY:
            continue
        visual = visual_rows.get(values(row, spells.fields)[SPELL_VISUAL])
        effect = hand_glow(visual[VISUAL_PRECAST_KIT], kit_rows, attachments) if visual else 0
        if not effect:
            continue
        if effect not in glows:
            glows[effect] = FIRST_ID + len(glows)
            assert glows[effect] < LAST_ID
        mapping.append((copy_id, glows[effect]))
    for effect, glow_id in glows.items():
        kits.rows.append(packed(glow_kit(glow_id)))
        visuals.rows.append(packed(glow_visual(glow_id, glow_id)))
        attach.rows.append(struct.pack("<4I6f", glow_id, glow_id, effect, HAND_LEFT, 0, 0, 0, 0, 0, 0))
        spells.rows.append(packed(glow_spell(glow_id, glow_id, spells.fields, name_offset)))
    for name, table in tables.items():
        table.save(folder / (name + ".dbc"))
    with open(mapping_sql, "w", newline="\n") as out:
        out.write("-- mod-subclass: the other head's left hand glows while she casts. One hidden aura per\n"
                  "-- precast hand glow of her spell copies, and which glow each copy shows.\n\n")
        out.write("DROP TABLE IF EXISTS `mod_subclass_other_head_glow`;\n")
        out.write("CREATE TABLE `mod_subclass_other_head_glow` (\n    `ShownId` INT UNSIGNED NOT NULL,\n"
                  "    `GlowId` INT UNSIGNED NOT NULL,\n    PRIMARY KEY (`ShownId`)\n) ENGINE=InnoDB "
                  "DEFAULT CHARSET=utf8mb4;\n")
        out.write("DELETE FROM `mod_subclass_other_head_glow`;\n")
        out.write("INSERT INTO `mod_subclass_other_head_glow` (`ShownId`, `GlowId`) VALUES\n")
        out.write(",\n".join("(%d, %d)" % row for row in mapping) + ";\n\n")
        out.write("DELETE FROM `spell_dbc` WHERE `ID` BETWEEN %d AND %d;\n" % (FIRST_ID, LAST_ID - 1))
        out.write("INSERT INTO `spell_dbc` (`ID`, `Attributes`, `CastingTimeIndex`, `DurationIndex`, `RangeIndex`,\n"
                  "    `EquippedItemClass`, `Effect_1`, `ImplicitTargetA_1`, `EffectAura_1`, `SpellVisualID_1`,\n"
                  "    `SchoolMask`, `Name_Lang_enUS`) VALUES\n")
        out.write(",\n".join("(%d, %d, %d, %d, %d, %d, %d, %d, %d, %d, 1, '%s')" % (
            glow_id, ATTR0_CANT_CANCEL_HIDDEN & 0xFFFFFFFF, INSTANT, FOREVER, SELF, NO_ITEM, APPLY_AURA,
            TARGET_CASTER, DUMMY_AURA, glow_id, GLOW_NAME) for glow_id in glows.values()) + ";\n")
    print(len(glows), "hand glows for", len(mapping), "spell copies")


if __name__ == "__main__":
    main(*sys.argv[1:])
