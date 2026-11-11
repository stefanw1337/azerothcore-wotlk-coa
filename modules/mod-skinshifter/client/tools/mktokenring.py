"""The token counter's art for the Skin Collection addon. Each flavour has its own ring picture with holes
(tools\art\tokenring-<flavour>.png): the Possessor's has 20 holes, the Skinshifter's 10 (2 tokens each). A picture
becomes two 512x512 textures in the addon's Textures folder, and SkinTokensArt.lua says where the holes are.

  TokenRing_<flavour>.tga    the ring in grey, with alpha: the background gone, the ring band solid (its holes stay
                             dark over a bright world), the middle half dark for the count
  TokenFlames_<flavour>.tga  only what glows (flames, light), in grey, for additive blending: the addon burns it
                             brighter at 5, 10 and 20 tokens

Both are grey so the addon colours them by specialization (SkinTokens.lua, SPEC_COLOURS): one picture per flavour,
a colour per spec. The holes are found in the picture: round dark spots of one size on a circle.
A flavour without its own picture yet uses the Possessor's (with its 20 holes) as a stand-in.

    python mktokenring.py [addon folder]     (default: ..\\addon\\SkinCollection)

Needs only Pillow (the vendored wheel, as mkicons.py)."""
import math
import struct
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ART = HERE / "art"
# Each flavour's ring: "glow" art (light on dark, as the Possessor's ghost ring) is keyed out by its brightness and made
# grey, so the whole ring takes the spec's colour; "solid" art (stone, wood, as the Skinshifter's) keeps its colours,
# is cut from its background, and gets a grey halo around it for the spec's colour and the burning.
KINDS = {"possessor": "glow", "skinshifter": "solid"}
HOLE_COUNTS = (10, 20)  # the counter's holes: 20 of one token each, or 10 of two (the cap is a multiple of 20)
SIZE = 512


def pillow():
    try:
        from PIL import Image
        return Image
    except ImportError:
        sys.path.insert(0, str(HERE))
        from mkicons import _load_pillow
        _load_pillow()
        from PIL import Image
        return Image


def tga(path, image):
    """Uncompressed 32-bit TGA, bottom-left origin: the plain kind every WoW client loads from an addon folder."""
    width, height = image.size
    header = struct.pack("<BBBHHBHHHHBB", 0, 0, 2, 0, 0, 0, 0, 0, width, height, 32, 8)
    rows = []
    data = image.tobytes("raw", "BGRA")
    stride = width * 4
    for row in range(height - 1, -1, -1):
        rows.append(data[row * stride:(row + 1) * stride])
    path.write_bytes(header + b"".join(rows))


