"""Make the Thin Twins brother the player's mirror image: a copy of the model and its skin reflected left to right
(vertices, normals, bones, every animation key, attachments, events, cameras and bounds), with the triangle winding
reversed so it still faces out. Items the game hangs on the model keep their own shape, so the paired armour points
(elbows, shoulders, knees, hips, feet) trade places and the left shoulder piece still sits on the outer side of the
left shoulder; the hands do not trade, so the brother holds his main-hand weapon in his left hand and swings it with
that arm. The shield points (forearm and back) sit on bones turned half a round about the line shields are drawn
symmetric to, so the shield faces outward and still reaches the same way.
    python mkmirror.py <thinhumanmale.m2> <thinhumanmale00.skin> <out .m2> <out 00.skin>
    python mkmirror.py --client <Data folder> [<EsteriaAppearanceGeometry.bin>]
The client form builds the mirrored model from the client's own Thin Twins model, adds display 977602 (model 901402)
to CreatureDisplayInfo.dbc and CreatureModelData.dbc, merges all of it into patch-ZZZZY.MPQ and gives the mirrored
model its own copy of the Thin Twins entry in EsteriaAppearanceGeometry.bin."""
import math
import struct
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
for folder in (HERE, HERE.parents[2] / "tools" if len(HERE.parents) > 2 else HERE):
    if str(folder) not in sys.path:
        sys.path.append(str(folder))
import patch_handspring_client as h  # noqa: E402
import build_vault_patches as patches  # noqa: E402

VERTEX_SIZE, VERTEX_NORMAL = 48, 20
BONE_SIZE, BONE_FLAGS, BONE_PARENT, BONE_PIVOT = 88, 4, 8, 76
TRACKS = {h.TRANSLATION: "3f", h.ROTATION: "4h"}
ATTACHMENT_SIZE, ATTACHMENT_BONE, ATTACHMENT_POSITION, ATTACHMENT_LOOKUP = 40, 4, 8, 0xF8
EVENT_SIZE, EVENT_POSITION = 36, 12
CAMERA_SIZE, CAMERA_POSITION, CAMERA_TARGET = 100, 36, 68
CAMERA_SPLINES, CAMERA_ROLL = (16, 48), 80
GLOBAL_LOOPS = 0x14
MODEL_BOUNDS, COLLISION_BOUNDS = 0xA0, 0xBC
COLLISION_INDICES, COLLISION_POSITIONS, COLLISION_NORMALS = 0xD8, 0xE0, 0xE8
UNSUPPORTED = {0x48: "colours", 0x108: "lights", 0x120: "ribbons", 0x128: "particles"}
SKIN_INDICES, SKIN_SUBMESHES, SUBMESH_SIZE, SUBMESH_CENTRE, SUBMESH_SORT = 12, 28, 48, 20, 32
SHIELD_POINTS = (0, 28)
TRADED_PAIRS = ((3, 4), (5, 6), (7, 8), (9, 10), (47, 48))
TRANSFORMED = 0x200
SHIELD_AXIS = math.radians(12)
HALF_TURN = (math.cos(SHIELD_AXIS), 0.0, math.sin(SHIELD_AXIS), 0.0)


def counted(data, at):
    return struct.unpack_from("<II", data, at)


def flip_vector(data, at):
    x, y, z = struct.unpack_from("<3f", data, at)
    struct.pack_into("<3f", data, at, x, -y, z)


def flip_bounds(data, at):
    low = list(struct.unpack_from("<3f", data, at))
    high = list(struct.unpack_from("<3f", data, at + 12))
    low[1], high[1] = -high[1], -low[1]
    struct.pack_into("<6f", data, at, *low, *high)


def decoded(value):
    return (value + 32768) / 32767.0 if value < 0 else (value - 32767) / 32767.0


def encoded(value):
    value = max(-1.0, min(1.0, value))
    return round(value * 32767) - 32768 if value >= 0 else round(value * 32767) + 32767


def mirrored_quaternion(x, y, z, w):
    return -x, y, -z, w


def flip_key(data, at, track):
    if track == h.ROTATION:
        raw = struct.unpack_from("<4h", data, at)
        turned = mirrored_quaternion(*(decoded(value) for value in raw))
        struct.pack_into("<4h", data, at, *(encoded(value) for value in turned))
    else:
        flip_vector(data, at)


def flip_track(data, place, track, done, stride=None):
    count, outer = counted(data, place + 12)
    size = stride or struct.calcsize("<" + TRACKS[track])
    for sequence in range(count):
        keys, at = counted(data, outer + sequence * 8)
        if not keys or at in done:
            continue
        done.add(at)
        for key in range(keys):
            flip_key(data, at + key * size, track)


