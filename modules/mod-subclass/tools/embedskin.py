"""Put a model's skin into EsteriaAppearanceGeometry.bin, which the client draws Esteria models from instead of the
skin in the MPQ. The entry keeps its model path; only its skin is replaced.
    python embedskin.py <EsteriaAppearanceGeometry.bin> <model path in the bin> <skin> <output .bin>"""
import struct
import sys

MAGIC = b"EAGM"
PATH_SIZE = 128


def entries(data):
    magic, version, count = struct.unpack_from("<4sII", data, 0)
    assert magic == MAGIC, "not an EsteriaAppearanceGeometry file"
    offset = 12
    for _ in range(count):
        path = data[offset:offset + PATH_SIZE].split(b"\0")[0].decode("latin-1")
        size = struct.unpack_from("<I", data, offset + PATH_SIZE)[0]
        yield path, data[offset:offset + PATH_SIZE], data[offset + PATH_SIZE + 4:offset + PATH_SIZE + 4 + size]
        offset += PATH_SIZE + 4 + size
    assert offset == len(data), "trailing bytes after the last entry"


def main(geometry, model_path, skin_path, output):
    data = open(geometry, "rb").read()
    skin = open(skin_path, "rb").read()
    magic, version, count = struct.unpack_from("<4sII", data, 0)
    rebuilt = bytearray(struct.pack("<4sII", magic, version, count))
    replaced = 0
    for path, raw_path, body in entries(data):
        if path.lower() == model_path.lower():
            body = skin
            replaced += 1
        rebuilt += raw_path + struct.pack("<I", len(body)) + body
    assert replaced == 1, "model path not found exactly once"
    open(output, "wb").write(rebuilt)
    print("replaced the skin of", model_path, "with", len(skin), "bytes")


if __name__ == "__main__":
    main(*sys.argv[1:])
