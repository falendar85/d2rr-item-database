#!/usr/bin/env python3
"""Convert official D2R Reimagined wiki pages into an offline guide database."""

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
    ("orbs", "Items/Orbs.html"),
)
BREAK = "\x1e"
ORB_NAMES = (
    "Orb of Renewal", "Orb of Conversion", "Orb of Assemblage", "Orb of Infusion",
    "Orb of Shadows", "Orb of Socketing", "Orb of Corruption",
)
CORRUPTION_GEARS = ("Amulet", "Ring", "Chest", "Gloves", "Boots", "Belts", "Helm", "Shield", "Weapon")


def clean(parts):
    value = html.unescape(" ".join(parts)).replace("\xa0", " ").replace("�", "•")
    value = re.sub(r"\s*/\s*", " / ", value)
    return re.sub(r"\s+", " ", value).strip(" /\t\r\n")


def clean_cell(parts):
    value = html.unescape(" ".join(parts)).replace("\xa0", " ")
    output = []
    for line in value.split(BREAK):
        line = re.sub(r"[ \t\r\f\v]+", " ", line).strip()
        if line and (not output or output[-1] != line):
            output.append(line)
    return "\n".join(output)


class GuideParser(HTMLParser):
    def __init__(self, section_tag="h2", subsection_tag="h3"):
        super().__init__(convert_charrefs=True)
        self.section_tag = section_tag
        self.subsection_tag = subsection_tag
        self.sections = []
        self.current = None
        self.capture = None
        self.parts = []
        self.table_depth = 0
        self.head_depth = 0
        self.table = None
        self.row = None
        self.cell = None
        self.pending_table_title = ""

    def handle_starttag(self, tag, attrs):
        tag = tag.lower()
        if self.cell is not None:
            if tag == "br":
                self.cell.append(BREAK)
            return
        if tag == "table":
            self.table_depth += 1
            if self.table_depth == 1 and self.current is not None:
                self.table = {"title": self.pending_table_title, "headers": [], "rows": []}
                if self.pending_table_title and self.current["lines"] and \
                        self.current["lines"][-1] == self.pending_table_title.upper():
                    self.current["lines"].pop()
                self.pending_table_title = ""
                self.current["tables"].append(self.table)
        elif tag == "thead" and self.table_depth:
            self.head_depth += 1
        elif tag == "tr" and self.table_depth:
            self.row = []
        elif tag in ("td", "th") and self.table_depth and self.row is not None:
            self.cell = []
        elif tag in (self.section_tag, self.subsection_tag) or (tag in ("p", "li") and not self.table_depth):
            self.capture = tag
            self.parts = []
        elif tag == "br" and self.capture:
            self.parts.append(" / ")

    def handle_endtag(self, tag):
        tag = tag.lower()
        if tag in ("p", "div") and self.cell is not None:
            self.cell.append(BREAK)
        if tag in ("td", "th") and self.cell is not None:
            self.row.append(clean_cell(self.cell))
            self.cell = None
        elif tag == "tr" and self.row is not None:
            if self.table is not None:
                target = "headers" if self.head_depth else "rows"
                if target == "headers" and not self.table[target]:
                    self.table[target] = self.row
                elif target == "rows" and any(self.row):
                    self.table[target].append(self.row)
            self.row = None
        elif tag == "thead" and self.head_depth:
            self.head_depth -= 1
        elif tag == "table" and self.table_depth:
            self.table_depth -= 1
            if self.table_depth == 0:
                self.table = None
        elif tag == self.capture:
            value = clean(self.parts)
            if tag == self.section_tag:
                self.current = {"name": value, "lines": [], "tables": []}
                self.sections.append(self.current)
                self.pending_table_title = ""
            elif self.current is not None and value:
                if tag == self.subsection_tag:
                    if self.current["lines"] and self.current["lines"][-1] != "":
                        self.current["lines"].append("")
                    self.current["lines"].append(value.upper())
                    self.pending_table_title = value
                else:
                    self.current["lines"].append(("• " if tag == "li" else "") + value)
            self.capture = None
            self.parts = []

    def handle_data(self, data):
        if self.cell is not None:
            self.cell.append(data)
        elif self.capture:
            self.parts.append(data)