def flip_spline(data, place, done):
    count, outer = counted(data, place + 12)
    for sequence in range(count):
        keys, at = counted(data, outer + sequence * 8)
        if not keys or at in done:
            continue
        done.add(at)
        for key in range(keys):
            for part in range(3):
                flip_vector(data, at + key * 36 + part * 12)


def flip_roll(data, place, done):
    count, outer = counted(data, place + 12)
    for sequence in range(count):
        keys, at = counted(data, outer + sequence * 8)
        if not keys or at in done:
            continue
        done.add(at)
        for key in range(keys * 3):
            value = struct.unpack_from("<f", data, at + key * 4)[0]
            struct.pack_into("<f", data, at + key * 4, -value)


def append(data, payload, align=16):
    while len(data) % align:
        data.append(0)
    at = len(data)
    data += payload
    return at


def trade_pairs(data):
    count, records = counted(data, h.ATTACHMENTS)
    lookup_count, lookup_at = counted(data, ATTACHMENT_LOOKUP)
    lookup = list(struct.unpack_from(f"<{lookup_count}h", data, lookup_at))
    for first, second in TRADED_PAIRS:
        if max(first, second) >= lookup_count or lookup[first] < 0 or lookup[second] < 0:
            continue
        one, other = lookup[first], lookup[second]
        struct.pack_into("<I", data, records + one * ATTACHMENT_SIZE, second)
        struct.pack_into("<I", data, records + other * ATTACHMENT_SIZE, first)
        lookup[first], lookup[second] = other, one
    struct.pack_into(f"<{lookup_count}h", data, lookup_at, *lookup)


def global_loop(data):
    loops, _ = counted(data, GLOBAL_LOOPS)
    if not loops:
        raise ValueError("the model has no global sequence to hold the shield turn")
    return 0


def turned_shields(data):
    for point in SHIELD_POINTS:
        turned_shield(data, point)


def turned_shield(data, point):
    count, records = counted(data, h.ATTACHMENTS)
    lookup_count, lookup_at = counted(data, ATTACHMENT_LOOKUP)
    index = struct.unpack_from("<h", data, lookup_at + point * 2)[0] if lookup_count > point else -1
    if index < 0:
        return
    record = records + index * ATTACHMENT_SIZE
    parent = struct.unpack_from("<H", data, record + ATTACHMENT_BONE)[0]
    position = struct.unpack_from("<3f", data, record + ATTACHMENT_POSITION)
    bones, bone_at = counted(data, h.BONES)
    loop = global_loop(data)
    stamps = append(data, struct.pack("<2I", 0, 1), 4)
    value = append(data, struct.pack("<4h", *(encoded(component) for component in HALF_TURN)) * 2, 4)
    outer_stamps = append(data, struct.pack("<II", 2, stamps), 4)
    outer_values = append(data, struct.pack("<II", 2, value), 4)
    still = struct.pack("<hhIIII", 0, -1, 0, 0, 0, 0)
    turn = struct.pack("<hhIIII", 1, loop, 1, outer_stamps, 1, outer_values)
    bone = struct.pack("<iIhHI", -1, TRANSFORMED, parent, 0, 0) + still + turn + still + struct.pack("<3f", *position)
    table = bytes(data[bone_at:bone_at + bones * BONE_SIZE]) + bone
    struct.pack_into("<II", data, h.BONES, bones + 1, append(data, table))
    struct.pack_into("<H", data, record + ATTACHMENT_BONE, bones)


