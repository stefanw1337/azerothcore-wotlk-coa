import argparse
import json
import re
import shutil
import struct
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

from mkmergepatch import LISTFILE, names_in, verified, written  # noqa: E402
from mpqpull import Archive  # noqa: E402

PATCH = "patch-ZZZZZZ-HOTS.MPQ"
FIRST_ID, LAST_ID = 986001, 986999
FIRST_SPELL, LAST_SPELL = 9860001, 9860999
MARK = "Heroes of the Storm"
TEMPLATE_MODELS = ("creature\\ridinghorse\\ridinghorse.mdx", "creature\\horse\\horse.mdx")
TEMPLATE_CREATURE = 14561
TEMPLATE_SPELL = 23229
MODEL_FIELDS = "iisif" "iifff" "iiii" "fff" "ffffff" "fffff"
DISPLAY_FIELDS = "iiiifissssiiiiii"
SPELL_FIELDS = ("iiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiifiiiiiiiiiiiiiiiiiiiiiiiiiiiiifffiiiiiiiiiiiiiiiiiiii"
                "ifffiiiiiiiiiiiiiiifffiiiiiiiiiiiiiissssssssssssssssissssssssssssssssissssssssssssssssisssssssssssss"
                "sssiiiiiiiiiiiiifffiiiiiiiiiifffii")
SPELL_NAME, SPELL_SUBTEXT, SPELL_DESCRIPTION, SPELL_MISC, SPELL_ICON = 136, 153, 170, 110, 133
SKILL_MOUNTS = 777
HORSE_MODEL = "creature\\ridinghorse\\ridinghorse.mdx"
FAMILIES = (
    (("unicorn", "rainbow", "celestialhorse"), 1176, 10088, HORSE_MODEL, 23229),
    (("dead", "ghost", "headless", "haunted", "invisible"), 1241, 13911,
     "creature\\undeadhorse\\ridingundeadhorse.mdx", 17465),
    (("demon", "hell", "fel", "nightmare"), 1179, 18762,
     "creature\\headlesshorsemanmount\\headlesshorsemanmount.mdx", 23161),
    (("charger", "judgement", "tyrael", "warhorse", "malthael"), 1716, 24116, HORSE_MODEL, 23214),
    (("horse", "hare", "nazeebra", "steed"), 1176, 10088, HORSE_MODEL, 23229),
    (("lion", "tiger", "cat", "felstalker"), 916, 13757, "creature\\frostsabre\\ridingfrostsabre.mdx", 23219),
    (("wolf", "dog", "cerberus", "zergling"), 956, 12696, "creature\\direwolf\\ridingdirewolf.mdx", 23250),
    (("raptor", "triceratops", "spider"), 1180, 13723, "creature\\ridingraptor\\ridingraptor.mdx", 23241),
    (("reigndeer", "deer", "antelope"), 1177, 13628, "creature\\reindeer\\reindeermount.mdx", 25859),
    (("ram", "goat", "billie", "sheep"), 1177, 13628, "creature\\ram\\ridingram.mdx", 23238),
    (("bear",), 3497, 49268, "creature\\bearmount\\bearmount.mdx", 59572),
    (("turtle", "crab"), 1682, 23394, "creature\\ridingturtle\\ridingturtle.mdx", 30174),
    (("pig", "beast", "mouse", "raccoon"), 1682, 23394, "creature\\kodobeast\\ridingkodo.mdx", 23247),
    (("dragon", "snake"), 3442, 48932, "creature\\drakemount\\drakemount.mdx", 59569),
    (("rooster", "duck"), 2054, 51838, "creature\\chickenmount\\chickenmount.mdx", 66122),
    (("phoenix", "vulture", "raven", "owl"), 2054, 51838, "creature\\gryphon\\gryphon_mount.mdx", 32242),
    (("rocket", "train"), 3208, 45793, "creature\\rocketmount\\rocketmount.mdx", 55531),
    (("bike", "speeder", "wheel", "wagon"), 1240, 13910,
     "creature\\motorcyclevehicle\\motorcyclevehicle.mdx", 60424),
    (("reaver", "chariot", "starbreaker", "mecha", "cyber", "bottle"), 1240, 13910,
     "creature\\mechastrider\\mechastrider.mdx", 23222),
    (("carpet", "cloud", "nexagon", "surf", "board", "banner", "eye", "heart", "card", "doubloon", "dollar",
      "snowflake", "goblin"), 3833, 51378, "creature\\flyingcarpetmount\\flyingcarpetmount.mdx", 61451),
)
DEFAULT_FAMILY = ((), 1176, 10088, HORSE_MODEL, 23229)
STANDING_FAMILY = FAMILIES[-1]
SPELL_VISUALS = (131, 132)
COA_COLLECTION_TABLE = ["""CREATE TABLE IF NOT EXISTS `coa_client_vanity_collection_added` (
  `ID` INT UNSIGNED NOT NULL,
  `ItemID` INT UNSIGNED NOT NULL,
  `TemplateItemID` INT UNSIGNED NOT NULL,
  `CreatureID` INT UNSIGNED NOT NULL DEFAULT 0,
  `LearnedSpell` INT UNSIGNED NOT NULL DEFAULT 0,
  `Description` VARCHAR(255) NOT NULL DEFAULT '',
  `AdditionalText` VARCHAR(255) NOT NULL DEFAULT '',
  PRIMARY KEY (`ID`),
  UNIQUE KEY `ItemID` (`ItemID`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;"""]
