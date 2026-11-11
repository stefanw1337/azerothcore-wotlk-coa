"""Turns pictures (jpg/png, any size) into 64x64 WoW icons (.blp, BLP2 DXT1 with mipmaps).
    python mkicons.py <picture> <out.blp> [<picture> <out.blp> ...]
Needs Pillow: an installed one, or (for a Python without pip) its wheel in tools\\vendor
(pillow-*-cp314-*-win_amd64.whl), unpacked next to it the first time it is needed.
The Skinshifter client patch picks the .blp files up from tools\\icons."""
import io
import struct
import sys
import zipfile
from pathlib import Path


def _load_pillow():
    try:
        import PIL  # noqa: F401
        return
    except ImportError:
        pass
    vendor = Path(__file__).resolve().parent / "vendor"
    unpacked = vendor / "pillow"
    if not (unpacked / "PIL").is_dir():
        tag = f"cp{sys.version_info.major}{sys.version_info.minor}"
        wheels = sorted(vendor.glob(f"pillow-*-{tag}-*.whl"))
        if not wheels:
            sys.exit(f"Pillow is missing: run 'python -m pip install pillow', or put the pillow wheel for Python "
                     f"{sys.version_info.major}.{sys.version_info.minor} ({tag}, win_amd64) in {vendor}")
        print(f"  unpacking {wheels[-1].name} into {unpacked}")
        zipfile.ZipFile(wheels[-1]).extractall(unpacked)
    sys.path.insert(0, str(unpacked))


_load_pillow()
from PIL import Image, ImageFilter  # noqa: E402


def dxt1(level):
    """DXT1 blocks of one mip level (Pillow's DDS writer, without its 128-byte header)."""
    buffer = io.BytesIO()
    level.save(buffer, format="DDS", pixel_format="DXT1")
    data = buffer.getvalue()[128:]
    blocks = max(1, (level.width + 3) // 4) * max(1, (level.height + 3) // 4)
    if len(data) != blocks * 8:
        raise SystemExit(f"Pillow wrote {len(data)} bytes of DXT1 for {level.size}, expected {blocks * 8}")
    return data


def blp(image):
    """BLP2 with DXT1 compression and a full mipmap chain: the format of Blizzard's own 3.3.5 icons."""
    image = image.convert("RGB")
    levels = []
    size = image.size[0]
    while size >= 1:
        level = image.resize((size, size), Image.LANCZOS)
        if size < 4:  # DXT works on 4x4 blocks: the smallest levels are stored as a padded block
            level = level.resize((4, 4), Image.NEAREST)
        levels.append(dxt1(level))
        size //= 2
    offsets, sizes, position = [], [], 20 + 64 + 64 + 1024  # header, offsets, sizes, (unused) palette
    for data in levels:
        offsets.append(position)
        sizes.append(len(data))
        position += len(data)
    offsets += [0] * (16 - len(levels))
    sizes += [0] * (16 - len(levels))
    width, height = image.size
    # BLP2, type 1, encoding 2 (DXT), alpha depth 0, alpha type 0 (DXT1), has mipmaps.
    header = struct.pack("<4sIBBBBII", b"BLP2", 1, 2, 0, 0, 1, width, height)
    header += struct.pack("<16I", *offsets) + struct.pack("<16I", *sizes) + b"\0" * 1024
    return header, levels


def blp_rect(image):
    """BLP2 DXT1 with mipmaps for any power-of-two size (talent backgrounds, spec pictures)."""
    image = image.convert("RGB")
    width, height = image.size
    levels = []
    w, h = width, height
    while True:
        level = image.resize((max(w, 1), max(h, 1)), Image.LANCZOS)
        if level.width < 4 or level.height < 4:
            level = level.resize((max(4, level.width), max(4, level.height)), Image.NEAREST)
        levels.append(dxt1(level))
        if w == 1 and h == 1 or len(levels) == 16:
            break
        w, h = max(1, w // 2), max(1, h // 2)
    offsets, sizes, position = [], [], 20 + 64 + 64 + 1024
    for data in levels:
        offsets.append(position)
        sizes.append(len(data))
        position += len(data)
    offsets += [0] * (16 - len(levels))
    sizes += [0] * (16 - len(levels))
    header = struct.pack("<4sIBBBBII", b"BLP2", 1, 2, 0, 0, 1, width, height)
    header += struct.pack("<16I", *offsets) + struct.pack("<16I", *sizes) + b"\0" * 1024
    return header + b"".join(levels)


def placed(picture, canvas, area, darkest_mean=None):
    """The picture cut to the area's shape (from its middle), scaled to it, at the top left of a black canvas.
    darkest_mean (0-1): a bright picture is darkened until its mean brightness is at most that, so what is drawn
    over it stays readable."""
    image = Image.open(picture).convert("RGB")
    if darkest_mean:
        # Bright parts (a white floor, a pale room) are pressed down first, then the whole picture is darkened
        # if it is still brighter than darkest_mean on average.
        from PIL import ImageEnhance, ImageStat
        # Darker overall (gamma), and nothing above about half brightness.
        curve = [round(255 * min((v / 255) ** 1.3, 0.3 + max(0.0, (v / 255) ** 1.3 - 0.3) * 0.3)) for v in range(256)]
        image = image.point(curve * 3)
        mean = sum(ImageStat.Stat(image.convert("L")).mean) / 255.0
        if mean > darkest_mean:
            image = ImageEnhance.Brightness(image).enhance(darkest_mean / mean)
    want = area[0] / area[1]
    if image.width / image.height > want:
        cut = int(image.height * want)
        image = image.crop(((image.width - cut) // 2, 0, (image.width - cut) // 2 + cut, image.height))
    else:
        cut = int(image.width / want)
        image = image.crop((0, (image.height - cut) // 2, image.width, (image.height - cut) // 2 + cut))
    out = Image.new("RGB", canvas)
    out.paste(image.resize(area, Image.LANCZOS), (0, 0))
    return out


def written(image):
    header, levels = blp(image)
    assert len(header) == 20 + 64 + 64 + 1024
    return header + b"".join(levels)


def icon(picture):
    image = Image.open(picture).convert("RGBA")
    side = min(image.size)
    left, top = (image.width - side) // 2, (image.height - side) // 2
    image = image.crop((left, top, left + side, top + side))
    image = image.resize((128, 128), Image.LANCZOS).filter(ImageFilter.UnsharpMask(radius=1, percent=60, threshold=2))
    return image.resize((64, 64), Image.LANCZOS)


if __name__ == "__main__":
    pairs = sys.argv[1:]
    if not pairs or len(pairs) % 2:
        raise SystemExit(__doc__)
    for picture, out in zip(pairs[::2], pairs[1::2]):
        Path(out).write_bytes(written(icon(picture)))
        print(f"  {picture} -> {out}")
