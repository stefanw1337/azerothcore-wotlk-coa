import struct
import sys
from pathlib import Path

BONES, ATTACHMENTS = 0x2C, 0xF0
BONE_SIZE, ATTACHMENT_SIZE = 88, 40
SCALE_TRACK, TRACK_SIZE = 56, 20
HAND_RIGHT = 1
LARGE_WEAPON_LEFT, LARGE_WEAPON_RIGHT = 30, 31


def array(data, offset):
    return struct.unpack_from("<II", data, offset)


def attachment_bones(data):
    count, offset = array(data, ATTACHMENTS)
    return {struct.unpack_from("<I", data, offset + index * ATTACHMENT_SIZE)[0]:
            struct.unpack_from("<H", data, offset + index * ATTACHMENT_SIZE + 4)[0] for index in range(count)}


def hand_scale_on_back(model):
    data = bytearray(model)
    bones = attachment_bones(data)
    _, offset = array(data, BONES)
    hand = offset + bones[HAND_RIGHT] * BONE_SIZE + SCALE_TRACK
    for attachment in (LARGE_WEAPON_LEFT, LARGE_WEAPON_RIGHT):
        place = offset + bones[attachment] * BONE_SIZE + SCALE_TRACK
        data[place:place + TRACK_SIZE] = data[hand:hand + TRACK_SIZE]
    return bytes(data)


if __name__ == "__main__":
    path = Path(sys.argv[1])
    path.write_bytes(hand_scale_on_back(path.read_bytes()))