TEMPLATE_ITEM = 13335
ITEM_QUALITY = {"Common": 2, "Rare": 3, "Epic": 4, "Legendary": 5}
SKILL_FIELDS = "iiiiiiiiiiiiii"
ITEM_FIELDS = "iiiiiiii"
CLIENT_TABLES = {
    "Spell.dbc": (SPELL_FIELDS, FIRST_SPELL, LAST_SPELL),
    "CreatureModelData.dbc": (MODEL_FIELDS, FIRST_ID, LAST_ID),
    "CreatureDisplayInfo.dbc": (DISPLAY_FIELDS, FIRST_ID, LAST_ID),
    "SkillLineAbility.dbc": (SKILL_FIELDS, FIRST_ID, LAST_ID),
    "Item.dbc": (ITEM_FIELDS, FIRST_ID, LAST_ID),
}


def log(text):
    print(text, flush=True)


class Dbc:
    def __init__(self, data):
        magic, count, self.fields, self.size, text_size = struct.unpack_from("<4s4I", data, 0)
        if magic != b"WDBC":
            raise ValueError("not a DBC table")
        self.data = data
        self.count = count
        self.texts_at = 20 + count * self.size
        self.texts = data[self.texts_at:self.texts_at + text_size]

    def row(self, index):
        return self.data[20 + index * self.size:20 + (index + 1) * self.size]

    def rows(self):
        return (self.row(i) for i in range(self.count))

    def get(self, row, field):
        return struct.unpack_from("<I", row, field * 4)[0]

    def string(self, row, field):
        start = self.get(row, field)
        end = self.texts.index(b"\0", start)
        return self.texts[start:end].decode("utf-8", "replace")

    def values(self, row, kinds):
        out = []
        for field, kind in enumerate(kinds):
            if kind == "f":
                out.append(struct.unpack_from("<f", row, field * 4)[0])
            elif kind == "s":
                out.append(self.string(row, field))
            else:
                out.append(struct.unpack_from("<i", row, field * 4)[0])
        return out


def merged_table(data, kinds, first, last, rows):
    magic, count, fields, size, text_size = struct.unpack_from("<4s4I", data, 0)
    if magic != b"WDBC" or fields != len(kinds) or size != 4 * fields:
        raise SystemExit(f"unexpected table layout ({fields} fields, {size} bytes a row)")
    kept = [data[20 + i * size:20 + (i + 1) * size] for i in range(count)]
    kept = [r for r in kept if not first <= struct.unpack_from("<I", r, 0)[0] <= last]
    texts = bytearray(data[20 + count * size:20 + count * size + text_size] or b"\0")
    offsets = {}

    def text(value):
        if not value:
            return 0
        if value not in offsets:
            offsets[value] = len(texts)
            texts.extend(value.encode("utf-8") + b"\0")
        return offsets[value]

    for values in rows:
        packed = b""
        for kind, value in zip(kinds, values):
            if kind == "f":
                packed += struct.pack("<f", float(value or 0))
            elif kind == "s":
                packed += struct.pack("<I", text(value or ""))
            else:
                packed += struct.pack("<I", int(value or 0) & 0xFFFFFFFF)
        kept.append(packed)
    return struct.pack("<4s4I", magic, len(kept), fields, size, len(texts)) + b"".join(kept) + bytes(texts)


