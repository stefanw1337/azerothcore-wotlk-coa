"""Write the subclass racials and second-self auras into Spell.dbc and the matching spell_dbc SQL, and lift the
ranged dead zone from shots so a brother standing back can take them (reads Spell.dbc and SpellRange.dbc from SRC)."""
import struct
import sys

SRC, OUT, SQL = sys.argv[1], sys.argv[2], sys.argv[3]
FIELDS, SIZE = 234, 936
ATTR0_CANT_CANCEL = 0x80000000
ATTR0_HIDDEN_CLIENTSIDE = 0x00000080
ATTR3_DEATH_PERSISTENT = 0x00100000

RACIAL, AURA, MODE, REACH, PASSIVE = "racial", "aura", "mode", "reach", "passive"
ATTR0_PASSIVE = 0x00000040
SPELL_AURA_ASCENSION_IGNORE_MIN_RANGE = 312
IGNORE_MIN_RANGE_RANGED_ABILITIES = 10
SPELLS = [
    (9530100, RACIAL, 18227, "Brother, Where Art Thou?", "Racial",
     "Call your twin brother to your side, or send him home. Once you have split at level 20, his class "
     "abilities rest while he is away.", ""),
    (9530101, RACIAL, 6102, "Agree or Disagree", "Racial",
     "Your two heads either agree for once, or go back to arguing. While they agree, the second head's "
     "class abilities stay quiet.", ""),
    (9530102, AURA, 3609, "Teaming Up", "", "", "You're weak! Half your stats now belong to your brother."),
    (9530103, AURA, 63, "Lonely and Strong", "", "", "Your brother is at home. Every stat you have is yours alone."),
    (9530104, AURA, 24897, "Agreed", "", "", "For once, both heads think alike. The other head's tricks stay quiet."),
    (9530105, AURA, 25332, "Disagreed", "", "",
     "Your heads can't agree on anything. Half your stats now belong to the other head."),
    (9530106, RACIAL, 113360, "Play Easy, Cowboy", "Racial",
     "Let your brother pick and cast his own spells in combat, or take the reins back. Pressing one of his "
     "spells yourself always comes first. Requires your brother at your side.", ""),
    (9530107, RACIAL, 25225, "Two Heads, Half a Brain", "Racial",
     "Let your other head cast her own spells in combat, or take control back. Pressing one of her spells "
     "yourself always comes first. Requires your heads to disagree.", ""),
    (9530108, MODE, 113360, "Play Easy, Cowboy", "", "", "Your brother picks his own spells."),
    (9530109, MODE, 25225, "Two Heads, Half a Brain", "", "", "Your other head does what she likes."),
    (9530110, REACH, 3609, "Brother's Reach", "", "", "Your brother shoots from where he stands."),
    (9530111, PASSIVE, 18227, "Twin-Linked", "Racial Passive",
     "You and your brother share one health and mana pool, and whatever befalls you befalls him too: "
     "stuns, fears and even sheep.", ""),
]
CASTER_AURA = {9530106: 9530102, 9530107: 9530105}

data = open(f"{SRC}/Spell.dbc", "rb").read()
magic, count, fields, size, string_size = struct.unpack_from("<4s4I", data)
assert (fields, size) == (FIELDS, SIZE)
records = bytearray(data[20:20 + count * size])
strings = bytearray(data[20 + count * size:])
ours = {spell[0] for spell in SPELLS}
kept = [records[i * size:(i + 1) * size] for i in range(count)
        if struct.unpack_from("<I", records, i * size)[0] not in ours]
records = bytearray(b"".join(kept))
count = len(kept)


def text(value):
    if not value:
        return 0
    offset = len(strings)
    strings.extend(value.encode("utf-8") + b"\0")
    return offset


def build(spell_id, kind, icon, name, rank, description, tooltip):
    row = [0] * FIELDS
    floats = {}
    row[0] = spell_id
    row[68] = -1
    row[28] = 1
    row[40] = 21
    row[46] = 1
    row[86] = 1
    row[133] = icon
    for column, value in ((136, name), (153, rank), (170, description), (187, tooltip)):
        row[column] = text(value)
        row[column + 16] = 0x00FFFFEE if value else 0
    row[225] = 1
    floats.update({216: 1.0, 217: 1.0, 218: 1.0, 229: 1.0, 230: 1.0, 231: 1.0})
    row[24] = CASTER_AURA.get(spell_id, 0)
    if kind == RACIAL:
        row[71] = 3
        row[205] = 133
        row[206] = 1500
        row[40] = 0
    elif kind == MODE:
        row[71] = 6
        row[95] = 4
    elif kind == PASSIVE:
        row[4] = ATTR0_PASSIVE
        row[71] = 6
        row[95] = 4
    elif kind == REACH:
        row[4] = ATTR0_CANT_CANCEL | ATTR0_HIDDEN_CLIENTSIDE
        row[7] = ATTR3_DEATH_PERSISTENT
        row[71] = 6
        row[95] = SPELL_AURA_ASCENSION_IGNORE_MIN_RANGE
        row[110] = IGNORE_MIN_RANGE_RANGED_ABILITIES
    else:
        row[4] = ATTR0_CANT_CANCEL
        row[7] = ATTR3_DEATH_PERSISTENT
        row[71] = 6
        row[95] = 4
    return row, floats


def pack(row, floats):
    packed = bytearray(SIZE)
    for column in range(FIELDS):
        if column in floats:
            struct.pack_into("<f", packed, column * 4, floats[column])
        else:
            struct.pack_into("<I", packed, column * 4, row[column] & 0xFFFFFFFF)
    return packed


