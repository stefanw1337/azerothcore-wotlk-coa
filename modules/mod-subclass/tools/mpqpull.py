import bz2
import struct
import sys
import zipfile
import zlib
from pathlib import Path

BASE_ARCHIVES = ("common", "common-2", "expansion", "lichking")
HASH_OFFSET, HASH_NAME_A, HASH_NAME_B, HASH_KEY = 0, 1, 2, 3
FILE_EXISTS, SINGLE_UNIT, COMPRESSED, IMPLODED = 0x80000000, 0x01000000, 0x200, 0x100
ENCRYPTED, FIX_KEY, SECTOR_CRC = 0x10000, 0x20000, 0x04000000
ZLIB, PKWARE, BZIP2 = 0x02, 0x08, 0x10
EMPTY_HASH = 0xFFFFFFFF


def crypt_table():
    table = [0] * 0x500
    seed = 0x00100001
    for index in range(0x100):
        for slot in range(5):
            seed = (seed * 125 + 3) % 0x2AAAAB
            high = (seed & 0xFFFF) << 16
            seed = (seed * 125 + 3) % 0x2AAAAB
            table[index + slot * 0x100] = high | (seed & 0xFFFF)
    return table


CRYPT = crypt_table()


def hashed(name, kind):
    seed1, seed2 = 0x7FED7FED, 0xEEEEEEEE
    for char in name.upper().replace("/", "\\").encode("latin1"):
        seed1 = (CRYPT[kind * 0x100 + char] ^ (seed1 + seed2)) & 0xFFFFFFFF
        seed2 = (char + seed1 + seed2 + (seed2 << 5) + 3) & 0xFFFFFFFF
    return seed1


