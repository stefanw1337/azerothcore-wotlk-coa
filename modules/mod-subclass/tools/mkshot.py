"""Give the hidden shot of a two-headed ogre's other head a spell visual that launches from her left hand.
    python mkshot.py <client DBFilesClient folder>"""
import struct
import sys
from pathlib import Path

SHOT_SPELL = 9530110
SHOT_VISUAL = 9534100
SPELL_VISUAL_FIELD = 131
VISUAL_FIELDS = 32
HAS_MISSILE, MISSILE_MODEL, MISSILE_DESTINATION, MISSILE_ATTACHMENT = 7, 8, 10, 16
SHOT_MISSILE = 4328
HAND_LEFT = 2
CHEST = 1


def rows_of(data):
    _, count, fields, size, _ = struct.unpack_from("<4s4I", data)
    return count, fields, size


def add_visual(path):
    data = bytearray(Path(path).read_bytes())
    count, fields, size = rows_of(data)
    assert fields == VISUAL_FIELDS
    kept = [data[20 + i * size:20 + (i + 1) * size] for i in range(count)
            if struct.unpack_from("<I", data, 20 + i * size)[0] != SHOT_VISUAL]
    values = [0] * VISUAL_FIELDS
    values[0] = SHOT_VISUAL
    values[HAS_MISSILE] = 1
    values[MISSILE_MODEL] = SHOT_MISSILE
    values[MISSILE_DESTINATION] = CHEST
    values[MISSILE_ATTACHMENT] = HAND_LEFT
    kept.append(struct.pack("<%di" % VISUAL_FIELDS, *values))
    strings = data[20 + count * size:]
    Path(path).write_bytes(struct.pack("<4s4I", b"WDBC", len(kept), fields, size, len(strings)) + b"".join(kept) +
                           bytes(strings))


def point_shot(path):
    data = bytearray(Path(path).read_bytes())
    count, _, size = rows_of(data)
    for index in range(count):
        if struct.unpack_from("<I", data, 20 + index * size)[0] == SHOT_SPELL:
            struct.pack_into("<I", data, 20 + index * size + SPELL_VISUAL_FIELD * 4, SHOT_VISUAL)
            Path(path).write_bytes(data)
            return
    raise ValueError("no hidden shot spell in Spell.dbc")


def main(folder):
    add_visual(Path(folder) / "SpellVisual.dbc")
    point_shot(Path(folder) / "Spell.dbc")
    print("other head shot launches from the left hand, visual", SHOT_VISUAL)


if __name__ == "__main__":
    main(*sys.argv[1:])
