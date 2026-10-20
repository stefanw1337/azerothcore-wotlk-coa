"""Let the client leave the shot dead zone to the server, which lifts it for a subclass whose other half shoots.
    python mkranges.py <SpellRange.dbc from the client> <output SpellRange.dbc>"""
import struct
import sys

RANGED = 2
FLAGS = 20


def main(source, target):
    data = bytearray(open(source, "rb").read())
    _, count, _, size, _ = struct.unpack_from("<4s4I", data)
    opened = 0
    for index in range(count):
        offset = 20 + index * size + FLAGS
        flags = struct.unpack_from("<I", data, offset)[0]
        if flags & RANGED:
            struct.pack_into("<I", data, offset, flags & ~RANGED)
            opened += 1
    open(target, "wb").write(data)
    print("shot ranges handed to the server:", opened)


if __name__ == "__main__":
    main(*sys.argv[1:])