def server_table(dbc_folder, name):
    path = dbc_folder / name
    if path.exists():
        return Dbc(path.read_bytes()), path
    raise SystemExit(f"{name} not found in {dbc_folder}")


def templates(server):
    models, origin = server_table(server, "CreatureModelData.dbc")
    by_name = {}
    for row in models.rows():
        by_name.setdefault(models.string(row, 2).lower(), row)
    model = next((by_name[name] for name in TEMPLATE_MODELS if name in by_name), None)
    if model is None:
        raise SystemExit(f"{origin} has no horse model to copy settings from")
    family_models = {}
    for family in FAMILIES:
        row = by_name.get(family[3])
        if row is not None:
            family_models[family[3]] = models.values(row, MODEL_FIELDS)
    missing = sorted({family[3] for family in FAMILIES} - set(family_models))
    if missing:
        log(f"no {', '.join(missing)} on this server: those mounts use the horse's sounds")
    model_id = models.get(model, 0)
    displays, _ = server_table(server, "CreatureDisplayInfo.dbc")
    using = [row for row in displays.rows() if displays.get(row, 1) == model_id]
    display = next((row for row in using if displays.get(row, 2)), using[0] if using else None)
    log(f"model settings copied from {models.string(model, 2)}")
    log("reading the server's Spell.dbc for the mount spell template ...")
    spells = Dbc((server / "Spell.dbc").read_bytes())
    if spells.fields != len(SPELL_FIELDS):
        raise SystemExit(f"Spell.dbc has {spells.fields} fields, spell_dbc needs {len(SPELL_FIELDS)}")
    spell, taken, visuals = None, [], {}
    wanted = {family[4] for family in FAMILIES}
    for row in spells.rows():
        sid = spells.get(row, 0)
        if sid in wanted:
            visuals[sid] = tuple(spells.get(row, field) for field in SPELL_VISUALS)
        if sid == TEMPLATE_SPELL:
            spell = row
        elif FIRST_SPELL <= sid <= LAST_SPELL:
            taken.append(sid)
    if spell is None:
        raise SystemExit(f"Spell.dbc has no spell {TEMPLATE_SPELL} to copy")
    if taken:
        raise SystemExit(f"Spell.dbc already uses spell ids {taken[:5]} in our range; nothing written")
    spell_values = spells.values(spell, SPELL_FIELDS)
    log(f"spell settings copied from {spell_values[SPELL_NAME]} ({TEMPLATE_SPELL}), creature {spell_values[SPELL_MISC]}")
    return (models.values(model, MODEL_FIELDS),
            displays.values(display, DISPLAY_FIELDS) if display is not None else [0] * len(DISPLAY_FIELDS),
            spell_values, family_models, visuals)


def sql_value(value):
    if value is None:
        return "NULL"
    if isinstance(value, str):
        return "'" + value.replace("\\", "\\\\").replace("'", "''") + "'"
    if isinstance(value, float):
        return repr(value) if value == value and abs(value) != float("inf") else "0"
    return str(value)


def row_sql(table, values):
    return f"INSERT INTO `{table}` VALUES ({', '.join(sql_value(v) for v in values)});"


def assign_ids(mounts):
    model_id = display_id = FIRST_ID
    spell_id = FIRST_SPELL
    for mount in mounts:
        mount["model_id"] = model_id
        model_id += 1
        for display in mount["displays"]:
            display["id"] = display["creature"] = display_id
            display["spell"] = spell_id
            display["ability"] = FIRST_ID + spell_id - FIRST_SPELL
            display_id += 1
            spell_id += 1
    if max(model_id, display_id) - 1 > LAST_ID or spell_id - 1 > LAST_SPELL:
        raise SystemExit("more mounts than the id ranges hold")


def mount_family(mount, display):
    words = (mount["name"] + " " + display["name"]).lower()
    return next((family for family in FAMILIES if any(word in words for word in family[0])), DEFAULT_FAMILY)


