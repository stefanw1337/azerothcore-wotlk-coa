"""Clone CoA Custom's race 32 into our Thin Human (96, male) and race 53 into our Two-Headed Ogre (85, female) in a
set of client or server DBCs. The Two-Headed Ogre gets her own model path so her eyes never reach race 53.
    python mkraces.py <source folder> <output folder>
Every table found in the source folder is written to the output folder with the new rows appended."""
import struct
import sys
from dataclasses import dataclass
from pathlib import Path

from mkeyes import EYE_SETS, eye_texture_name

MALE, FEMALE = 0, 1


@dataclass(frozen=True)
class Race:
    race_id: int
    source: int
    name: str
    sex: int
    model_path: str = ""
    display_id: int = 0
    model_id: int = 0
    camera_distance: float = 1.0
    eye_textures: tuple = ()


THIN_HUMAN = Race(96, 32, "Thin Human", MALE)
TWO_HEADED_OGRE = Race(85, 53, "Two-Headed Ogre", FEMALE, "Character\\Ogre\\Female\\TwoHeadedOgre.mdx", 977600, 901400,
                       1.0, tuple(eye_texture_name(index) for index in range(len(EYE_SETS))))
RACES = (THIN_HUMAN, TWO_HEADED_OGRE)

CHR_DISPLAYS = (4, 5)
CHR_NAMES = (14, 31, 48)
CHR_FACIAL_HAIR = (65, 66)
CHR_HAIR = 67
EYES_CUSTOMIZATION = "EYES"
NO_CUSTOMIZATION = "NONE"
SECTION_HAIR = 3
SECTION_TEXTURE = 4
SECTION_VARIATION = 8
SECTION_COLOR = 9


@dataclass(frozen=True)
class Table:
    file: str
    race: int
    sex: int | None
    numbered: bool = True


ROW_TABLES = (
    Table("CharSections.dbc", 1, 2),
    Table("CharHairGeosets.dbc", 1, 2),
    Table("CharacterFacialHairStyles.dbc", 0, 1, numbered=False),
    Table("NameGen.dbc", 2, 3),
    Table("EmotesTextSound.dbc", 2, 3),
    Table("HDEmotesTextSound.dbc", 2, 3),
    Table("UICameraAppearanceChrRaces.dbc", 1, 2),
    Table("CharacterCreationClassDetails.dbc", 2, None),
    Table("CharacterCreationArchetypeDetails.dbc", 2, None),
    Table("CharacterCreationShapeshiftDetails.dbc", 2, None),
)


class Dbc:
    def __init__(self, path):
        data = Path(path).read_bytes()
        _, count, self.fields, self.size, string_size = struct.unpack_from("<4s4I", data)
        self.rows = [bytearray(data[20 + i * self.size:20 + (i + 1) * self.size]) for i in range(count)]
        self.strings = bytearray(data[20 + count * self.size:])

    def value(self, row, field):
        return struct.unpack_from("<I", row, field * 4)[0]

    def set(self, row, field, value):
        struct.pack_into("<I", row, field * 4, value)

    def text(self, value):
        offset = len(self.strings)
        self.strings += value.encode("utf-8") + b"\0"
        return offset

    def next_id(self):
        return max(self.value(row, 0) for row in self.rows) + 1

    def save(self, path):
        with open(path, "wb") as out:
            out.write(struct.pack("<4s4I", b"WDBC", len(self.rows), self.fields, self.size, len(self.strings)))
            for row in self.rows:
                out.write(row)
            out.write(self.strings)


def clone_rows(dbc, table):
    ours = {race.race_id for race in RACES}
    dbc.rows = [row for row in dbc.rows if dbc.value(row, table.race) not in ours]
    added = []
    next_id = dbc.next_id() if table.numbered else 0
    for race in RACES:
        for row in list(dbc.rows):
            if dbc.value(row, table.race) != race.source:
                continue
            if table.sex is not None and dbc.value(row, table.sex) != race.sex:
                continue
            copy = bytearray(row)
            dbc.set(copy, table.race, race.race_id)
            if table.numbered:
                dbc.set(copy, 0, next_id)
                next_id += 1
            added.append(copy)
    dbc.rows += added
    return len(added)


def eye_sections(dbc):
    added = 0
    for race in RACES:
        if not race.eye_textures:
            continue
        hair = [row for row in dbc.rows if dbc.value(row, 1) == race.race_id and dbc.value(row, 2) == race.sex and
                dbc.value(row, 3) == SECTION_HAIR]
        template = hair[0]
        dbc.rows = [row for row in dbc.rows if not any(row is other for other in hair)]
        next_id = dbc.next_id()
        for color, texture in enumerate(race.eye_textures):
            row = bytearray(template)
            dbc.set(row, 0, next_id + color)
            dbc.set(row, SECTION_TEXTURE, dbc.text(texture))
            dbc.set(row, SECTION_VARIATION, 0)
            dbc.set(row, SECTION_COLOR, color)
            dbc.rows.append(row)
            added += 1
    return added


def clone_outfits(dbc):
    ours = {race.race_id for race in RACES}
    dbc.rows = [row for row in dbc.rows if row[4] not in ours]
    added = []
    next_id = dbc.next_id()
    for race in RACES:
        for row in list(dbc.rows):
            if row[4] != race.source or row[6] != race.sex:
                continue
            copy = bytearray(row)
            copy[4] = race.race_id
            dbc.set(copy, 0, next_id)
            next_id += 1
            added.append(copy)
    dbc.rows += added
    return len(added)