def mirrored_model(model):
    data = bytearray(model)
    for at, name in UNSUPPORTED.items():
        if counted(data, at)[0]:
            raise ValueError(f"mirroring {name} is not supported")
    count, at = counted(data, h.VERTICES)
    for index in range(count):
        flip_vector(data, at + index * VERTEX_SIZE)
        flip_vector(data, at + index * VERTEX_SIZE + VERTEX_NORMAL)
    done = set()
    bones, bone_at = counted(data, h.BONES)
    for bone in range(bones):
        record = bone_at + bone * BONE_SIZE
        flip_vector(data, record + BONE_PIVOT)
        for track in (h.TRANSLATION, h.ROTATION):
            flip_track(data, record + track, track, done)
    count, at = counted(data, h.ATTACHMENTS)
    for index in range(count):
        flip_vector(data, at + index * ATTACHMENT_SIZE + ATTACHMENT_POSITION)
    count, at = counted(data, h.EVENTS)
    for index in range(count):
        flip_vector(data, at + index * EVENT_SIZE + EVENT_POSITION)
    count, at = counted(data, h.CAMERAS)
    for index in range(count):
        record = at + index * CAMERA_SIZE
        flip_vector(data, record + CAMERA_POSITION)
        flip_vector(data, record + CAMERA_TARGET)
        for spline in CAMERA_SPLINES:
            flip_spline(data, record + spline, done)
        flip_roll(data, record + CAMERA_ROLL, done)
    count, at = counted(data, h.SEQUENCES)
    for index in range(count):
        flip_bounds(data, at + index * h.SEQUENCE_SIZE + h.SEQUENCE_BOUNDS)
    flip_bounds(data, MODEL_BOUNDS)
    flip_bounds(data, COLLISION_BOUNDS)
    count, at = counted(data, COLLISION_POSITIONS)
    for index in range(count):
        flip_vector(data, at + index * 12)
    count, at = counted(data, COLLISION_NORMALS)
    for index in range(count):
        flip_vector(data, at + index * 12)
    count, at = counted(data, COLLISION_INDICES)
    for first in range(at, at + count * 2, 6):
        a, b, c = struct.unpack_from("<3H", data, first)
        struct.pack_into("<3H", data, first, a, c, b)
    trade_pairs(data)
    turned_shields(data)
    return bytes(data)


def mirrored_skin(skin):
    data = bytearray(skin)
    count, at = counted(data, SKIN_INDICES)
    for first in range(at, at + count * 2 - 4, 6):
        a, b, c = struct.unpack_from("<3H", data, first)
        struct.pack_into("<3H", data, first, a, c, b)
    count, at = counted(data, SKIN_SUBMESHES)
    for index in range(count):
        flip_vector(data, at + index * SUBMESH_SIZE + SUBMESH_CENTRE)
        flip_vector(data, at + index * SUBMESH_SIZE + SUBMESH_SORT)
    return bytes(data)


MODEL = "custom\\thinhuman\\native\\male\\thinhumanmale.m2"
MIRROR = "custom\\thinhuman\\native\\male\\thinhumanmirrormale.m2"
MODEL_DATA, DISPLAY_INFO = "DBFilesClient\\CreatureModelData.dbc", "DBFilesClient\\CreatureDisplayInfo.dbc"
MODEL_ROW, MIRROR_MODEL_ROW = 901401, 901402
DISPLAY_ROW, MIRROR_DISPLAY_ROW = 977601, 977602
MODEL_NAME_FIELD, DISPLAY_MODEL_FIELD = 2, 1
GEOMETRY_MAGIC, GEOMETRY_PATH = 0x4D474145, 128


def with_cloned_row(dbc, source, clone, change):
    magic, count, fields, size, text_size = struct.unpack_from("<4s4I", dbc, 0)
    if magic != b"WDBC":
        raise ValueError("not a DBC table")
    rows = [list(struct.unpack_from(f"<{fields}I", dbc, 20 + index * size)) for index in range(count)]
    by_id = {row[0]: row for row in rows}
    if clone in by_id:
        rows.remove(by_id.pop(clone))
    if source not in by_id:
        raise ValueError(f"the table has no row {source} to copy")
    texts = bytearray(dbc[20 + count * size:20 + count * size + text_size])
    row = list(by_id[source])
    row[0] = clone
    change(row, texts)
    rows.append(row)
    body = b"".join(struct.pack(f"<{fields}I", *row) for row in rows)
    return struct.pack("<4s4I", b"WDBC", len(rows), fields, size, len(texts)) + body + bytes(texts)


def mirror_model_row(mirror):
    def change(row, texts):
        row[MODEL_NAME_FIELD] = len(texts)
        texts += mirror[:-len(".m2")].encode("latin1") + b".mdx\0"
    return change


def twin_model_path(model_data):
    magic, count, fields, size, text_size = struct.unpack_from("<4s4I", model_data, 0)
    texts = 20 + count * size
    for index in range(count):
        row = struct.unpack_from(f"<{fields}I", model_data, 20 + index * size)
        if row[0] == MODEL_ROW:
            start = texts + row[MODEL_NAME_FIELD]
            name = model_data[start:model_data.index(b"\0", start)].decode("latin1")
            return name[:-len(".mdx")] + ".m2" if name.lower().endswith(".mdx") else name
    raise ValueError(f"the table has no row {MODEL_ROW}")