def world_sql(mounts, model_template, display_template, spell_template, family_models, visuals, coa_stream,
              client_rows=None):
    client_rows = {} if client_rows is None else client_rows
    keep = lambda table, values: client_rows.setdefault(table, []).append(list(values))
    template_creature = spell_template[SPELL_MISC]
    lines = [f"-- Heroes of the Storm mounts: {sum(len(m['displays']) for m in mounts)} mounts on {len(mounts)} models.",
             "-- AzerothCore reads these at start: restart the worldserver after applying.",
             f"DELETE FROM `creaturemodeldata_dbc` WHERE `ID` BETWEEN {FIRST_ID} AND {LAST_ID};",
             f"DELETE FROM `creaturedisplayinfo_dbc` WHERE `ID` BETWEEN {FIRST_ID} AND {LAST_ID};",
             f"DELETE FROM `creature_template_model` WHERE `CreatureID` BETWEEN {FIRST_ID} AND {LAST_ID};",
             f"DELETE FROM `creature_template` WHERE `entry` BETWEEN {FIRST_ID} AND {LAST_ID};",
             f"DELETE FROM `spell_dbc` WHERE `ID` BETWEEN {FIRST_SPELL} AND {LAST_SPELL};",
             f"DELETE FROM `skilllineability_dbc` WHERE `ID` BETWEEN {FIRST_ID} AND {LAST_ID};",
             f"DELETE FROM `creature_model_info` WHERE `DisplayID` BETWEEN {FIRST_ID} AND {LAST_ID};",
             f"DELETE FROM `item_template` WHERE `entry` BETWEEN {FIRST_ID} AND {LAST_ID};",
             f"DELETE FROM `item_dbc` WHERE `ID` BETWEEN {FIRST_ID} AND {LAST_ID};",
             *(COA_COLLECTION_TABLE + [
                 f"DELETE FROM `coa_client_skill_line_ability` WHERE `ID` BETWEEN {FIRST_ID} AND {LAST_ID};",
                 f"DELETE FROM `coa_client_vanity_collection_added` WHERE `ID` BETWEEN {FIRST_SPELL} AND {LAST_SPELL};"]
               if coa_stream else []),
             "DROP TEMPORARY TABLE IF EXISTS `hots_mount_creature`;",
             f"CREATE TEMPORARY TABLE `hots_mount_creature` SELECT * FROM `creature_template` WHERE `entry` = {template_creature};",
             "DROP TEMPORARY TABLE IF EXISTS `hots_mount_item`;",
             f"CREATE TEMPORARY TABLE `hots_mount_item` SELECT * FROM `item_template` WHERE `entry` = {TEMPLATE_ITEM};"]
    for mount in mounts:
        family = mount_family(mount, mount["displays"][0])
        row = list(family_models.get(family[3], model_template))
        row[0], row[2], row[4] = mount["model_id"], mount["model"][:-len(".m2")] + ".mdx", 1.0
        row[17:23] = mount["bounds"]["minimum"] + mount["bounds"]["maximum"]
        lines.append(row_sql("creaturemodeldata_dbc", row))
        keep("CreatureModelData.dbc", row)
        for display in mount["displays"]:
            row = list(display_template)
            row[0], row[1], row[4], row[5] = display["id"], mount["model_id"], 1.0, 255
            row[6:10] = list(display["textures"][:3]) + [""]
            lines.append(row_sql("creaturedisplayinfo_dbc", row) + f" -- {display['name']}")
            keep("CreatureDisplayInfo.dbc", row)
            lines.append(f"UPDATE `hots_mount_creature` SET `entry` = {display['creature']}, "
                         f"`name` = {sql_value(display['name'])}, `subname` = {sql_value(MARK)};")
            lines.append("INSERT INTO `creature_template` SELECT * FROM `hots_mount_creature`;")
            lines.append("INSERT INTO `creature_template_model` (`CreatureID`, `Idx`, `CreatureDisplayID`, "
                         f"`DisplayScale`, `Probability`) VALUES ({display['creature']}, 0, {display['id']}, 1, 1);")
            spell = list(spell_template)
            spell[0] = display["spell"]
            spell[SPELL_NAME] = display["name"]
            spell[SPELL_SUBTEXT] = MARK
            spell[SPELL_DESCRIPTION] = (f"Summons and dismisses a rideable {display['name']} from Heroes of the "
                                        "Storm. This is a very fast mount.")
            spell[SPELL_MISC] = display["creature"]
            family = mount_family(mount, display)
            spell_icon, item_icon = family[1], family[2]
            spell[SPELL_ICON] = spell_icon
            pose = STANDING_FAMILY if mount.get("category") == "Ridesurf" else family
            for field, value in zip(SPELL_VISUALS, visuals.get(pose[4], ())):
                spell[field] = value
            lines.append(row_sql("spell_dbc", spell))
            keep("Spell.dbc", spell)
            radius = max(0.3, min(1.5, mount["bounds"]["radius"] * 0.4))
            lines.append("INSERT INTO `creature_model_info` (`DisplayID`, `BoundingRadius`, `CombatReach`, `Gender`) "
                         f"VALUES ({display['id']}, {radius:.3f}, 1.5, 2);")
            quality = ITEM_QUALITY.get(display.get("rarity") or "", 4)
            lines.append(f"UPDATE `hots_mount_item` SET `entry` = {display['creature']}, "
                         f"`name` = {sql_value(display['name'])}, `displayid` = {item_icon}, `Quality` = {quality}, "
                         f"`spellid_2` = {display['spell']}, `description` = "
                         f"{sql_value('Teaches you how to summon this mount from Heroes of the Storm. This is a very fast mount.')};")
            lines.append("INSERT INTO `item_template` SELECT * FROM `hots_mount_item`;")
            lines.append("INSERT INTO `item_dbc` (`ID`, `ClassID`, `SubclassID`, `Sound_Override_Subclassid`, `Material`, "
                         f"`DisplayInfoID`, `InventoryType`, `SheatheType`) VALUES ({display['creature']}, 15, 5, -1, -1, "
                         f"{item_icon}, 0, 0);")
            keep("Item.dbc", [display["creature"], 15, 5, -1, -1, item_icon, 0, 0])
            if coa_stream:
                facts = ", ".join(x for x in (display.get("collection"), display.get("rarity")) if x)
                lines.append("INSERT INTO `coa_client_vanity_collection_added` (`ID`, `ItemID`, `TemplateItemID`, "
                             "`CreatureID`, `LearnedSpell`, `Description`, `AdditionalText`) VALUES ("
                             f"{display['spell']}, {display['creature']}, {TEMPLATE_ITEM}, {display['creature']}, "
                             f"{display['spell']}, {sql_value(MARK + (f' ({facts})' if facts else ''))}, '');")
            lines.append("INSERT INTO `skilllineability_dbc` (`ID`, `SkillLine`, `Spell`, `MinSkillLineRank`) "
                         f"VALUES ({display['ability']}, {SKILL_MOUNTS}, {display['spell']}, 1);")
            keep("SkillLineAbility.dbc", [display["ability"], SKILL_MOUNTS, display["spell"], 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0])
            if coa_stream:
                lines.append("INSERT INTO `coa_client_skill_line_ability` (`ID`, `SkillLine`) "
                             f"VALUES ({display['ability']}, {SKILL_MOUNTS});")
    lines.append("DROP TEMPORARY TABLE IF EXISTS `hots_mount_creature`;")
    lines.append("DROP TEMPORARY TABLE IF EXISTS `hots_mount_item`;")
    return "\n".join(lines) + "\n"


