import struct
import sys
from pathlib import Path

READY_1H = 26
ATTACK_OFF = 87
RANGED_STANCE = {
    29: READY_1H,
    48: READY_1H,
    109: READY_1H,
    110: READY_1H,
    46: ATTACK_OFF,
    47: ATTACK_OFF,
    49: ATTACK_OFF,
}


def sequence_of(model: bytes, animation: int) -> int:
    count, offset = struct.unpack_from("<II", model, 0x1C)
    for index in range(count):
        found, variation = struct.unpack_from("<HH", model, offset + index * 64)
        if found == animation and variation == 0:
            return index
    raise SystemExit(f"the model has no animation {animation}")


def lend_ranged_stance(path: Path) -> None:
    model = bytearray(path.read_bytes())
    count, offset = struct.unpack_from("<II", model, 0x24)
    for ranged, melee in RANGED_STANCE.items():
        if ranged >= count:
            raise SystemExit(f"the animation lookup ends before {ranged}")
        struct.pack_into("<h", model, offset + ranged * 2, sequence_of(model, melee))
    path.write_bytes(model)


if __name__ == "__main__":
    lend_ranged_stance(Path(sys.argv[1]))
