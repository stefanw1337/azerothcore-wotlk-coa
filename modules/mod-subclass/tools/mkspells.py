"""Append the subclass racials and second-self auras to Spell.dbc and emit the matching SQL ids."""
import struct
import sys

SRC, OUT = sys.argv[1], sys.argv[2]
FIELDS, SIZE = 234, 936
ATTR0_CANT_CANCEL = 0x80000000
ATTR0_HIDDEN_CLIENTSIDE = 0x00000080
ATTR3_DEATH_PERSISTENT = 0x00100000

RACIAL, AURA = "racial", "aura"
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
]

data = open(f"{SRC}/Spell.dbc", "rb").read()
magic, count, fields, size, string_size = struct.unpack_from("<4s4I", data)
assert (fields, size) == (FIELDS, SIZE)
records = bytearray(data[20:20 + count * size])
strings = bytearray(data[20 + count * size:])
existing = {struct.unpack_from("<I", records, i * size)[0] for i in range(count)}


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
    if kind == RACIAL:
        row[71] = 3
        row[205] = 133
        row[206] = 1500
        row[40] = 0
    else:
        row[4] = ATTR0_CANT_CANCEL
        row[7] = ATTR3_DEATH_PERSISTENT
        row[71] = 6
        row[95] = 4
    packed = bytearray(SIZE)
    for column in range(FIELDS):
        if column in floats:
            struct.pack_into("<f", packed, column * 4, floats[column])
        else:
            struct.pack_into("<I", packed, column * 4, row[column] & 0xFFFFFFFF)
    return packed


rows = bytearray()
for spell in SPELLS:
    assert spell[0] not in existing, spell[0]
    rows += build(*spell)
with open(f"{OUT}/Spell.dbc", "wb") as out:
    out.write(struct.pack("<4s4I", b"WDBC", count + len(SPELLS), FIELDS, SIZE, len(strings)))
    out.write(records)
    out.write(rows)
    out.write(strings)
print("added", [spell[0] for spell in SPELLS])
