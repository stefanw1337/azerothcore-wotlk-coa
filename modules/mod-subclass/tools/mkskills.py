"""Open every class skill line to every class, so borrowed subclass spells get their own spellbook tab."""
import struct
import sys

RACES, BASE, OUT, SERVER_OUT = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4]
CLASS_CATEGORY = 7
ALL_CLASSES = 0xFFFFFFFF


def load(path):
    data = bytearray(open(path, "rb").read())
    _, count, fields, size, _ = struct.unpack_from("<4s4I", data)
    return data, count, fields, size


line_data, line_count, line_fields, line_size = load(f"{BASE}/SkillLine.dbc")
class_lines = set()
for index in range(line_count):
    line_id, category = struct.unpack_from("<2I", line_data, 20 + index * line_size)
    if category == CLASS_CATEGORY:
        class_lines.add(line_id)

info, count, fields, size = load(f"{RACES}/SkillRaceClassInfo.dbc")
assert (fields, size) == (8, 32)
opened = 0
for index in range(count):
    offset = 20 + index * size
    skill = struct.unpack_from("<I", info, offset + 4)[0]
    if skill in class_lines:
        struct.pack_into("<I", info, offset + 12, ALL_CLASSES)
        opened += 1
open(f"{OUT}/SkillRaceClassInfo.dbc", "wb").write(info)

ability, count, fields, size = load(f"{RACES}/SkillLineAbility.dbc")
assert (fields, size) == (14, 56)
server_ability = bytes(ability)
spells = 0
for index in range(count):
    offset = 20 + index * size
    skill = struct.unpack_from("<I", ability, offset + 4)[0]
    if skill in class_lines and struct.unpack_from("<I", ability, offset + 16)[0]:
        struct.pack_into("<I", ability, offset + 16, 0)
        spells += 1
open(f"{OUT}/SkillLineAbility.dbc", "wb").write(ability)
open(f"{SERVER_OUT}/SkillLineAbility.dbc", "wb").write(server_ability)
open(f"{SERVER_OUT}/SkillRaceClassInfo.dbc", "wb").write(info)
print(f"class skill lines {len(class_lines)}, permission rows opened {opened}, spells opened for the client {spells}")