class World:
    def __init__(self, conf, client):
        text = Path(conf).read_text("latin1")
        match = re.search(r'^\s*WorldDatabaseInfo\s*=\s*"([^"]*)"', text, re.M)
        if not match:
            raise SystemExit(f"WorldDatabaseInfo not found in {conf}")
        self.login = match.group(1).split(";")[:5]
        self.client = Path(client) if client else None
        if self.client is None:
            found = shutil.which("mysql") or shutil.which("mariadb")
            if not found:
                raise SystemExit("no MySQL client on PATH; pass --mysql <path to mysql.exe>")
            self.client = Path(found)


def mysql(server, sql, query=False):
    host, port, user, password, database = server.login
    command = [str(server.client), f"--host={host}", f"--port={port}", f"--user={user}",
               f"--password={password}", "--default-character-set=utf8mb4", database]
    if query:
        command[1:1] = ["--batch", "--skip-column-names"]
    result = subprocess.run(command, input=sql.encode("utf-8"), capture_output=True)
    if result.returncode:
        raise SystemExit("MySQL failed (is the database running?):\n" + result.stderr.decode("utf-8", "replace"))
    return result.stdout.decode("utf-8", "replace")


def check_free(server):
    checks = [
        ("creature_template", f"SELECT `entry` FROM `creature_template` WHERE `entry` BETWEEN {FIRST_ID} AND {LAST_ID} "
                              f"AND IFNULL(`subname`, '') <> {sql_value(MARK)} LIMIT 5;"),
        ("spell_dbc", f"SELECT `ID` FROM `spell_dbc` WHERE `ID` BETWEEN {FIRST_SPELL} AND {LAST_SPELL} "
                      f"AND IFNULL(`NameSubtext_Lang_enUS`, '') <> {sql_value(MARK)} LIMIT 5;"),
        ("skilllineability_dbc", f"SELECT `ID` FROM `skilllineability_dbc` WHERE `ID` BETWEEN {FIRST_ID} AND {LAST_ID} "
                                 f"AND NOT (`SkillLine` = {SKILL_MOUNTS} AND `Spell` BETWEEN {FIRST_SPELL} AND {LAST_SPELL}) "
                                 "LIMIT 5;"),
        ("item_template", f"SELECT `entry` FROM `item_template` WHERE `entry` BETWEEN {FIRST_ID} AND {LAST_ID} "
                          "AND IFNULL(`description`, '') NOT LIKE '%Heroes of the Storm%' LIMIT 5;"),
        ("creaturemodeldata_dbc", f"SELECT `ID` FROM `creaturemodeldata_dbc` WHERE `ID` BETWEEN {FIRST_ID} AND {LAST_ID} "
                                  "AND LOWER(IFNULL(`ModelName`, '')) NOT LIKE 'creature%hotsmounts%' LIMIT 5;"),
    ]
    for table, query in checks:
        taken = mysql(server, query, query=True).split()
        if taken:
            raise SystemExit(f"{table} already has rows {taken} in our id range that are not ours; nothing written")
    template = mysql(server, f"SELECT COUNT(*) FROM `creature_template` WHERE `entry` = {TEMPLATE_CREATURE};",
                     query=True).split()
    if template != ["1"]:
        raise SystemExit(f"creature_template has no entry {TEMPLATE_CREATURE} (Swift Brown Steed) to copy; nothing written")
    template = mysql(server, f"SELECT COUNT(*) FROM `item_template` WHERE `entry` = {TEMPLATE_ITEM};", query=True).split()
    if template != ["1"]:
        raise SystemExit(f"item_template has no entry {TEMPLATE_ITEM} (Deathcharger's Reins) to copy; nothing written")
    log("server: our id ranges are free (or hold only our own rows)")


