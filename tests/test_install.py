import csv
import importlib.util
import io
import json
from pathlib import Path
from contextlib import contextmanager
from uuid import uuid4
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location("recharge_install", Path(__file__).resolve().parents[1] / "tools/install.py")
install = importlib.util.module_from_spec(spec)
spec.loader.exec_module(install)
TEST_ROOT = Path(__file__).resolve().parents[2] / "build-charge-replenishment-tests"
TEST_ROOT.mkdir(exist_ok=True)


@contextmanager
def fixture_directory():
    # Ordinary inherited directory permissions also work in Windows sandboxes.
    # Retain generated fixtures outside the source tree for failure inspection.
    path = TEST_ROOT / str(uuid4())
    path.mkdir()
    yield path


class InstallerTests(unittest.TestCase):
    def test_reordered_columns_creator_and_preservation(self):
        original = b"extra\tcode\tstat1\tfunc1\t*Creator\r\nkeep\told\toldstat\t1\tSomeone\r\n"
        result, changed = install.add_row(original, "code", {"code": "new", "func1": "1", "stat1": "stat"})
        self.assertTrue(changed)
        self.assertTrue(result.startswith(original))
        _, rows = install.table(result, "code")
        self.assertEqual(rows[-1], {"extra": "", "code": "new", "stat1": "stat", "func1": "1", "*Creator": "MadMike"})
        self.assertEqual(install.add_row(result, "code", {"code": "new", "func1": "1", "stat1": "stat"}), (result, False))

    def test_reject_bad_headers_missing_values_and_collisions(self):
        for bad in (b"code\tcode\n", b"code\t\n", b"wrong\tvalue\n"):
            with self.assertRaises(ValueError):
                install.add_row(bad, "code", {"code": "new"})
        with self.assertRaises(ValueError):
            install.add_row(b"code\n", "code", {"code": "new", "missing": "valuable"})
        with self.assertRaises(ValueError):
            install.add_row(b"code\tstat1\nnew\tother\n", "code", {"code": "new", "stat1": "stat"})
        result, _ = install.add_row(b"code\n", "code", {"code": "new", "*Creator": "MadMike"})
        self.assertNotIn(b"*Creator", result)

    def test_json_comments_trailing_commas_and_string_content(self):
        text = '[\n  {"id": 1, "Key": "original", "enUS": "// not a comment ,} \\\"text\\\"",},\n// credit\n]\n'
        before = text.encode()
        entry = {"id": 2, "Key": "new", "enUS": "Charges %d"}
        result = install.append_localization(before, entry)
        self.assertEqual(install.load_json(result.decode()), install.load_json(text) + [entry])
        self.assertIn(b"// credit", result)
        self.assertIn(b'// not a comment ,}', result)
        self.assertEqual(install.append_localization(result, entry), result)

    def fixture(self, root, with_mirror=True):
        mod = root / "mods/TestMod"
        excel = mod / "TestMod.mpq/data/global/excel"
        strings = mod / "TestMod.mpq/data/local/lng/strings"
        excel.mkdir(parents=True)
        strings.mkdir(parents=True)
        # Reverse the entire header order to prove name-based insertion.
        header = list(reversed(["Stat", "*ID", "Signed", "Send Bits", "Save Bits", "Save Add", "descpriority", "descfunc", "descval", "descstrpos", "descstrneg", "*eol"]))
        output = io.StringIO()
        writer = csv.DictWriter(output, fieldnames=header, delimiter="\t", lineterminator="\n")
        writer.writeheader()
        for index in range(399):
            writer.writerow({"Stat": "item_charged_skill" if index == 204 else f"stat_{index}", "*ID": str(index), "Save Bits": "16" if index == 204 else ""})
        (excel / "itemstatcost.txt").write_text(output.getvalue(), encoding="utf-8")
        header = list(reversed(["code", "*Id", "*Enabled", "func1", "stat1", "uiRangeType", "*Tooltip", "*Parameter", "*Min", "*Max", "*Notes", "*eol"]))
        output = io.StringIO()
        writer = csv.DictWriter(output, fieldnames=header, delimiter="\t", lineterminator="\n")
        writer.writeheader()
        writer.writerow({"code": "old", "*Id": "0"})
        writer.writerow({"code": "Expansion"})
        (excel / "properties.txt").write_text(output.getvalue(), encoding="utf-8")
        (strings / "item-modifiers.json").write_text('[\n  {"id":10000,"Key":"existing","enUS":"Keep"}\n]\n', encoding="utf-8")
        (strings / "skills.json").write_text('[{"id":10001,"Key":"skill"}, // Keep this comment\n]', encoding="utf-8")
        config = mod / "d2rloader/config/charge-replenishment.toml"
        config.parent.mkdir(parents=True)
        config.write_text('[charge-replenishment]\nenabled = false\nstat_id = -1\ninclude_inventory = false\n\n[other-plugin]\nkeep = 17\n', encoding="utf-8")
        (excel / "itemstatcost.bin").write_bytes(b"stale")
        if with_mirror:
            (root / "d2rloader/I Hate TOMLs").mkdir(parents=True)
        return mod

    def test_install_without_optional_config_mirror(self):
        with fixture_directory() as temporary:
            root = Path(temporary)
            mod = self.fixture(root, with_mirror=False)
            changes, _ = install.plan_install(mod, None)
            install.commit(mod, changes)
            self.assertFalse((root / "d2rloader/I Hate TOMLs").exists())
            self.assertTrue(install.tomllib.loads((mod / "d2rloader/config/charge-replenishment.toml").read_text())["installation"]["complete"])
            self.assertEqual(install.plan_install(mod, None)[0], {})

    def test_full_plan_backup_config_and_idempotence(self):
        with fixture_directory() as temporary:
            self.assertTrue(Path(temporary).resolve().is_relative_to(TEST_ROOT.resolve()))
            mod = self.fixture(Path(temporary))
            changes, summary = install.plan_install(mod, None)
            self.assertEqual(summary["stat_id"], 399)
            self.assertEqual(summary["property_id"], 1)
            vanilla = json.loads((install.ROOT / "data/vanilla-string-ids.json").read_text())
            reserved = {i for start, end in vanilla["inclusive_ranges"] for i in range(start, end + 1)}
            self.assertNotIn(summary["localization_id"], reserved | {10000, 10001})
            originals = {p: p.read_bytes() for p in changes if p.exists()}
            backup = install.commit(mod, changes)
            self.assertTrue(backup.is_dir())
            self.assertEqual(len(json.loads((backup / "manifest.json").read_text())), len(changes))
            config = mod / "d2rloader/config/charge-replenishment.toml"
            parsed = install.tomllib.loads(config.read_text())
            self.assertFalse(parsed[install.PLUGIN_ID]["enabled"])
            self.assertFalse(parsed[install.PLUGIN_ID]["include_inventory"])
            self.assertEqual(parsed["other-plugin"]["keep"], 17)
            self.assertTrue(parsed["installation"]["complete"])
            mirror = mod.parent.parent / "d2rloader/I Hate TOMLs/TestMod/charge-replenishment.toml"
            self.assertEqual(config.read_bytes(), mirror.read_bytes())
            changes_again, _ = install.plan_install(mod, None)
            self.assertEqual(changes_again, {})
            self.assertNotEqual(originals[config], config.read_bytes())

    def test_failed_commit_restores_all_files_and_leaves_no_token(self):
        with fixture_directory() as temporary:
            self.assertTrue(Path(temporary).resolve().is_relative_to(TEST_ROOT.resolve()))
            mod = self.fixture(Path(temporary))
            changes, _ = install.plan_install(mod, None)
            before = {p: p.read_bytes() if p.exists() else None for p in changes}
            real_replace = Path.replace
            count = 0
            def fail_once(path, target):
                nonlocal count
                count += 1
                if count == 3:
                    raise OSError("injected disk failure")
                return real_replace(path, target)
            with patch.object(Path, "replace", fail_once):
                with self.assertRaises(OSError):
                    install.commit(mod, changes)
            for path, original in before.items():
                self.assertEqual(path.read_bytes() if path.exists() else None, original)
            self.assertNotIn("installation", (mod / "d2rloader/config/charge-replenishment.toml").read_text())

    def test_rebind_owned_localization_when_an_inherited_id_collides(self):
        with fixture_directory() as temporary:
            mod = self.fixture(Path(temporary))
            changes, initial = install.plan_install(mod, None)
            install.commit(mod, changes)
            path = mod / "TestMod.mpq/data/local/lng/strings/item-modifiers.json"
            owned = next(e for e in install.load_json(path.read_text(encoding="utf-8")) if e.get("Key") == "ModChargeReplenishmentRate")
            # ID 10002 belongs to an inherited vanilla monster string.
            path.write_bytes(install.append_localization(path.read_bytes(), {**owned, "id": 10002}))
            unrelated = b'{"id":10000,"Key":"existing","enUS":"Keep"}'
            changes, corrected = install.plan_install(mod, None)
            self.assertEqual(set(changes), {path})
            self.assertEqual(corrected["localization_id"], initial["localization_id"])
            self.assertIn(unrelated, changes[path])
            install.commit(mod, changes)
            self.assertEqual(install.plan_install(mod, None)[0], {})


if __name__ == "__main__":
    unittest.main()
