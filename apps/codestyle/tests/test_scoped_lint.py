"""Exercise scoped lint selection and protected SQL paths in disposable repositories."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

SCRIPTS = Path(__file__).resolve().parents[1]
GOOD_SQL = "SELECT 1;\n"
BAD_SQL = "SELECT 1;;\n"


class ScopedLintTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="scoped-lint-")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name) / "database-work"
        self.root.mkdir()
        self.pending = "data/sql/updates/pending_db_world/"
        self.put(self.pending + "selected.sql", GOOD_SQL)
        self.put(self.pending + "unrelated.sql", BAD_SQL)
        self.put("src/selected.cpp", "int value = 1;\n")
        self.put("src/unrelated.cpp", "int value = 1;;\n")
        self.git("init", "--quiet", "--initial-branch=main")
        self.git("add", ".")
        self.git("-c", "user.name=Lint Tests", "-c", "user.email=lint-tests@example.invalid",
                 "commit", "--quiet", "-m", "Baseline")

    def put(self, relative, text):
        path = self.root / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8", newline="\n")
        return path

    def git(self, *args):
        return subprocess.run(["git", *args], cwd=self.root, check=True, capture_output=True,
                              text=True, encoding="utf-8")

    def lint(self, language, *args):
        return subprocess.run([sys.executable, "-B", "-X", "utf8",
                               str(SCRIPTS / ("codestyle-" + language + ".py")), *args],
                              cwd=self.root, capture_output=True, text=True, encoding="utf-8", timeout=20)

    def assert_passes(self, result):
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_cpp_files_ignores_unselected_errors_and_default_still_scans(self):
        self.assert_passes(self.lint("cpp", "--files", "src/selected.cpp"))
        result = self.lint("cpp")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("unrelated.cpp", result.stdout)

    def test_cpp_selected_file_keeps_style_checks(self):
        result = self.lint("cpp", "--files", "src/unrelated.cpp")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Double semicolon", result.stdout)

    def test_sql_files_works_without_git_and_handles_spaces(self):
        path = self.put(self.pending + "selected migration.sql", GOOD_SQL)
        # Explicit files never need git, a local ref, or a remote connection.
        environment = dict(os.environ, PATH="")
        result = subprocess.run([sys.executable, "-B", "-X", "utf8",
                                 str(SCRIPTS / "codestyle-sql.py"), "--files", str(path)],
                                cwd=self.root, env=environment, capture_output=True,
                                text=True, encoding="utf-8", timeout=20)
        self.assert_passes(result)

    def test_sql_local_base_limits_selection_and_default_still_scans(self):
        self.put(self.pending + "selected.sql", "SELECT 2;\n")
        self.assert_passes(self.lint("sql", "--base", "main"))
        result = self.lint("sql")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("unrelated.sql", result.stdout)

    def test_sql_builtin_functions_do_not_require_identifier_quotes(self):
        self.put(self.pending + "functions.sql", "SELECT ABS(`spell_id`), LEAST(`level` * 5, 400), SUM(`level`), FLOOR(`level` / 2) FROM `characters`;\n")
        self.assert_passes(self.lint("sql", "--files", self.pending + "functions.sql"))

    def test_sql_upsert_continues_after_the_last_value_tuple(self):
        self.put(self.pending + "upsert.sql", "INSERT INTO `creature_template` (`entry`, `name`) VALUES\n(1, 'One'),\n(2, 'Two')\nON DUPLICATE KEY UPDATE\n`name` = VALUES(`name`);\n")
        self.assert_passes(self.lint("sql", "--files", self.pending + "upsert.sql"))

    def test_sql_selected_file_keeps_style_checks(self):
        result = self.lint("sql", "--files", self.pending + "unrelated.sql")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("SQL codestyle check : Failed", result.stdout)

    def test_sql_base_handles_renames_additions_and_deletions(self):
        self.git("mv", self.pending + "selected.sql", self.pending + "renamed migration.sql")
        self.put(self.pending + "added.sql", GOOD_SQL)
        self.git("add", self.pending + "added.sql")
        self.git("rm", self.pending + "unrelated.sql")
        self.assert_passes(self.lint("sql", "--base", "main"))
        self.put(self.pending + "added.sql", BAD_SQL)
        self.assertNotEqual(self.lint("sql", "--base", "main").returncode, 0)

    def test_sql_protected_directories_remain_rejected(self):
        for directory in ("base", "archive"):
            relative = "data/sql/" + directory + "/db_world/template.sql"
            self.put(relative, GOOD_SQL)
            result = self.lint("sql", "--files", relative)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("Directory check : Failed", result.stdout)
            self.git("add", relative)
        self.assertNotEqual(self.lint("sql", "--base", "main").returncode, 0)

    def test_sql_multiline_delete_matches_insert(self):
        path = self.pending + "selected.sql"
        self.put(path, "DELETE FROM `spell_script_names` WHERE `spell_id` = 123\n"
                       "  AND `ScriptName` = 'example';\n"
                       "INSERT INTO `spell_script_names` (`spell_id`, `ScriptName`) VALUES (123, 'example');\n")
        self.assert_passes(self.lint("sql", "--files", path))

    def test_sql_protected_upsert_preserves_existing_template(self):
        path = self.pending + "selected.sql"
        self.put(path, "INSERT INTO `creature_template` (`entry`, `name`)\n"
                       "VALUES (557911, 'Soulstone Lure')\n"
                       "ON DUPLICATE KEY UPDATE `name` = VALUES(`name`);\n")
        self.assert_passes(self.lint("sql", "--files", path))
        self.put(path, "DELETE FROM `creature_template` WHERE `entry` = 557911;\n"
                       "INSERT INTO `creature_template` (`entry`, `name`) VALUES (557911, 'Soulstone Lure');\n")
        self.assertNotEqual(self.lint("sql", "--files", path).returncode, 0)

    def test_sql_fresh_temporary_table_accepts_initial_rows(self):
        path = self.pending + "temporary.sql"
        self.put(path, "CREATE TEMPORARY TABLE `migration_guard` (`id` INT NOT NULL);\n"
                       "INSERT INTO `migration_guard` (`id`) VALUES (1);\n")
        self.assert_passes(self.lint("sql", "--files", path))

    def test_sql_contiguous_inserts_share_the_matching_delete(self):
        path = self.pending + "rows.sql"
        self.put(path, "DELETE FROM `spell_script_names` WHERE `spell_id` IN (1, 2);\n"
                       "INSERT INTO `spell_script_names` VALUES (1, 'one');\n"
                       "INSERT INTO `spell_script_names` VALUES (2, 'two');\n")
        self.assert_passes(self.lint("sql", "--files", path))

    def test_sql_join_delete_keeps_protected_table_safety(self):
        path = self.pending + "joined.sql"
        for table, allowed in (("reference_loot_template", True), ("creature_template", False)):
            with self.subTest(table=table):
                self.put(path, f"DELETE `r` FROM `{table}` AS `r` JOIN `owned` AS `o` USING (`entry`);\n"
                               f"INSERT INTO `{table}` (`entry`) VALUES (7);\n")
                self.assertEqual(self.lint("sql", "--files", path).returncode == 0, allowed)

    def test_sql_multiline_set_and_builtin_functions(self):
        path = self.pending + "count.sql"
        self.put(path, "SET @rows := (\n"
                       "SELECT COUNT(*) FROM `creature_template`\n"
                       ");\n")
        self.assert_passes(self.lint("sql", "--files", path))

    def test_sql_not_exists_guard_requires_the_inserted_table(self):
        path = self.pending + "guarded.sql"
        for guard, allowed in (("manastorm_hub_return", True), ("another_table", False)):
            with self.subTest(guard=guard):
                self.put(path, "INSERT INTO `manastorm_hub_return` (`id`) SELECT 1\n"
                               f"WHERE NOT EXISTS (SELECT 1 FROM `{guard}` WHERE `id` = 1);\n")
                self.assertEqual(self.lint("sql", "--files", path).returncode == 0, allowed)

    def test_sql_module_upsert_preserves_existing_rows(self):
        path = self.pending + "module_upsert.sql"
        self.put(path, "INSERT INTO `manastorm_segment` (`segment_id`, `name`)\n"
                       "VALUES (7, 'Seven')\n"
                       "ON DUPLICATE KEY UPDATE `name` = VALUES(`name`);\n")
        self.assert_passes(self.lint("sql", "--files", path))

    def test_sql_unrelated_delete_and_quoted_upsert_do_not_bypass_safety(self):
        path = self.pending + "selected.sql"
        for text in [
            "DELETE FROM `spell_proc` WHERE `SpellId` = 123;\n"
            "INSERT INTO `spell_script_names` (`spell_id`, `ScriptName`) VALUES (123, 'example');\n",
            "INSERT INTO `creature_template` (`entry`, `name`) VALUES (557911, 'ON DUPLICATE KEY UPDATE');\n",
            "INSERT INTO `manastorm_segment` (`segment_id`, `name`) VALUES (7, 'ON DUPLICATE KEY UPDATE');\n",
        ]:
            with self.subTest(text=text):
                self.put(path, text)
                self.assertNotEqual(self.lint("sql", "--files", path).returncode, 0)

    def test_invalid_inputs_fail_clearly(self):
        for language in ("cpp", "sql"):
            result = self.lint(language, "--files", "missing.file")
            self.assertEqual(result.returncode, 2)
            self.assertIn("error:", result.stderr)
        result = self.lint("sql", "--base", "missing-ref")
        self.assertEqual(result.returncode, 2)
        self.assertIn("No local comparison ref", result.stderr)
        self.assertEqual(self.lint("sql", "--files", self.pending + "selected.sql",
                                   "--base", "main").returncode, 2)


if __name__ == "__main__":
    unittest.main()
