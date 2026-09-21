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
        self.assertEqual(parser.sections[0]["name"], "Socket Recipes")
        self.assertEqual(parser.sections[0]["lines"], ["Note"])
        self.assertEqual(parser.sections[0]["tables"][0]["rows"], [
            ["Reagent", "", "Outcome"], ["Item\nOR\nWeapon", "=", "Socketed Item"]])

    def test_checked_in_categories_and_details(self):
        database = json.loads((ROOT / "data" / "guides.json").read_text(encoding="utf-8"))
        by_tab = {}
        for record in database["records"]:
            by_tab.setdefault(record["tab"], []).append(record)
            self.assertTrue(record["display_lines"])
            self.assertNotRegex("\n".join(record["display_lines"]), r"<[^>]+>")
            for table in record["guide_tables"]:
                self.assertTrue(table["rows"])
                self.assertTrue(table["headers"])
                self.assertTrue(all(len(row) == len(table["headers"]) for row in table["rows"]))
        self.assertEqual({tab: len(rows) for tab, rows in by_tab.items()}, {
            "cube-recipes": 8, "item-enchants": 9, "item-crafting": 12, "orbs": 7})
        self.assertEqual([row["name"] for row in by_tab["cube-recipes"]][0:2],
                         ["Socket Recipes", "Unsocket Recipes"])
        self.assertEqual([row["name"] for row in by_tab["orbs"]], [
            "Orb of Renewal", "Orb of Conversion", "Orb of Assemblage", "Orb of Infusion",
            "Orb of Shadows", "Orb of Socketing", "Orb of Corruption"])
        corruption = by_tab["orbs"][-1]
        self.assertNotIn("WHERE TO FIND", "\n".join(corruption["display_lines"]))
        self.assertEqual([table["title"] for table in corruption["guide_tables"]], [
            "Amulet", "Ring", "Chest", "Gloves", "Boots", "Belts", "Helm", "Shield", "Weapon"])

    def test_deterministic_regeneration_when_source_is_cached(self):
        source = pathlib.Path(os.environ.get("ITEMDB_WIKI_SOURCE", ROOT / ".deps" / "wiki-content-main"))
        if not source.exists() or not (source / "Items" / "Orbs.html").exists():
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