def has_coa_stream(server):
    return bool(mysql(server, "SHOW TABLES LIKE 'coa_client_skill_line_ability';", query=True).strip())


def install_addons(target):
    source = HERE / "AddOns"
    target.mkdir(parents=True, exist_ok=True)
    for folder in sorted(source.iterdir()):
        destination = target / folder.name
        if destination.exists():
            shutil.rmtree(destination)
        shutil.copytree(folder, destination)
        log(f"client: addon {folder.name} -> {destination}")


def package_archive():
    whole = HERE / PATCH
    if whole.exists():
        return whole.read_bytes()
    parts = sorted(HERE.glob(PATCH + ".part*"))
    if not parts:
        raise SystemExit(f"no {PATCH} (or {PATCH}.part*) next to this script")
    blob = bytearray()
    for part in parts:
        log(f"client: joining {part.name} ({part.stat().st_size // (1024 * 1024)} MB)")
        blob += part.read_bytes()
    return bytes(blob)


def client_archives(data, locale, skip):
    def rank(path):
        stem = path.stem.lower()
        return (0, stem) if stem.startswith("patch") else (-1, stem)
    found = []
    for folder in (data, data / locale):
        if folder.is_dir():
            mpqs = [p for p in folder.iterdir() if p.suffix.lower() == ".mpq" and p.name.lower() != skip.lower()]
            found += sorted(mpqs, key=rank, reverse=True)
    return found