def clone_base_info(path, out):
    data = Path(path).read_bytes()
    _, count, fields, size, string_size = struct.unpack_from("<4s4I", data)
    rows = [data[20 + i * size:20 + (i + 1) * size] for i in range(count)]
    ours = {race.race_id for race in RACES}
    rows = [row for row in rows if row[0] not in ours]
    added = [bytes([race.race_id]) + row[1:] for race in RACES for row in rows if row[0] == race.source]
    rows += added
    with open(out, "wb") as target:
        target.write(struct.pack("<4s4I", b"WDBC", len(rows), fields, size, string_size))
        target.write(b"".join(rows))
        target.write(data[20 + count * size:])
    return len(added)


def own_displays(source):
    models = Dbc(source / "CreatureModelData.dbc")
    displays = Dbc(source / "CreatureDisplayInfo.dbc")
    races = Dbc(source / "ChrRaces.dbc")
    by_id = {races.value(row, 0): row for row in races.rows}
    display_rows = {displays.value(row, 0): row for row in displays.rows}
    model_rows = {models.value(row, 0): row for row in models.rows}
    chosen = {}
    for race in RACES:
        display = races.value(by_id[race.source], CHR_DISPLAYS[race.sex])
        if not race.model_path:
            chosen[race.race_id] = display
            continue
        models.rows = [row for row in models.rows if models.value(row, 0) != race.model_id]
        displays.rows = [row for row in displays.rows if displays.value(row, 0) != race.display_id]
        model = bytearray(model_rows[displays.value(display_rows[display], 1)])
        models.set(model, 0, race.model_id)
        models.set(model, 2, models.text(race.model_path))
        models.rows.append(model)
        copy = bytearray(display_rows[display])
        displays.set(copy, 0, race.display_id)
        displays.set(copy, 1, race.model_id)
        displays.rows.append(copy)
        chosen[race.race_id] = race.display_id
    return models, displays, chosen


def clone_races(dbc, displays):
    rows = {dbc.value(row, 0): row for row in dbc.rows}
    for race in RACES:
        copy = bytearray(rows[race.source])
        dbc.set(copy, 0, race.race_id)
        for field in CHR_DISPLAYS:
            dbc.set(copy, field, displays[race.race_id])
        name = dbc.text(race.name)
        for group in CHR_NAMES:
            dbc.set(copy, group, name)
        if race.eye_textures:
            dbc.set(copy, CHR_HAIR, dbc.text(EYES_CUSTOMIZATION))
            dbc.set(copy, CHR_FACIAL_HAIR[race.sex], dbc.text(NO_CUSTOMIZATION))
        dbc.rows = [row for row in dbc.rows if dbc.value(row, 0) != race.race_id] + [copy]


CAMERA_DISTANCES = (1, 5)
APPEARANCE_CAMERA = 4


def push_cameras(cameras, appearances):
    by_id = {cameras.value(row, 0): row for row in cameras.rows}
    next_id = cameras.next_id()
    for race in RACES:
        if race.camera_distance == 1.0:
            continue
        made = {}
        for row in appearances.rows:
            if appearances.value(row, 1) != race.race_id:
                continue
            source = appearances.value(row, APPEARANCE_CAMERA)
            if source not in made:
                copy = bytearray(by_id[source])
                cameras.set(copy, 0, next_id)
                for field in CAMERA_DISTANCES:
                    distance = struct.unpack_from("<f", copy, field * 4)[0] * race.camera_distance
                    struct.pack_into("<f", copy, field * 4, distance)
                cameras.rows.append(copy)
                made[source] = next_id
                next_id += 1
            appearances.set(row, APPEARANCE_CAMERA, made[source])
    return next_id


def main(source, output):
    source, output = Path(source), Path(output)
    output.mkdir(parents=True, exist_ok=True)
    displays = {}
    if (source / "CreatureModelData.dbc").exists() and (source / "CreatureDisplayInfo.dbc").exists():
        models, display_info, displays = own_displays(source)
        models.save(output / "CreatureModelData.dbc")
        display_info.save(output / "CreatureDisplayInfo.dbc")
        print("displays", displays)
    if (source / "ChrRaces.dbc").exists():
        races = Dbc(source / "ChrRaces.dbc")
        if not displays:
            rows = {races.value(row, 0): row for row in races.rows}
            displays = {race.race_id: races.value(rows[race.source], CHR_DISPLAYS[race.sex]) for race in RACES}
        clone_races(races, displays)
        races.save(output / "ChrRaces.dbc")
    for table in ROW_TABLES:
        if (source / table.file).exists():
            dbc = Dbc(source / table.file)
            print(table.file, clone_rows(dbc, table))
            if table.file == "CharSections.dbc":
                print("eye colours", eye_sections(dbc))
            dbc.save(output / table.file)
    if (source / "UICameras.dbc").exists() and (output / "UICameraAppearanceChrRaces.dbc").exists():
        cameras = Dbc(source / "UICameras.dbc")
        appearances = Dbc(output / "UICameraAppearanceChrRaces.dbc")
        print("UICameras.dbc up to", push_cameras(cameras, appearances))
        cameras.save(output / "UICameras.dbc")
        appearances.save(output / "UICameraAppearanceChrRaces.dbc")
    if (source / "CharStartOutfit.dbc").exists():
        dbc = Dbc(source / "CharStartOutfit.dbc")
        print("CharStartOutfit.dbc", clone_outfits(dbc))
        dbc.save(output / "CharStartOutfit.dbc")
    if (source / "CharBaseInfo.dbc").exists():
        print("CharBaseInfo.dbc", clone_base_info(source / "CharBaseInfo.dbc", output / "CharBaseInfo.dbc"))


if __name__ == "__main__":
    main(*sys.argv[1:3])