def find_holes(image):
    """The holes: round dark spots of about the same size. Returns (centres, radius) in the picture's pixels."""
    work = image.convert("L")
    scale = 768 / max(work.size)
    small = work.resize((round(work.width * scale), round(work.height * scale)))
    width, height = small.size
    dark = [value < 18 for value in small.tobytes()]
    seen = bytearray(width * height)
    spots = []
    for start in range(width * height):
        if not dark[start] or seen[start]:
            continue
        stack, pixels = [start], []
        seen[start] = 1
        while stack:
            index = stack.pop()
            pixels.append(index)
            x, y = index % width, index // width
            for nx, ny in ((x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)):
                if 0 <= nx < width and 0 <= ny < height:
                    neighbour = ny * width + nx
                    if dark[neighbour] and not seen[neighbour]:
                        seen[neighbour] = 1
                        stack.append(neighbour)
        area = len(pixels)
        if not 0.0004 * width * height <= area <= 0.01 * width * height:
            continue
        xs = [index % width for index in pixels]
        ys = [index // width for index in pixels]
        box_w, box_h = max(xs) - min(xs) + 1, max(ys) - min(ys) + 1
        if not 0.75 <= box_w / box_h <= 1.33 or area / (box_w * box_h) < 0.6:
            continue  # not round
        if min(xs) == 0 or min(ys) == 0 or max(xs) == width - 1 or max(ys) == height - 1:
            continue  # cut by the edge
        spots.append((sum(xs) / area, sum(ys) / area, area))
    # the holes are the largest group of spots of about one size
    spots.sort(key=lambda spot: spot[2], reverse=True)
    best = []
    for spot in spots:
        group = [other for other in spots if 0.7 <= other[2] / spot[2] <= 1.4]
        if len(group) > len(best):
            best = group
    if len(best) not in HOLE_COUNTS:
        raise SystemExit(f"found {len(best)} round holes of one size in the picture; the counter needs "
                         f"{' or '.join(map(str, HOLE_COUNTS))}")
    radius = math.sqrt(sum(spot[2] for spot in best) / len(best) / math.pi) / scale
    return [(x / scale, y / scale) for x, y, _ in best], radius


def clockwise_from_top(centres, middle):
    cx, cy = middle
    return sorted(centres, key=lambda c: (math.atan2(c[0] - cx, -(c[1] - cy)) + 2 * math.pi) % (2 * math.pi))


def smoothstep(edge0, edge1, value):
    t = min(1.0, max(0.0, (value - edge0) / (edge1 - edge0)))
    return t * t * (3 - 2 * t)


def background_colour(image):
    width, height = image.size
    px = image.load()
    samples = [px[x, y] for x in range(0, width, max(1, width // 64)) for y in (0, height - 1)] + \
              [px[x, y] for y in range(0, height, max(1, height // 64)) for x in (0, width - 1)]
    return tuple(sorted(sample[channel] for sample in samples)[len(samples) // 2] for channel in range(3))


def build(Image, picture, out_folder, flavour, kind):
    image = Image.open(picture).convert("RGB")
    centres, hole_radius = find_holes(image)
    count = len(centres)
    cx = sum(c[0] for c in centres) / count
    cy = sum(c[1] for c in centres) / count
    ring_radius = sum(math.hypot(c[0] - cx, c[1] - cy) for c in centres) / count
    if kind == "solid":
        return build_solid(Image, image, out_folder, flavour, centres, hole_radius, (cx, cy), ring_radius)
    half = ring_radius * 1.85  # the flames reach out past the ring
    box = (round(cx - half), round(cy - half), round(cx + half), round(cy + half))
    crop = image.crop(box).resize((SIZE, SIZE), Image.LANCZOS)
    k = SIZE / (2 * half)
    ring_r, hole_r = ring_radius * k, hole_radius * k
    band_in, band_out = ring_r - hole_r * 1.9, ring_r + hole_r * 1.9
    pixels = crop.load()

    border = [sum(pixels[x, y]) / 3 for x in range(0, SIZE, 8) for y in (0, SIZE - 1)] + \
             [sum(pixels[x, y]) / 3 for y in range(0, SIZE, 8) for x in (0, SIZE - 1)]
    border.sort()
    background = border[len(border) // 2]

    ring = Image.new("RGBA", (SIZE, SIZE))
    flames = Image.new("RGBA", (SIZE, SIZE))
    ring_px, flame_px = ring.load(), flames.load()
    middle = SIZE / 2
    for y in range(SIZE):
        for x in range(SIZE):
            r, g, b = pixels[x, y]
            grey = round(max(r, g, b) * 0.7 + (r + g + b) / 3 * 0.3)  # bright stays bright: the colour comes in the game
            distance = math.hypot(x + 0.5 - middle, y + 0.5 - middle)
            edge = 1 - smoothstep(middle * 0.82, middle * 0.99, distance)  # fades out before the texture's edge
            glow = max(0.0, (r + g + b) / 3 - background - 4)
            key = min(1.0, glow / 55) * edge
            band = smoothstep(band_in - 6, band_in, distance) * (1 - smoothstep(band_out, band_out + 6, distance))
            centre = 0.55 * (1 - smoothstep(band_in - 14, band_in - 2, distance))
            alpha = max(key, band, centre)
            ring_px[x, y] = (grey, grey, grey, round(alpha * 255))
            lift = min(255, round(max(0, grey - background - 4) * 1.6 * edge))
            flame_px[x, y] = (lift, lift, lift, 255)
    out_folder.mkdir(parents=True, exist_ok=True)
    tga(out_folder / f"TokenRing_{flavour.capitalize()}.tga", ring)
    tga(out_folder / f"TokenFlames_{flavour.capitalize()}.tga", flames)
    ordered = clockwise_from_top(centres, (cx, cy))
    holes = [((x - box[0]) * k / SIZE, (y - box[1]) * k / SIZE) for x, y in ordered]
    return holes, hole_r / SIZE, True


def build_solid(Image, image, out_folder, flavour, centres, hole_radius, middle_point, ring_radius):
    """Solid art: cut out of its background with its own colours; the middle darkened a little for the count; a grey
    halo around its edge (the spec's colour, burning brighter at the milestones)."""
    from PIL import ImageFilter
    cx, cy = middle_point
    bg = background_colour(image)
    # how far the art reaches from the middle: the furthest pixel clearly off the background
    small = image.resize((256, round(256 * image.height / image.width)))
    sx = small.width / image.width
    reach = 0.0
    spx = small.load()
    for y in range(small.height):
        for x in range(small.width):
            if sum(abs(a - b) for a, b in zip(spx[x, y], bg)) > 60:
                reach = max(reach, math.hypot(x / sx - cx, y / sx - cy))
    half = reach * 1.12  # room for the halo
    box = (round(cx - half), round(cy - half), round(cx + half), round(cy + half))
    canvas = Image.new("RGB", (box[2] - box[0], box[3] - box[1]), bg)  # past the picture's edge: its background
    canvas.paste(image, (-box[0], -box[1]))
    crop = canvas.resize((SIZE, SIZE), Image.LANCZOS)
    k = SIZE / (2 * half)
    ring_r, hole_r = ring_radius * k, hole_radius * k
    inner = ring_r - hole_r * 1.6
    pixels = crop.load()
    # the background is what is like the background colour and reaches the edge; everything it encloses is the ring
    # (dark stone inside it stays solid)
    from PIL import ImageDraw
    near = Image.new("L", (SIZE, SIZE))
    npx = near.load()
    for y in range(SIZE):
        for x in range(SIZE):
            npx[x, y] = 0 if sum(abs(a - c) for a, c in zip(pixels[x, y], bg)) < 40 else 255
    near = near.filter(ImageFilter.MinFilter(3)).filter(ImageFilter.MaxFilter(3))  # loose specks and stars go
    # dark stone in the shade can look like the background, so the ring's outline is taken around it: in each
    # direction the furthest point of the art (smoothed over the neighbouring directions), filled in
    npx = near.load()
    steps = 720
    reach = []
    for step in range(steps):
        angle = step / steps * 2 * math.pi
        dx, dy = math.sin(angle), -math.cos(angle)
        furthest = 0
        for radius in range(0, SIZE // 2):
            x, y = SIZE / 2 + dx * radius, SIZE / 2 + dy * radius
            if 0 <= int(x) < SIZE and 0 <= int(y) < SIZE and npx[int(x), int(y)]:
                furthest = radius
        reach.append(furthest)
    smooth = [sorted(reach[(step + offset) % steps] for offset in range(-6, 7))[6] for step in range(steps)]
    outline = [(SIZE / 2 + math.sin(step / steps * 2 * math.pi) * smooth[step],
                SIZE / 2 - math.cos(step / steps * 2 * math.pi) * smooth[step]) for step in range(steps)]
    filled = Image.new("L", (SIZE, SIZE))
    ImageDraw.Draw(filled).polygon(outline, fill=255)
    from PIL import ImageChops
    mask = ImageChops.lighter(filled, near).filter(ImageFilter.GaussianBlur(1.2))
    mpx = mask.load()
    ring = Image.new("RGBA", (SIZE, SIZE))
    rpx = ring.load()
    middle = SIZE / 2
    for y in range(SIZE):
        for x in range(SIZE):
            r, g, b = pixels[x, y]
            distance = math.hypot(x + 0.5 - middle, y + 0.5 - middle)
            shade = 1 - 0.4 * (1 - smoothstep(inner * 0.75, inner, distance))  # darker middle for the count
            rpx[x, y] = (round(r * shade), round(g * shade), round(b * shade), mpx[x, y])
    grown = mask.filter(ImageFilter.MaxFilter(9)).filter(ImageFilter.GaussianBlur(10))
    halo = Image.new("RGBA", (SIZE, SIZE))
    hpx, gpx = halo.load(), grown.load()
    for y in range(SIZE):
        for x in range(SIZE):
            value = max(0, gpx[x, y] - mpx[x, y])  # only outside the ring
            hpx[x, y] = (value, value, value, 255)
    out_folder.mkdir(parents=True, exist_ok=True)
    tga(out_folder / f"TokenRing_{flavour.capitalize()}.tga", ring)
    tga(out_folder / f"TokenFlames_{flavour.capitalize()}.tga", halo)
    ordered = clockwise_from_top(centres, (cx, cy))
    holes = [((x - box[0]) * k / SIZE, (y - box[1]) * k / SIZE) for x, y in ordered]
    return holes, hole_r / SIZE, False


def main():
    Image = pillow()
    addon = Path(sys.argv[1]) if len(sys.argv) > 1 else HERE.parent / "addon" / "SkinCollection"
    textures = addon / "Textures"
    lines = ["-- Written by tools\\mktokenring.py: each flavour's token ring (grey textures, coloured by spec in the game)",
             "-- and where its holes are (0..1 across the texture, from the top left; clockwise from the top), with a",
             "-- hole's radius on the same scale.",
             "SKIN_TOKEN_ART = {"]
    for flavour in KINDS:
        own = ART / f"tokenring-{flavour}.png"
        picture = own if own.exists() else ART / "tokenring-possessor.png"
        holes, radius, tint = build(Image, picture, textures, flavour,
                                    KINDS[flavour] if own.exists() else "glow")
        print(f"  {flavour}: {picture.name}{'' if own.exists() else ' (stand-in until its own picture)'}, "
              f"{len(holes)} holes found")
        lines.append(f"\t{flavour} = {{")
        lines.append(f"\t\tring = \"Interface\\\\AddOns\\\\SkinCollection\\\\Textures\\\\TokenRing_{flavour.capitalize()}\",")
        lines.append(f"\t\tflames = \"Interface\\\\AddOns\\\\SkinCollection\\\\Textures\\\\TokenFlames_{flavour.capitalize()}\",")
        lines.append(f"\t\tholeRadius = {radius:.4f},")
        lines.append(f"\t\ttint = {'true' if tint else 'false'},  -- true: the ring itself takes the spec's colour")
        lines.append("\t\tholes = { " + ", ".join(f"{{ {x:.4f}, {y:.4f} }}" for x, y in holes) + " },")
        lines.append("\t},")
    lines.append("}")
    (addon / "SkinTokensArt.lua").write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"  written to {addon}")


if __name__ == "__main__":
    main()
