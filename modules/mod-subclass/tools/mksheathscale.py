import struct
import sys
from pathlib import Path

BONES, ATTACHMENTS = 0x2C, 0xF0
BONE_SIZE, ATTACHMENT_SIZE = 88, 40
SCALE_TRACK = 56
SCALE_VALUE_SIZE = 12
HAND_RIGHT = 1
LARGE_WEAPON_LEFT, LARGE_WEAPON_RIGHT = 30, 31


def array(data, offset):
    return struct.unpack_from("<II", data, offset)


def append(data, payload):
    while len(data) % 16:
        data.append(0)
    offset = len(data)
    data += payload
    return offset


def attachment_bones(data):
    count, offset = array(data, ATTACHMENTS)
    return {struct.unpack_from("<I", data, offset + index * ATTACHMENT_SIZE)[0]:
            struct.unpack_from("<H", data, offset + index * ATTACHMENT_SIZE + 4)[0] for index in range(count)}


def own_copy(data, outer, size):
    count, offset = array(data, outer)
    inner = bytearray()
    for index in range(count):
        keys, at = array(data, offset + index * 8)
        inner += struct.pack("<II", keys, append(data, bytes(data[at:at + keys * size])) if keys else 0)
    return struct.pack("<II", count, append(data, bytes(inner)) if count else 0)


def hand_scale_on_back(model):
    data = bytearray(model)
    bones = attachment_bones(data)
    _, offset = array(data, BONES)
    hand = offset + bones[HAND_RIGHT] * BONE_SIZE + SCALE_TRACK
    for attachment in (LARGE_WEAPON_LEFT, LARGE_WEAPON_RIGHT):
        place = offset + bones[attachment] * BONE_SIZE + SCALE_TRACK
        times = own_copy(data, hand + 4, 4)
        values = own_copy(data, hand + 12, SCALE_VALUE_SIZE)
        data[place:place + 4] = data[hand:hand + 4]
        data[place + 4:place + 12] = times
        data[place + 12:place + 20] = values
    return bytes(data)


if __name__ == "__main__":
    path = Path(sys.argv[1])
    path.write_bytes(hand_scale_on_back(path.read_bytes()))