def normalize_recipe_table(table):
    rows = []
    reagent_count = 0
    for raw in table["rows"]:
        cells = [cell for cell in raw if cell]
        if "=" not in cells:
            continue
        equals = cells.index("=")
        reagents = cells[:equals]
        outcome = "\n".join(cells[equals + 1:])
        reagent_count = max(reagent_count, len(reagents))
        rows.append((reagents, outcome))
    if rows:
        source_headers = [cell for cell in table["headers"] if cell]
        result_name = source_headers[-1] if source_headers else "Outcome"
        headers = [f"Reagent {index + 1}" for index in range(reagent_count)] + ["=", result_name]
        normalized_rows = []
        for reagents, outcome in rows:
            normalized_rows.append(reagents + [""] * (reagent_count - len(reagents)) + ["=", outcome])
        return {"title": table["title"], "headers": headers, "rows": normalized_rows}

    width = max([len(table["headers"]), *(len(row) for row in table["rows"])], default=0)
    headers = list(table["headers"]) + [""] * (width - len(table["headers"]))
    raw_rows = [list(row) + [""] * (width - len(row)) for row in table["rows"] if any(row)]
    return {"title": table["title"], "headers": headers, "rows": raw_rows}


def normalize_recipe_tables(table):
    tables = []
    current = {"title": table["title"], "headers": table["headers"], "rows": []}
    for row in table["rows"]:
        values = [cell for cell in row if cell]
        divider = len(values) == 1 and not values[0].startswith("^") and (
            values[0].isupper() or values[0] == "Vanilla Sunder Charms are Below")
        if divider:
            if current["rows"]:
                tables.append(normalize_recipe_table(current))
            current = {"title": values[0], "headers": table["headers"], "rows": []}
        else:
            current["rows"].append(row)
    if current["rows"]:
        tables.append(normalize_recipe_table(current))
    return tables


def parse_page(path):
    parser = GuideParser()
    parser.feed(path.read_text(encoding="utf-8"))
    sections = parser.sections
    if path.name == "ItemEnchants.html":
        sections = [section for section in sections if section["name"] != "Item Enchant System"]
    for section in sections:
        for table in section["tables"]:
            section["lines"].extend(cell for row in table["rows"] for cell in row
                                    if cell.startswith("^"))
        while section["lines"] and not section["lines"][-1]:
            section["lines"].pop()
        if not section["lines"]:
            section["lines"].append("")
        section["tables"] = [normalized for table in section["tables"] if table["rows"]
                             for normalized in normalize_recipe_tables(table)]
    return sections


def corruption_tables(raw_table):
    groups = []
    active = []
    current = {}
    for row in raw_table["rows"]:
        values = [cell for cell in row if cell]
        gear_names = [cell for cell in values if cell in CORRUPTION_GEARS]
        if gear_names:
            active = gear_names
            for gear in active:
                table = {"title": gear, "headers": ["Corruption", "Range"], "rows": []}
                groups.append(table)
                current[gear] = table
            continue
        if not active or values == ["Corruption", "Range"] * len(active):
            continue
        if len(row) >= len(active) * 2:
            for index, gear in enumerate(active):
                effect = row[index * 2].strip()
                value = row[index * 2 + 1].strip()
                if effect or value:
                    current[gear]["rows"].append([effect, value])
    return [table for table in groups if table["rows"]]


def parse_orbs(path):
    parser = GuideParser(section_tag="h1", subsection_tag="h2")
    parser.feed(path.read_text(encoding="utf-8"))
    sections = [section for section in parser.sections if section["name"] in ORB_NAMES]
    for section in sections:
        if section["name"] == "Orb of Corruption":
            lines = section["lines"]
            affixes = next((i for i, value in enumerate(lines) if value == "CORRUPTION AFFIXES BY GEAR"), len(lines))
            drop_note = next((i for i, value in enumerate(lines)
                              if value.startswith("An Orb of Corruption will always drop")), affixes)
            section["lines"] = lines[:drop_note] + ([""] + lines[affixes:] if affixes < len(lines) else [])
            table_start = next((i for i, value in enumerate(section["lines"])
                                if value.startswith("Amulet | Ring | Chest")), len(section["lines"]))
            section["lines"] = section["lines"][:table_start]
            section["tables"] = corruption_tables(section["tables"][-1]) if section["tables"] else []
        else:
            section["tables"] = []
        while section["lines"] and not section["lines"][-1]:
            section["lines"].pop()
        if not section["lines"]:
            section["lines"].append("")
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
        sections = parse_orbs(source) if tab == "orbs" else parse_page(source)
        counts[tab] = len(sections)
        for order, section in enumerate(sections):
            lines = section["lines"]
            tables = section["tables"]
            table_search = [cell for table in tables for row in table["rows"] for cell in row]
            records.append({
                "id": f"{tab}:{order:02d}:{section['name']}",
                "tab": tab,
                "name": section["name"],
                "fields": {"source": [relative]},
                "numbers": {"order": order},
                "properties": [],
                "display_lines": lines,
                "guide_tables": tables,
                "source_ref": {"file": relative, "section": section["name"]},
                "search_text": "\n".join([section["name"], *lines, *table_search]),
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
