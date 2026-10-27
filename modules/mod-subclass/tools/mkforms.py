"""Let borrowed forms that keep the character's own look show the character's weapons in the client."""
import struct
import sys

SRC = sys.argv[1]
OUT = sys.argv[2]
FLAGS_FIELD, DONT_USE_WEAPON = 19, 0x10
CURSED_FORM = 57
WEAPON_FORMS = (CURSED_FORM,)


def main():
    data = bytearray(open(SRC, "rb").read())
    magic, count, fields, size, _ = struct.unpack_from("<4s4I", data)
    if magic != b"WDBC" or fields <= FLAGS_FIELD:
        raise SystemExit(f"unexpected SpellShapeshiftForm layout: {magic} {fields} fields")
    changed = []
    for index in range(count):
        row = 20 + index * size
        form = struct.unpack_from("<I", data, row)[0]
        if form not in WEAPON_FORMS:
            continue
        flags = struct.unpack_from("<I", data, row + FLAGS_FIELD * 4)[0]
        struct.pack_into("<I", data, row + FLAGS_FIELD * 4, flags & ~DONT_USE_WEAPON)
        changed.append((form, flags, flags & ~DONT_USE_WEAPON))
    open(OUT, "wb").write(data)
    for form, before, after in changed:
        print(f"form {form}: flags {before:#x} -> {after:#x}")


main()