def client_table(data, locale, skip, name):
    for path in client_archives(data, locale, skip):
        try:
            archive = Archive(str(path))
        except (OSError, struct.error, ValueError):
            continue
        try:
            blob = archive.extract("DBFilesClient\\" + name)
        finally:
            archive.close()
        if blob:
            return blob, path.name
    raise SystemExit(f"DBFilesClient\\{name} not found in the client's MPQs")


def client(data, patch_name, client_rows=None, locale="enUS"):
    blob = package_archive()
    staged = data / (patch_name + ".joining")
    staged.write_bytes(blob)
    archive = Archive(str(staged))
    try:
        names = names_in(archive, staged)
        entries = []
        for name in names.values():
            payload = archive.extract(name)
            if payload is None:
                raise SystemExit(f"{PATCH}: {name} does not read back; nothing installed")
            if name != LISTFILE:
                entries.append((name, payload))
    finally:
        archive.close()
    if client_rows is not None:
        for table, (kinds, first, last) in CLIENT_TABLES.items():
            original, source = client_table(data, locale, patch_name, table)
            merged = merged_table(original, kinds, first, last, client_rows.get(table, []))
            entries.append(("DBFilesClient\\" + table, merged))
            log(f"client: {table} from {source} + {len(client_rows.get(table, []))} rows")
        listing = "\r\n".join(sorted(name for name, _ in entries)).encode("latin1")
        entries = sorted(entries + [(LISTFILE, listing)], key=lambda e: e[0].lower())
        staged.write_bytes(written(entries))
        verified(staged, entries)
    staged.replace(data / patch_name)
    log(f"client: {patch_name} installed, {len(entries)} files read back intact")


def main():
    parser = argparse.ArgumentParser(description="Install the HotS mounts on a WoW 3.3.5a client and an AzerothCore server")
    parser.add_argument("--target", choices=("coa", "335"), required=True)
    parser.add_argument("--client", type=Path, required=True)
    parser.add_argument("--world-conf", type=Path, required=True)
    parser.add_argument("--server-dbc", type=Path, required=True)
    parser.add_argument("--mysql", type=Path)
    parser.add_argument("--patch-name")
    parser.add_argument("--locale", default="enUS")
    args = parser.parse_args()
    patch_name = args.patch_name or (PATCH if args.target == "coa" else "patch-H.MPQ")
    target = args.client / patch_name
    if target.exists():
        try:
            target.replace(target.with_name(target.name + ".checking"))
            target.with_name(target.name + ".checking").replace(target)
        except PermissionError:
            raise SystemExit(f"{target.name} is in use (the game or an MPQ tool); close it and run this again")
    server = World(args.world_conf, args.mysql)
    mounts = json.loads((HERE / "mounts.json").read_text())
    assign_ids(mounts)
    log(f"{sum(len(m['displays']) for m in mounts)} mounts on {len(mounts)} models")
    log("=== server: checking our id ranges ===")
    check_free(server)
    coa_stream = has_coa_stream(server)
    if args.target == "coa" and not coa_stream:
        raise SystemExit("--target coa needs the CoA patch stream tables (coa_client_skill_line_ability); "
                         "use --target 335 for a server without them")
    client_rows = {}
    sql = world_sql(mounts, *templates(args.server_dbc), coa_stream, client_rows)
    log("=== client ===")
    client(args.client, patch_name, client_rows if args.target == "335" else None, args.locale)
    install_addons(args.client.parent / "Interface" / "AddOns")
    log("=== server ===")
    (HERE / "hotsmounts-world.sql").write_text(sql, "utf-8")
    log(f"server: {HERE / 'hotsmounts-world.sql'} written ({sql.count(chr(10))} lines)")
    mysql(server, sql)
    log("server: SQL applied")
    listing = ["spell\tdisplay\tname"] + [f"{d['spell']}\t{d['id']}\t{d['name']}" for m in mounts for d in m["displays"]]
    (HERE / "hotsmounts-ids.txt").write_text("\n".join(listing) + "\n", "utf-8")
    log("\nRestart the worldserver, then start the game. /cj opens the journal; /hotsmounts learn (GM) teaches them all.")


if __name__ == "__main__":
    main()
