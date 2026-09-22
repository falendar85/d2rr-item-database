import importlib.util
import json
from pathlib import Path
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("maintain_data", ROOT / "tools" / "maintain_data.py")
maintain_data = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(maintain_data)


class MaintenanceWorkflowTests(unittest.TestCase):
    def test_current_project_builds_a_valid_baseline_package(self):
        with tempfile.TemporaryDirectory() as temporary:
            baseline = Path(temporary) / "baseline"
            manifest = maintain_data.prepare_baseline(baseline)
            self.assertEqual(
                maintain_data.toolkit.sha256_file(baseline / "database.json"),
                "527323eaa9a8d89af6e7798ba9d7893bf3071f0cbc7034f41d6134f389949b14",
            )
            self.assertEqual(
                maintain_data.toolkit.sha256_file(baseline / "guides.json"),
                "f1931cae646ade287a26d5c89910da132c2153d290aeed84dbf033a7716d7f4a",
            )
            self.assertEqual(
                manifest["groups"]["website"]["revision"],
                "c7db0b49f6f75e1c15e808533572e4ac69be2db8",
            )
            self.assertFalse(maintain_data.toolkit.verify_sha256s(baseline)[0])

    def test_identical_normalized_data_has_no_record_changes(self):
        report = maintain_data.toolkit.diff_normalized(
            ROOT / "data" / "database.json",
            ROOT / "data" / "database.json",
        )
        self.assertEqual(report["added_count"], 0)
        self.assertEqual(report["removed_count"], 0)
        self.assertEqual(report["changed_count"], 0)

    def test_record_diff_reports_add_change_and_remove(self):
        def record(identifier, value):
            return {
                "id": identifier,
                "tab": "uniques",
                "name": identifier,
                "fields": {"name": [identifier]},
                "numbers": {},
                "properties": [],
                "display_lines": [identifier, str(value)],
                "source_ref": {"file": "uniques.json", "index": identifier, "row": 0},
                "search_text": f"{identifier}\n{value}",
            }

        def root(records):
            return {
                "schema_version": 1,
                "provenance": {"repository": "x/y", "revision": "abc", "source_hashes": {}},
                "records": records,
            }

        with tempfile.TemporaryDirectory() as temporary:
            old_path = Path(temporary) / "old.json"
            new_path = Path(temporary) / "new.json"
            old_path.write_text(json.dumps(root([record("uniques:A", 1), record("uniques:B", 1)])), encoding="utf-8")
            new_path.write_text(json.dumps(root([record("uniques:A", 2), record("uniques:C", 1)])), encoding="utf-8")
            report = maintain_data.toolkit.diff_normalized(old_path, new_path)
            self.assertEqual((report["added_count"], report["removed_count"], report["changed_count"]), (1, 1, 1))

    def test_validator_rejects_duplicate_record_ids(self):
        source = json.loads((ROOT / "data" / "database.json").read_text(encoding="utf-8"))
        source["records"].append(source["records"][0])
        with tempfile.TemporaryDirectory() as temporary:
            candidate = Path(temporary) / "database.json"
            candidate.write_text(json.dumps(source), encoding="utf-8")
            result = maintain_data.toolkit.validate_normalized(candidate, "database")
            self.assertTrue(any("duplicate id" in error for error in result["errors"]))

    def test_source_text_report_caps_long_key_lists(self):
        report = {
            "old_revision_summary": "old",
            "new_revision_summary": "new",
            "files": [{
                "path": "website/strings/enUS.json",
                "changed": True,
                "format": "json-keyed",
                "added_count": 75,
                "removed_count": 0,
                "changed_count": 0,
                "added": [f"key-{index}" for index in range(75)],
                "removed": [],
                "changed_entries": [],
            }],
        }
        text = maintain_data.toolkit.format_source_diff(report)
        self.assertIn("... 25 more added entries in JSON report", text)
        self.assertIn("key-49", text)
        self.assertNotIn("key-50", text)


if __name__ == "__main__":
    unittest.main()
