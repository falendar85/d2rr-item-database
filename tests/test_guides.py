import json
import os
import pathlib
import subprocess
import sys
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from generate_guides import GuideParser


class GuideGeneratorTests(unittest.TestCase):
    def test_table_recipe_is_flattened(self):
        parser = GuideParser()
        parser.feed("<h2>Socket Recipes</h2><p>Note</p><table><tr><th>Reagent</th><th></th><th>Outcome</th></tr>"
                    "<tr><td>Item<br>OR<br>Weapon</td><td>=</td><td>Socketed Item</td></tr></table>")
        self.assertEqual(parser.sections, [{"name": "Socket Recipes", "lines": [
            "Note", "Item / OR / Weapon = Socketed Item", ""]}])

    def test_checked_in_categories_and_details(self):
        database = json.loads((ROOT / "data" / "guides.json").read_text(encoding="utf-8"))
        by_tab = {}
        for record in database["records"]:
            by_tab.setdefault(record["tab"], []).append(record)
            self.assertTrue(record["display_lines"])
            self.assertNotRegex("\n".join(record["display_lines"]), r"<[^>]+>")
        self.assertEqual({tab: len(rows) for tab, rows in by_tab.items()}, {
            "cube-recipes": 8, "item-enchants": 9, "item-crafting": 12, "loot-table": 4})
        self.assertEqual([row["name"] for row in by_tab["cube-recipes"]][0:2],
                         ["Socket Recipes", "Unsocket Recipes"])
        self.assertEqual(by_tab["loot-table"][-1]["name"], "Runes")

    def test_deterministic_regeneration_when_source_is_cached(self):
        source = pathlib.Path(os.environ.get("ITEMDB_WIKI_SOURCE", ROOT / ".deps" / "wiki-content-main"))
        if not source.exists():
            self.skipTest("cached pinned wiki source is not present")
        revision = json.loads((ROOT / "upstream.lock.json").read_text())["wiki"]["sha"]
        with tempfile.TemporaryDirectory() as temporary:
            one = pathlib.Path(temporary) / "one.json"
            two = pathlib.Path(temporary) / "two.json"
            command = [sys.executable, str(ROOT / "tools" / "generate_guides.py"),
                       "--source", str(source), "--revision", revision, "--output"]
            subprocess.run(command + [str(one)], check=True, capture_output=True, text=True)
            subprocess.run(command + [str(two)], check=True, capture_output=True, text=True)
            self.assertEqual(one.read_bytes(), two.read_bytes())
            self.assertEqual(one.read_bytes(), (ROOT / "data" / "guides.json").read_bytes())


if __name__ == "__main__":
    unittest.main()
