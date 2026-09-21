#!/usr/bin/env python3
"""Convert the pinned D2R Reimagined wiki pages into an offline guide database."""

import argparse
import hashlib
import html
from html.parser import HTMLParser
import json
from pathlib import Path
import re


PAGES = (
    ("cube-recipes", "recipes/CubeRecipes.html"),
    ("item-enchants", "recipes/ItemEnchants.html"),
    ("item-crafting", "recipes/Crafting.html"),
    ("loot-table", "Items/LootTable.html"),
)


def clean(parts):
    value = html.unescape(" ".join(parts)).replace("\xa0", " ")
    value = re.sub(r"\s*/\s*", " / ", value)
    return re.sub(r"\s+", " ", value).strip(" /\t\r\n")


class GuideParser(HTMLParser):
    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.sections = []
        self.current = None
        self.capture = None
        self.parts = []
        self.table_depth = 0
        self.row = None
        self.cell = None

    def handle_starttag(self, tag, attrs):
        tag = tag.lower()
        if tag == "table":
            self.table_depth += 1
        elif tag == "tr" and self.table_depth:
            self.row = []
        elif tag in ("td", "th") and self.table_depth and self.row is not None:
            self.cell = []
        elif tag in ("h2", "h3") or (tag in ("p", "li") and not self.table_depth):
            self.capture = tag
            self.parts = []
        elif tag == "br":
            if self.cell is not None:
                self.cell.append(" / ")
            elif self.capture:
                self.parts.append(" / ")

    def handle_endtag(self, tag):
        tag = tag.lower()
        if tag in ("td", "th") and self.cell is not None:
            self.row.append(clean(self.cell))
            self.cell = None
        elif tag == "tr" and self.row is not None:
            cells = [cell for cell in self.row if cell]
            lowered = {cell.lower() for cell in cells}
            if self.current is not None and cells and not lowered.issubset({"reagent", "outcome", "result", "item", "ingredients"}):
                if "=" in cells:
                    equals = cells.index("=")
                    left = " + ".join(cells[:equals])
                    right = " / ".join(cells[equals + 1:])
                    line = f"{left} = {right}" if right else left
                else:
                    line = " | ".join(cells)
                if line:
                    self.current["lines"].append(line)
            self.row = None
        elif tag == "table" and self.table_depth:
            self.table_depth -= 1
            if self.current is not None and self.current["lines"] and self.current["lines"][-1] != "":
                self.current["lines"].append("")
        elif tag == self.capture:
            value = clean(self.parts)
            if tag == "h2":
                self.current = {"name": value, "lines": []}
                self.sections.append(self.current)
            elif self.current is not None and value:
                if tag == "h3":
                    if self.current["lines"] and self.current["lines"][-1] != "":
                        self.current["lines"].append("")
                    self.current["lines"].append(value.upper())
                else:
                    self.current["lines"].append(("• " if tag == "li" else "") + value)
            self.capture = None
            self.parts = []

    def handle_data(self, data):
        if self.cell is not None:
            self.cell.append(data)
        elif self.capture:
            self.parts.append(data)


def parse_page(path):
    parser = GuideParser()
    parser.feed(path.read_text(encoding="utf-8"))
    sections = parser.sections
    if path.name == "ItemEnchants.html":
        sections = [section for section in sections if section["name"] != "Item Enchant System"]
    for section in sections:
        while section["lines"] and not section["lines"][-1]:
            section["lines"].pop()
    return sections


def main():
    argument_parser = argparse.ArgumentParser()
    argument_parser.add_argument("--source", required=True, type=Path)
    argument_parser.add_argument("--output", required=True, type=Path)
    argument_parser.add_argument("--revision", required=True)
    args = argument_parser.parse_args()

    records = []
    hashes = {}
    counts = {}
    for tab, relative in PAGES:
        source = args.source / relative
        raw = source.read_bytes()
        hashes[relative] = hashlib.sha256(raw).hexdigest()
        sections = parse_page(source)
        counts[tab] = len(sections)
        for order, section in enumerate(sections):
            lines = section["lines"]
            records.append({
                "id": f"{tab}:{order:02d}:{section['name']}",
                "tab": tab,
                "name": section["name"],
                "fields": {"source": [relative]},
                "numbers": {"order": order},
                "properties": [],
                "display_lines": lines,
                "source_ref": {"file": relative, "section": section["name"]},
                "search_text": "\n".join([section["name"], *lines]),
            })

    database = {
        "schema_version": 1,
        "provenance": {
            "repository": "D2R-Reimagined/wiki-content",
            "revision": args.revision,
            "source_hashes": hashes,
            "category_counts": counts,
        },
        "records": records,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(database, ensure_ascii=False, separators=(",", ":")) + "\n", encoding="utf-8")
    print(json.dumps({"records": len(records), "category_counts": counts}, indent=2))


if __name__ == "__main__":
    main()
