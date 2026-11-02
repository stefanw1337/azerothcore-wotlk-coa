"""Merge client patches into one MPQ: every file of every input, a later input winning over an earlier one, written
zlib-compressed in 4 KB sectors with a listfile. Runs with plain Python (no StormLib), so it works on Windows.
    python mkmergepatch.py <out.MPQ> <patch> [<patch> ...]"""
import struct
import sys
import zlib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from mpqpull import CRYPT, HASH_KEY, HASH_NAME_A, HASH_NAME_B, HASH_OFFSET, Archive, hashed

LISTFILE = "(listfile)"
SECTOR_SHIFT = 3
SECTOR = 512 << SECTOR_SHIFT
HEADER_SIZE = 32
FILE_EXISTS, COMPRESSED = 0x80000000, 0x200
ZLIB = 0x02
EMPTY = 0xFFFFFFFF


def encrypted(data, key):
    seed = 0xEEEEEEEE
    words = list(struct.unpack(f"<{len(data) // 4}I", data))
    for index, plain in enumerate(words):
        seed = (seed + CRYPT[0x400 + (key & 0xFF)]) & 0xFFFFFFFF
        words[index] = plain ^ ((key + seed) & 0xFFFFFFFF)
        key = ((((~key) << 21) + 0x11111111) | (key >> 11)) & 0xFFFFFFFF
        seed = (plain + seed + (seed << 5) + 3) & 0xFFFFFFFF
    return struct.pack(f"<{len(words)}I", *words)


def names_in(archive, path):
    listing = archive.extract(LISTFILE)
    if listing is None:
        raise SystemExit(f"{path} has no listfile, so its files cannot be listed")
    names = {}
    for line in listing.decode("latin1").replace(";", "\n").splitlines():
        name = line.strip()
        if name and name != LISTFILE:
            names.setdefault(name.lower(), name)
    return names


def packed(data):
    sectors = [data[start:start + SECTOR] for start in range(0, len(data), SECTOR)] or [b""]
    pieces = []
    for sector in sectors:
        squeezed = bytes([ZLIB]) + zlib.compress(sector, 9)
        pieces.append(squeezed if len(squeezed) < len(sector) else sector)
    offsets = [(len(pieces) + 1) * 4]
    for piece in pieces:
        offsets.append(offsets[-1] + len(piece))
    return struct.pack(f"<{len(offsets)}I", *offsets) + b"".join(pieces)


def table_size(count):
    size = 16
    while size < count * 2:
        size *= 2
    return size


def merge(inputs):
    files = {}
    for path in inputs:
        archive = Archive(str(path))
        for lower, name in names_in(archive, path).items():
            data = archive.extract(name)
            if data is None:
                raise SystemExit(f"{path} lists {name} but it cannot be read")
            files[lower] = (name, data)
        print(f"{path.name}: {len(files)} files so far")
    listing = "\r\n".join(sorted(name for name, _ in files.values())).encode("latin1")
    files[LISTFILE.lower()] = (LISTFILE, listing)
    return [entry for _, entry in sorted(files.items())]


def written(entries):
    body, blocks = bytearray(), []
    for _, data in entries:
        stored = packed(data)
        blocks.append((HEADER_SIZE + len(body), len(stored), len(data), FILE_EXISTS | COMPRESSED))
        body += stored
    size = table_size(len(entries))
    hashes = [(EMPTY, EMPTY, EMPTY, EMPTY)] * size
    for block, (name, _) in enumerate(entries):
        slot = hashed(name, HASH_OFFSET) % size
        while hashes[slot][3] != EMPTY:
            slot = (slot + 1) % size
        hashes[slot] = (hashed(name, HASH_NAME_A), hashed(name, HASH_NAME_B), 0, block)
    hash_table = encrypted(b"".join(struct.pack("<4I", *entry) for entry in hashes), hashed("(hash table)", HASH_KEY))
    block_table = encrypted(b"".join(struct.pack("<4I", *entry) for entry in blocks), hashed("(block table)", HASH_KEY))
    hash_at = HEADER_SIZE + len(body)
    block_at = hash_at + len(hash_table)
    total = block_at + len(block_table)
    header = struct.pack("<4sIIHHIIII", b"MPQ\x1a", HEADER_SIZE, total, 0, SECTOR_SHIFT, hash_at, block_at, size,
                         len(blocks))
    return header + bytes(body) + hash_table + block_table


def verified(path, entries):
    archive = Archive(str(path))
    for name, data in entries:
        if archive.extract(name) != data:
            raise SystemExit(f"{name} reads back wrong from {path}")


def main(out_path, *inputs):
    entries = merge([Path(path) for path in inputs])
    output = Path(out_path)
    output.write_bytes(written(entries))
    verified(output, entries)
    print(f"{output}: {len(entries)} files, {output.stat().st_size} bytes, every file read back intact")


if __name__ == "__main__":
    main(*sys.argv[1:])
