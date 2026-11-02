import struct
import tempfile
import unittest
from pathlib import Path

import build_vault_patches as vault
import patch_handspring_client as handspring
from mkmergepatch import written
from mpqpull import Archive
from test_handspring_client import synthetic_model, table

HUMAN = "Character\\Human\\Male\\HumanMale.m2"
OWN_FILE = "Interface\\Glues\\Twins.txt"
ANIMATIONS, KITS = vault.TABLES


def archive(path, files):
    listing = "\r\n".join(sorted(files)).encode("latin1")
    entries = sorted(list(files.items()) + [("(listfile)", listing)], key=lambda entry: entry[0].lower())
    path.write_bytes(written(entries))


def client_tables():
    animations = table(8, [[handspring.JUMP, 1, 0, 257, 2, 0, 38, 0],
                           [handspring.FLY_JUMP, 6, 0, 257, 2, 229, 38, 3]], b"\0Jump\0FlyJump\0")
    kits = table(38, [[handspring.VAULT_DUST_KIT, 0xFFFFFFFF, handspring.JUMP_START] + [0] * 35], b"\0")
    return {ANIMATIONS: animations, KITS: kits}


def rows(dbc):
    count, fields, size, _ = struct.unpack_from("<4I", dbc, 4)
    return {struct.unpack_from("<I", dbc, 20 + index * size)[0] for index in range(count)}


class VaultPatchTest(unittest.TestCase):
    def setUp(self):
        self.folder = tempfile.TemporaryDirectory()
        self.data = Path(self.folder.name)
        self.standard = synthetic_model()
        self.hd = synthetic_model() + bytes(16)
        archive(self.data / "common.MPQ", {HUMAN: self.standard, **client_tables()})
        archive(self.data / "patch-Q.MPQ", {HUMAN: self.hd})
        archive(self.data / vault.MERGED, {OWN_FILE: b"twins", HUMAN: b"stale"})

    def tearDown(self):
        self.folder.cleanup()

    def built(self, name):
        return Archive(str(self.data / name))

    def test_standard_model_goes_into_the_merged_patch(self):
        vault.build(self.data, self.data)
        merged = self.built(vault.MERGED)
        self.assertEqual(merged.extract(HUMAN), handspring.add_handsprings(self.standard))
        self.assertEqual(merged.extract(OWN_FILE), b"twins")
        self.assertTrue(set(handspring.ANIMATION_NAMES) <= rows(merged.extract(ANIMATIONS)))
        self.assertTrue(set(handspring.VAULT_KITS) <= rows(merged.extract(KITS)))

    def test_hd_model_goes_into_the_hd_patch(self):
        vault.build(self.data, self.data)
        self.assertEqual(self.built(vault.HD_PATCH).extract(HUMAN), handspring.add_handsprings(self.hd))

    def test_rebuilding_starts_from_the_client_models(self):
        vault.build(self.data, self.data)
        vault.build(self.data, self.data)
        self.assertEqual(self.built(vault.MERGED).extract(HUMAN), handspring.add_handsprings(self.standard))
        self.assertEqual(self.built(vault.HD_PATCH).extract(HUMAN), handspring.add_handsprings(self.hd))
        self.assertEqual(Archive(str(self.data / (vault.MERGED + ".before-vault"))).extract(HUMAN), b"stale")

    def test_client_without_hd_archives_gets_no_hd_patch(self):
        (self.data / "patch-Q.MPQ").unlink()
        vault.build(self.data, self.data)
        self.assertFalse((self.data / vault.HD_PATCH).exists())

    def test_root_patches_win_over_locale_tables(self):
        (self.data / "enUS").mkdir()
        locale_tables = {ANIMATIONS: table(8, [[1, 0, 0, 0, 0, 0, 0, 0]], b"\0"), KITS: client_tables()[KITS]}
        archive(self.data / "enUS" / "patch-enUS-3.MPQ", locale_tables)
        archive(self.data / "patch-ZZZZS.MPQ", client_tables())
        vault.build(self.data, self.data)
        built = rows(self.built(vault.MERGED).extract(ANIMATIONS))
        self.assertIn(handspring.JUMP, built)
        self.assertNotIn(1, built)

    def test_no_build_files_are_left_in_data(self):
        vault.build(self.data, self.data)
        self.assertEqual(sorted(path.name for path in self.data.iterdir() if ".building" in path.name or
                                path.name.endswith(".vault")), [])

    def test_hd_archives_are_left_out_of_the_standard_models(self):
        names = [Path(name) for name in ("common.MPQ", "patch-Q.MPQ", "patch-CHA.MPQ", "patch-ZZZZY.MPQ",
                                         "patch-ZZZZZ-HD.MPQ", "patch-Bear-HD.MPQ")]
        self.assertEqual([path.name for path in vault.standard_archives(names)], ["common.MPQ", "patch-CHA.MPQ"])
        self.assertEqual([path.name for path in vault.original_archives(names)],
                         ["common.MPQ", "patch-Q.MPQ", "patch-CHA.MPQ", "patch-Bear-HD.MPQ"])


if __name__ == "__main__":
    unittest.main()