def mirror_path(model):
    folder, _, stem = model[:-len(".m2")].rpartition("\\")
    stem = stem[:-len("male")] + "mirrormale" if stem.lower().endswith("male") else stem + "mirror"
    return f"{folder}\\{stem}.m2"


def mirror_display_row(row, _):
    row[DISPLAY_MODEL_FIELD] = MIRROR_MODEL_ROW


def geometry_entries(data):
    magic, version, count = struct.unpack_from("<3I", data, 0)
    if magic != GEOMETRY_MAGIC or version != 1:
        raise ValueError("not an Esteria geometry catalog")
    entries, at = [], 12
    for _ in range(count):
        name = data[at:at + GEOMETRY_PATH].split(b"\0")[0].decode("latin1")
        length = struct.unpack_from("<I", data, at + GEOMETRY_PATH)[0]
        entries.append((name, data[at + GEOMETRY_PATH + 4:at + GEOMETRY_PATH + 4 + length]))
        at += GEOMETRY_PATH + 4 + length
    return entries


def with_mirrored_geometry(data, model=MODEL, mirror=MIRROR, skin=None):
    mirrors = {mirror.lower(), MIRROR.lower()}
    entries = [(name, blob) for name, blob in geometry_entries(data) if name.lower() not in mirrors]
    names = {name.lower(): blob for name, blob in entries}
    source = names.get(model.lower(), skin)
    if source is None:
        return None
    entries.append((mirror, mirrored_skin(source)))
    body = b"".join(name.encode("latin1").ljust(GEOMETRY_PATH, b"\0") + struct.pack("<I", len(blob)) + blob
                    for name, blob in entries)
    updated = struct.pack("<3I", GEOMETRY_MAGIC, 1, len(entries)) + body
    return None if updated == data else updated


def client(data, geometry=None, backup_folder=None):
    backup_folder = backup_folder or data
    patches.require_writable([data / patches.MERGED] + ([geometry] if geometry else []))
    archives = patches.below_merged(patches.ordered_archives(data))
    found = patches.Client(archives)
    model_data, _ = found.newest(MODEL_DATA, archives)
    if model_data is None:
        raise SystemExit(f"no archive in Data has {MODEL_DATA}")
    model_path = twin_model_path(model_data)
    mirror = mirror_path(model_path)
    print(f"reading the Thin Twins model {model_path}", flush=True)
    model, source = found.newest(model_path, archives)
    skin, _ = found.newest(patches.skin_name(model_path), archives)
    if model is None or skin is None:
        raise SystemExit(f"no archive in Data has {model_path} and its skin")
    print(f"mirroring it from {source.name} into {mirror}", flush=True)
    additions = {mirror: mirrored_model(model), patches.skin_name(mirror): mirrored_skin(skin)}
    for name, row, clone, change in ((MODEL_DATA, MODEL_ROW, MIRROR_MODEL_ROW, mirror_model_row(mirror)),
                                     (DISPLAY_INFO, DISPLAY_ROW, MIRROR_DISPLAY_ROW, mirror_display_row)):
        table, _ = found.newest(name, archives)
        if table is None:
            raise SystemExit(f"no archive in Data has {name}")
        additions[name] = with_cloned_row(table, row, clone, change)
        print(f"{name}: row {clone} made from row {row}", flush=True)
    found.close()
    print(f"writing {patches.MERGED}", flush=True)
    patches.merge_into_merged(data, additions, backup_folder / (patches.MERGED + ".before-mirror"))
    if geometry and geometry.exists():
        updated = with_mirrored_geometry(geometry.read_bytes(), model_path, mirror, skin)
        if updated:
            backup = backup_folder / (geometry.name + ".before-mirror")
            if not backup.exists():
                backup.write_bytes(geometry.read_bytes())
            geometry.write_bytes(updated)
        print(f"{geometry.name}: {'mirrored entry added' if updated else 'nothing to add'}", flush=True)
    print("done: the brother's mirror model is in patch-ZZZZY.MPQ", flush=True)


def main(arguments):
    if arguments and arguments[0] == "--client":
        client(Path(arguments[1]), Path(arguments[2]) if len(arguments) > 2 else None,
               Path(arguments[3]) if len(arguments) > 3 else None)
        return
    model_path, skin_path, out_model, out_skin = arguments
    Path(out_model).write_bytes(mirrored_model(Path(model_path).read_bytes()))
    Path(out_skin).write_bytes(mirrored_skin(Path(skin_path).read_bytes()))


if __name__ == "__main__":
    main(sys.argv[1:])