FLOAT_COLUMNS = {47, 101, 102, 103, 119, 120, 121, 216, 217, 218, 229, 230, 231}
STRING_COLUMNS = {column for start in (136, 153, 170, 187) for column in range(start, start + 16)}
SIGNED_COLUMNS = {68, 69, 70}


def sql_value(column, row, floats, texts):
    if column in STRING_COLUMNS:
        return "'" + texts.get(column, "").replace("'", "''") + "'"
    if column in FLOAT_COLUMNS:
        return repr(float(floats.get(column, 0.0)))
    value = row[column] & 0xFFFFFFFF
    if column in SIGNED_COLUMNS and value >= 0x80000000:
        value -= 0x100000000
    return str(value)


def literal_parts(token, limit=100):
    if not token.startswith("'") or len(token) <= limit:
        return [token]
    words = token[1:-1].split(" ")
    parts, current = [], ""
    for word in words:
        candidate = word if not current else current + " " + word
        if len(candidate) > limit - 3 and current:
            parts.append("'" + current + " '")
            current = word
        else:
            current = candidate
    parts.append("'" + current + "'")
    return parts


def wrapped(tokens, opener="(", closer=")", width=120):
    lines = []
    line = opener
    for index, token in enumerate(tokens):
        parts = literal_parts(token)
        for part_index, part in enumerate(parts):
            last_part = part_index == len(parts) - 1
            piece = part + ((closer if index == len(tokens) - 1 else ",") if last_part else "")
            if line not in (opener, "    ") and len(line) + 1 + len(piece) > width:
                lines.append(line)
                line = "    " + piece
            else:
                line += ("" if line in (opener, "    ") else " ") + piece
    lines.append(line)
    return "\n".join(lines)


SHOOTERS = (1 << 2) | (1 << 3) | (1 << 18)
SPELLFAMILY_HUNTER = 9
SHARED_SHOTS = {75, 2764, 3018, 5019}
SPELL_RANGE_RANGED = 2
ATTR0_USES_RANGED_SLOT = 0x00000002
CUSTOM_CLASS_FAMILIES = range(18, 39)


def range_table():
    table = open(f"{SRC}/SpellRange.dbc", "rb").read()
    _, rows_count, _, row_size, _ = struct.unpack_from("<4s4I", table)
    return {struct.unpack_from("<I", table, 20 + i * row_size)[0]: struct.unpack_from("<I4fI", table, 20 + i * row_size)
            for i in range(rows_count)}


def without_dead_zone(ranges):
    plain = {}
    for index, (_, min_hostile, min_friend, max_hostile, max_friend, flags) in ranges.items():
        if flags & SPELL_RANGE_RANGED:
            same_reach = (min_hostile, min_friend, max_hostile, max_friend)
            plain[index] = next((other for other, row in sorted(ranges.items())
                                 if row[1:5] == same_reach and not row[5]), None)
    return {index: other for index, other in plain.items() if other}


def lift_dead_zones(records, count):
    plain = without_dead_zone(range_table())
    lifted = 0
    for index in range(count):
        offset = index * SIZE
        spell_id, = struct.unpack_from("<I", records, offset)
        item_class, = struct.unpack_from("<i", records, offset + 68 * 4)
        item_mask, = struct.unpack_from("<I", records, offset + 69 * 4)
        family, = struct.unpack_from("<I", records, offset + 208 * 4)
        range_index, = struct.unpack_from("<I", records, offset + 46 * 4)
        if item_class != 2 or not item_mask & SHOOTERS or family == SPELLFAMILY_HUNTER or spell_id in SHARED_SHOTS:
            continue
        changed = False
        if range_index in plain:
            struct.pack_into("<I", records, offset + 46 * 4, plain[range_index])
            changed = True
        attributes, = struct.unpack_from("<I", records, offset + 4 * 4)
        if family in CUSTOM_CLASS_FAMILIES and attributes & ATTR0_USES_RANGED_SLOT:
            struct.pack_into("<I", records, offset + 4 * 4, attributes & ~ATTR0_USES_RANGED_SLOT)
            changed = True
        lifted += changed
    return lifted


print("shots without a dead zone:", lift_dead_zones(records, count))
rows = bytearray()
values = []
for spell in SPELLS:
    row, floats = build(*spell)
    rows += pack(row, floats)
    texts = {136: spell[3], 153: spell[4], 170: spell[5], 187: spell[6]}
    values.append(wrapped([sql_value(column, row, floats, texts) for column in range(FIELDS)]))
with open(f"{OUT}/Spell.dbc", "wb") as out:
    out.write(struct.pack("<4s4I", b"WDBC", count + len(SPELLS), FIELDS, SIZE, len(strings)))
    out.write(records)
    out.write(rows)
    out.write(strings)
header = open(SQL).read()
header = header[header.index("INSERT INTO"):header.index(") VALUES") + len(") VALUES")]
ids = ", ".join(str(spell[0]) for spell in SPELLS)
with open(SQL, "w") as out:
    out.write("-- mod-subclass: server rows for the second-self racials and auras "
              "(the client gets the same rows in patch-ZZX).\n\n")
    out.write(wrapped(ids.split(", "), "DELETE FROM `spell_dbc` WHERE `ID` IN (", ");") + "\n")
    out.write(header + "\n" + ",\n".join(values) + ";\n")
print("wrote", [spell[0] for spell in SPELLS])