def decrypted(data, key):
    seed = 0xEEEEEEEE
    words = list(struct.unpack(f"<{len(data) // 4}I", data[:len(data) // 4 * 4]))
    for index, word in enumerate(words):
        seed = (seed + CRYPT[0x400 + (key & 0xFF)]) & 0xFFFFFFFF
        plain = word ^ ((key + seed) & 0xFFFFFFFF)
        key = ((((~key) << 21) + 0x11111111) | (key >> 11)) & 0xFFFFFFFF
        seed = (plain + seed + (seed << 5) + 3) & 0xFFFFFFFF
        words[index] = plain
    return struct.pack(f"<{len(words)}I", *words) + data[len(words) * 4:]


class Bits:
    def __init__(self, data):
        self.data, self.position, self.buffer, self.count = data, 0, 0, 0

    def take(self, wanted):
        while self.count < wanted:
            self.buffer |= self.data[self.position] << self.count
            self.position += 1
            self.count += 8
        value = self.buffer & ((1 << wanted) - 1)
        self.buffer >>= wanted
        self.count -= wanted
        return value


def huffman(lengths):
    counts = [0] * 17
    for length in lengths:
        counts[length] += 1
    offsets = [0] * 17
    for length in range(1, 16):
        offsets[length + 1] = offsets[length] + counts[length]
    symbols = [0] * len(lengths)
    for symbol, length in enumerate(lengths):
        if length:
            symbols[offsets[length]] = symbol
            offsets[length] += 1
    return counts, symbols


def expanded(packed):
    return [length for byte in packed for length in [byte & 15] * ((byte >> 4) + 1)]


LITERALS = huffman(expanded(bytes((
    11, 124, 8, 7, 28, 7, 188, 13, 76, 4, 10, 8, 12, 10, 12, 10, 8, 23, 8, 9, 7, 6, 7, 8, 7, 6, 55, 8, 23, 24, 12, 11,
    7, 9, 11, 12, 6, 7, 22, 5, 7, 24, 6, 11, 9, 6, 7, 22, 7, 11, 38, 7, 9, 8, 25, 11, 8, 11, 9, 12, 8, 12, 5, 38, 5,
    38, 5, 11, 7, 5, 6, 21, 6, 10, 53, 8, 7, 24, 10, 27, 44, 253, 253, 253, 252, 252, 252, 13, 12, 45, 12, 45, 12, 61,
    12, 45, 44, 173))))
LENGTHS = huffman(expanded(bytes((2, 35, 36, 53, 38, 23))))
DISTANCES = huffman(expanded(bytes((2, 20, 53, 230, 247, 151, 248))))
BASE = (3, 2, 4, 5, 6, 7, 8, 9, 10, 12, 16, 24, 40, 72, 136, 264)
EXTRA = (0, 0, 0, 0, 0, 0, 0, 0, 1, 2, 3, 4, 5, 6, 7, 8)


def decoded(bits, table):
    counts, symbols = table
    code = first = index = 0
    for length in range(1, 17):
        code |= bits.take(1) ^ 1
        count = counts[length]
        if code < first + count:
            return symbols[index + code - first]
        index += count
        first = (first + count) << 1
        code <<= 1
    raise ValueError("bad implode code")


def exploded(data):
    bits = Bits(data)
    coded, dictionary = bits.take(8), bits.take(8)
    output = bytearray()
    while True:
        if bits.take(1):
            symbol = decoded(bits, LENGTHS)
            length = BASE[symbol] + bits.take(EXTRA[symbol])
            if length == 519:
                return bytes(output)
            shift = 2 if length == 2 else dictionary
            distance = (decoded(bits, DISTANCES) << shift) + bits.take(shift) + 1
            for _ in range(length):
                output.append(output[-distance])
        else:
            output.append(decoded(bits, LITERALS) if coded else bits.take(8))


def unpacked(sector, flags, size):
    if len(sector) >= size:
        return sector
    if flags & IMPLODED:
        return exploded(sector)
    mask, body = sector[0], sector[1:]
    if mask & BZIP2:
        body = bz2.decompress(body)
    if mask & PKWARE:
        body = exploded(body)
    if mask & ZLIB:
        body = zlib.decompress(body)
    return body


class Archive:
    def __init__(self, path):
        self.file = open(path, "rb")
        self.start = self.find_header()
        header = self.read(self.start, 44)
        _, _, _, version, shift, hash_at, block_at, hash_count, block_count = struct.unpack("<4sIIHHIIII", header[:32])
        hi_block_at, hash_high, block_high = struct.unpack("<QHH", header[32:44]) if version >= 1 else (0, 0, 0)
        self.sector = 512 << shift
        self.hashes = self.table((hash_high << 32) | hash_at, hash_count, "(hash table)")
        self.blocks = self.table((block_high << 32) | block_at, block_count, "(block table)")
        self.high = [0] * block_count
        if hi_block_at:
            self.high = list(struct.unpack(f"<{block_count}H", self.read(self.start + hi_block_at, block_count * 2)))

    def find_header(self):
        position = 0
        while True:
            self.file.seek(position)
            if self.file.read(4) == b"MPQ\x1a":
                return position
            position += 512

    def read(self, offset, size):
        self.file.seek(offset)
        return self.file.read(size)

    def table(self, offset, count, key_name):
        raw = decrypted(self.read(self.start + offset, count * 16), hashed(key_name, HASH_KEY))
        return [struct.unpack_from("<4I", raw, index * 16) for index in range(count)]

    def locate(self, name):
        count = len(self.hashes)
        if not count:
            return None
        first = hashed(name, HASH_OFFSET) % count
        name_a, name_b = hashed(name, HASH_NAME_A), hashed(name, HASH_NAME_B)
        for step in range(count):
            entry_a, entry_b, _, block = self.hashes[(first + step) % count]
            if block == EMPTY_HASH:
                return None
            if entry_a == name_a and entry_b == name_b and block < len(self.blocks):
                return block
        return None

    def extract(self, name):
        block = self.locate(name)
        if block is None:
            return None
        offset, packed_size, size, flags = self.blocks[block]
        if not flags & FILE_EXISTS:
            return None
        position = self.start + ((self.high[block] << 32) | offset)
        key = 0
        if flags & ENCRYPTED:
            key = hashed(name.replace("/", "\\").split("\\")[-1], HASH_KEY)
            if flags & FIX_KEY:
                key = ((key + offset) ^ size) & 0xFFFFFFFF
        raw = self.read(position, packed_size)
        if flags & SINGLE_UNIT:
            if flags & ENCRYPTED:
                raw = decrypted(raw, key)
            return unpacked(raw, flags, size) if flags & (COMPRESSED | IMPLODED) else raw[:size]
        sectors = (size + self.sector - 1) // self.sector
        if not flags & (COMPRESSED | IMPLODED):
            starts = [min(index * self.sector, packed_size) for index in range(sectors + 1)]
        else:
            table = raw[:(sectors + 1) * 4]
            if flags & ENCRYPTED:
                table = decrypted(table, (key - 1) & 0xFFFFFFFF)
            starts = struct.unpack(f"<{sectors + 1}I", table)
        output = bytearray()
        for index in range(sectors):
            piece = raw[starts[index]:starts[index + 1]]
            if flags & ENCRYPTED:
                piece = decrypted(piece, (key + index) & 0xFFFFFFFF)
            wanted = min(self.sector, size - index * self.sector)
            output += unpacked(piece, flags, wanted) if flags & (COMPRESSED | IMPLODED) else piece[:wanted]
        return bytes(output[:size])


def patch_order(path):
    stem = path.stem.lower()
    if stem in BASE_ARCHIVES:
        return (0, BASE_ARCHIVES.index(stem), "")
    suffix = stem[len("patch"):].lstrip("-")
    return (1, 0, suffix.upper())


def archives(data_folder):
    found = [path for path in Path(data_folder).iterdir() if path.suffix.lower() == ".mpq"]
    return sorted(found, key=patch_order, reverse=True)


def main(data_folder, wanted_path, zip_path):
    wanted = [line.strip() for line in Path(wanted_path).read_text().splitlines() if line.strip()]
    missing = {name.lower(): name for name in wanted}
    with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_DEFLATED) as bundle:
        for path in archives(data_folder):
            if not missing:
                break
            try:
                archive = Archive(path)
            except (OSError, struct.error, ValueError) as problem:
                print("skipped", path.name, problem)
                continue
            for lowered, name in list(missing.items()):
                try:
                    data = archive.extract(name)
                except (zlib.error, OSError, ValueError, IndexError) as problem:
                    print("unreadable", name, "in", path.name, problem)
                    continue
                if data is None:
                    continue
                bundle.writestr(name.replace("\\", "/"), data)
                bundle.writestr("_from/" + name.replace("\\", "/") + ".txt", path.name)
                del missing[lowered]
            print(path.name, "done,", len(missing), "left", flush=True)
    print(len(wanted) - len(missing), "found,", len(missing), "missing")
    for name in sorted(missing.values()):
        print("missing", name)


if __name__ == "__main__":
    main(*sys.argv[1:])
