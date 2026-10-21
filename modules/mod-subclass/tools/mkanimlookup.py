import struct
import sys
from pathlib import Path

MOUNT = 91
READY_1H = 26
ATTACK_OFF = 87
KNEEL = {140: 114, 141: 115, 142: 116}
FIRE_BOW, ATTACK_BOW = 47, 46
RANGED_STANCE = {29: READY_1H, 48: READY_1H, 109: READY_1H, 110: READY_1H, 46: ATTACK_OFF, 47: ATTACK_OFF,
                 49: ATTACK_OFF}
ONE_HANDED = {18: 17, 19: 17, 22: 21, 23: 21, 27: 26, 28: 26, 58: 57}
RIDER_WORDS = ("Mount", "Carriage")
STEED_WORDS = ("MountSelf",)


def animations(dbc: bytes) -> dict:
    count, fields, size, _ = struct.unpack_from("<4I", dbc, 4)
    texts = 20 + count * size
    found = {}
    for index in range(count):
        row = struct.unpack_from(f"<{fields}I", dbc, 20 + index * size)
        name = dbc[texts + row[1]:dbc.index(b"\0", texts + row[1])].decode()
        found[row[0]] = name
    return found


def own_sequences(model: bytes) -> dict:
    count, offset = struct.unpack_from("<II", model, 0x1C)
    found = {}
    for index in range(count):
        animation, variation = struct.unpack_from("<HH", model, offset + index * 64)
        if variation == 0:
            found.setdefault(animation, index)
    return found


def rider_pose(name: str) -> bool:
    return any(word in name for word in RIDER_WORDS) and not any(word in name for word in STEED_WORDS)


def relink(model_path: Path, dbc_path: Path) -> None:
    model = bytearray(model_path.read_bytes())
    names = animations(dbc_path.read_bytes())
    own = own_sequences(model)
    count, offset = struct.unpack_from("<II", model, 0x24)
    old = list(struct.unpack_from(f"<{count}h", model, offset))
    size = max(count, max(names) + 1)
    lookup = []
    for animation in range(size):
        current = old[animation] if animation < count else -1
        if animation in ONE_HANDED and ONE_HANDED[animation] in own:
            lookup.append(own[ONE_HANDED[animation]])
        elif animation in own:
            lookup.append(own[animation])
        elif animation == FIRE_BOW and ATTACK_BOW in own:
            lookup.append(own[ATTACK_BOW])
        elif animation in RANGED_STANCE:
            lookup.append(own[RANGED_STANCE[animation]])
        elif animation in KNEEL:
            lookup.append(own[KNEEL[animation]])
        elif animation in names and rider_pose(names[animation]) and MOUNT in own:
            lookup.append(own[MOUNT])
        else:
            lookup.append(current)
    while len(model) % 16:
        model.append(0)
    struct.pack_into("<II", model, 0x24, len(lookup), len(model))
    model += struct.pack(f"<{len(lookup)}h", *lookup)
    model_path.write_bytes(model)


if __name__ == "__main__":
    relink(Path(sys.argv[1]), Path(sys.argv[2]))
