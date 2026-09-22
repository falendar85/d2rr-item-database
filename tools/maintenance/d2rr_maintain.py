#!/usr/bin/env python3
"""D2RR Item Database maintenance helper.

Standard-library-only toolkit for:
- verifying a v0.5.3-style baseline package
- checking pinned upstream files for changes
- downloading reproducible source snapshots
- diffing upstream snapshots at item/section level
- diffing normalized database.json / guides.json files
- validating normalized data before release
- staging a candidate data package and refreshing SHA256SUMS.txt

This tool intentionally does NOT attempt to reproduce the project's normalization/
rendering logic from raw upstream files. It makes upstream changes and normalized
output changes deterministic and reviewable, then leaves only the raw->normalized
conversion/build integration to the project generator/Codex.
"""
from __future__ import annotations

import argparse
import copy
import dataclasses
import difflib
import hashlib
import html.parser
import json
import os
from pathlib import Path
import re
import shutil
import sys
import tempfile
import urllib.error
import urllib.request
from collections import Counter, defaultdict
from typing import Any, Iterable

TOOL_VERSION = "1.0.0"
HEX64 = re.compile(r"^[0-9a-fA-F]{64}$")

# Provenance keys used in the normalized JSON -> actual repository paths.
SOURCE_GROUPS = {
    "website": {
        "repo": "D2R-Reimagined/d2r-reimagined-website",
        "default_branch": "master",
        "sources": {
            "keyed/uniques.json": "static/data/keyed/uniques.json",
            "keyed/sets.json": "static/data/keyed/sets.json",
            "keyed/runewords.json": "static/data/keyed/runewords.json",
            "keyed/armors.json": "static/data/keyed/armors.json",
            "keyed/weapons.json": "static/data/keyed/weapons.json",
            "keyed/ias-calculator.json": "static/data/keyed/ias-calculator.json",
            "strings/enUS.json": "static/data/strings/enUS.json",
        },
        "normalized_file": "database.json",
    },
    "wiki": {
        "repo": "D2R-Reimagined/wiki-content",
        "default_branch": "main",
        "sources": {
            "recipes/CubeRecipes.html": "recipes/CubeRecipes.html",
            "recipes/ItemEnchants.html": "recipes/ItemEnchants.html",
            "recipes/Crafting.html": "recipes/Crafting.html",
            "Items/Orbs.html": "Items/Orbs.html",
        },
        "normalized_file": "guides.json",
    },
}

DB_TABS = {"uniques", "sets", "runewords", "bases"}
GUIDE_TABS = {"cube-recipes", "item-enchants", "item-crafting", "orbs"}


def read_json(path: Path) -> Any:
    with path.open("r", encoding="utf-8") as f:
        return json.load(f)


def write_json(path: Path, obj: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="\n") as f:
        json.dump(obj, f, ensure_ascii=False, indent=2, sort_keys=False)
        f.write("\n")


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def http_bytes(url: str, timeout: int = 30) -> bytes:
    req = urllib.request.Request(
        url,
        headers={"User-Agent": f"D2RR-Item-Database-Maintenance/{TOOL_VERSION}"},
    )
    try:
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            return resp.read()
    except urllib.error.HTTPError as e:
        raise RuntimeError(f"HTTP {e.code} for {url}") from e
    except urllib.error.URLError as e:
        raise RuntimeError(f"Network error for {url}: {e.reason}") from e


def github_latest_sha(repo: str, branch: str) -> str:
    url = f"https://api.github.com/repos/{repo}/commits/{branch}"
    obj = json.loads(http_bytes(url).decode("utf-8"))
    sha = obj.get("sha")
    if not isinstance(sha, str) or not sha:
        raise RuntimeError(f"GitHub did not return a commit SHA for {repo}@{branch}")
    return sha


def github_raw(repo: str, revision: str, path: str) -> bytes:
    url = f"https://raw.githubusercontent.com/{repo}/{revision}/{path}"
    return http_bytes(url)


def load_lock(data_dir: Path) -> dict[str, Any]:
    path = data_dir / "upstream.lock.json"
    if not path.exists():
        raise FileNotFoundError(f"Missing {path}")
    obj = read_json(path)
    if not isinstance(obj, dict):
        raise ValueError("upstream.lock.json must contain an object")
    return obj


def parse_sha256s(path: Path) -> dict[str, str]:
    out: dict[str, str] = {}
    if not path.exists():
        return out
    for raw in path.read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if not line:
            continue
        parts = line.split(None, 1)
        if len(parts) != 2:
            continue
        digest, name = parts
        name = name.lstrip("*").strip()
        out[name] = digest.lower()
    return out


def verify_sha256s(data_dir: Path) -> tuple[list[str], list[str]]:
    sums_path = data_dir / "SHA256SUMS.txt"
    sums = parse_sha256s(sums_path)
    errors: list[str] = []
    notes: list[str] = []
    if not sums:
        errors.append("SHA256SUMS.txt is missing or empty")
        return errors, notes
    for rel, expected in sums.items():
        p = data_dir / rel
        if not p.exists():
            errors.append(f"Checksum target missing: {rel}")
            continue
        actual = sha256_file(p)
        if actual.lower() != expected.lower():
            errors.append(f"Checksum mismatch: {rel}: expected {expected}, got {actual}")
        else:
            notes.append(f"OK checksum {rel}")
    return errors, notes


def validate_provenance(obj: dict[str, Any], label: str) -> tuple[list[str], list[str]]:
    errors: list[str] = []
    warnings: list[str] = []
    prov = obj.get("provenance")
    if not isinstance(prov, dict):
        return [f"{label}: missing provenance object"], warnings
    for key in ("repository", "revision", "source_hashes"):
        if key not in prov:
            errors.append(f"{label}: provenance missing {key}")
    hashes = prov.get("source_hashes")
    if isinstance(hashes, dict):
        for k, v in hashes.items():
            if not isinstance(v, str) or not HEX64.fullmatch(v):
                errors.append(f"{label}: invalid SHA-256 for source {k!r}: {v!r}")
    elif hashes is not None:
        errors.append(f"{label}: provenance.source_hashes must be an object")
    return errors, warnings


def validate_normalized(path: Path, kind: str | None = None) -> dict[str, Any]:
    obj = read_json(path)
    errors: list[str] = []
    warnings: list[str] = []
    if not isinstance(obj, dict):
        return {"path": str(path), "errors": ["root is not an object"], "warnings": []}
    if obj.get("schema_version") != 1:
        errors.append(f"schema_version is {obj.get('schema_version')!r}, expected 1")
    e, w = validate_provenance(obj, path.name)
    errors += e
    warnings += w
    records = obj.get("records")
    if not isinstance(records, list):
        errors.append("records is not a list")
        records = []

    if kind is None:
        tabs_seen = {r.get("tab") for r in records if isinstance(r, dict)}
        if tabs_seen and tabs_seen <= DB_TABS:
            kind = "database"
        elif tabs_seen and tabs_seen <= GUIDE_TABS:
            kind = "guides"

    allowed_tabs = DB_TABS if kind == "database" else GUIDE_TABS if kind == "guides" else None
    ids: set[str] = set()
    tabs = Counter()
    source_keys = Counter()
    for i, rec in enumerate(records):
        prefix = f"record[{i}]"
        if not isinstance(rec, dict):
            errors.append(f"{prefix}: not an object")
            continue
        rid = rec.get("id")
        if not isinstance(rid, str) or not rid:
            errors.append(f"{prefix}: missing/non-string id")
        elif rid in ids:
            errors.append(f"{prefix}: duplicate id {rid!r}")
        else:
            ids.add(rid)
        tab = rec.get("tab")
        if not isinstance(tab, str) or not tab:
            errors.append(f"{prefix}: missing/non-string tab")
        else:
            tabs[tab] += 1
            if allowed_tabs is not None and tab not in allowed_tabs:
                errors.append(f"{prefix}: unexpected tab {tab!r}")
        name = rec.get("name")
        if not isinstance(name, str) or not name:
            errors.append(f"{prefix}: missing/non-string name")
        display = rec.get("display_lines")
        if not isinstance(display, list) or not all(isinstance(x, str) for x in display):
            errors.append(f"{prefix}: display_lines must be a string list")
        elif kind == "database" and name and display and display[0] != name:
            warnings.append(f"{prefix} {rid!r}: first display line differs from name")
        search = rec.get("search_text")
        if not isinstance(search, str):
            errors.append(f"{prefix}: search_text must be a string")
        elif name and name not in search:
            warnings.append(f"{prefix} {rid!r}: search_text does not contain item name")
        sref = rec.get("source_ref")
        if not isinstance(sref, dict) or not isinstance(sref.get("file"), str):
            errors.append(f"{prefix}: missing/invalid source_ref.file")
        else:
            source_keys[sref["file"]] += 1

        if kind == "database":
            if not isinstance(rec.get("fields"), dict):
                errors.append(f"{prefix}: fields must be an object")
            if not isinstance(rec.get("numbers"), dict):
                errors.append(f"{prefix}: numbers must be an object")
            props = rec.get("properties")
            if not isinstance(props, list):
                errors.append(f"{prefix}: properties must be a list")
            else:
                for j, prop in enumerate(props):
                    pp = f"{prefix}.properties[{j}]"
                    if not isinstance(prop, dict):
                        errors.append(f"{pp}: not an object")
                        continue
                    for req in ("property_id", "canonical_name", "text", "raw"):
                        if req not in prop:
                            errors.append(f"{pp}: missing {req}")
                    if "raw" in prop and not isinstance(prop["raw"], dict):
                        errors.append(f"{pp}: raw must be an object")
        elif kind == "guides":
            tables = rec.get("guide_tables")
            if not isinstance(tables, list):
                errors.append(f"{prefix}: guide_tables must be a list")
            else:
                for j, table in enumerate(tables):
                    tp = f"{prefix}.guide_tables[{j}]"
                    if not isinstance(table, dict):
                        errors.append(f"{tp}: not an object")
                        continue
                    headers = table.get("headers")
                    rows = table.get("rows")
                    if not isinstance(headers, list) or not all(isinstance(x, str) for x in headers):
                        errors.append(f"{tp}: headers must be a string list")
                    if not isinstance(rows, list):
                        errors.append(f"{tp}: rows must be a list")
                    elif isinstance(headers, list):
                        for k, row in enumerate(rows):
                            if not isinstance(row, list):
                                errors.append(f"{tp}.rows[{k}]: row must be a list")
                            elif len(row) != len(headers):
                                warnings.append(
                                    f"{tp}.rows[{k}]: {len(row)} cells for {len(headers)} headers"
                                )

    prov = obj.get("provenance", {})
    counts = prov.get("category_counts") if isinstance(prov, dict) else None
    if isinstance(counts, dict):
        for tab, expected in counts.items():
            if tabs.get(tab, 0) != expected:
                errors.append(
                    f"provenance.category_counts[{tab!r}]={expected} but records contain {tabs.get(tab, 0)}"
                )
    return {
        "path": str(path),
        "kind": kind,
        "record_count": len(records),
        "tab_counts": dict(sorted(tabs.items())),
        "source_ref_counts": dict(sorted(source_keys.items())),
        "errors": errors,
        "warnings": warnings,
    }


def _summarize_scalar(v: Any, max_len: int = 120) -> str:
    s = json.dumps(v, ensure_ascii=False, sort_keys=True)
    if len(s) > max_len:
        s = s[: max_len - 3] + "..."
    return s


def deep_diff(a: Any, b: Any, path: str = "", limit: int = 200) -> list[dict[str, Any]]:
    out: list[dict[str, Any]] = []

    def rec(x: Any, y: Any, p: str) -> None:
        if len(out) >= limit:
            return
        if type(x) is not type(y):
            out.append({"path": p or "$", "old": x, "new": y})
            return
        if isinstance(x, dict):
            keys = sorted(set(x) | set(y))
            for k in keys:
                np = f"{p}.{k}" if p else str(k)
                if k not in x:
                    out.append({"path": np, "old": "<missing>", "new": y[k]})
                elif k not in y:
                    out.append({"path": np, "old": x[k], "new": "<missing>"})
                else:
                    rec(x[k], y[k], np)
                if len(out) >= limit:
                    return
        elif isinstance(x, list):
            if x == y:
                return
            # For properties, try matching by raw key + occurrence number to keep stat diffs readable.
            if p.endswith("properties") and all(isinstance(v, dict) for v in x + y):
                def keyed(seq: list[dict[str, Any]]) -> dict[str, Any]:
                    ctr: Counter[str] = Counter()
                    res = {}
                    for item in seq:
                        raw = item.get("raw") if isinstance(item.get("raw"), dict) else {}
                        base = str(raw.get("key") or item.get("property_id") or "property")
                        ctr[base] += 1
                        res[f"{base}#{ctr[base]}"] = item
                    return res
                rec(keyed(x), keyed(y), p)
                return
            m = max(len(x), len(y))
            for i in range(m):
                np = f"{p}[{i}]"
                if i >= len(x):
                    out.append({"path": np, "old": "<missing>", "new": y[i]})
                elif i >= len(y):
                    out.append({"path": np, "old": x[i], "new": "<missing>"})
                else:
                    rec(x[i], y[i], np)
                if len(out) >= limit:
                    return
        elif x != y:
            out.append({"path": p or "$", "old": x, "new": y})

    rec(a, b, path)
    return out


def stable_source_identity(rec: dict[str, Any]) -> str | None:
    s = rec.get("source_ref")
    if not isinstance(s, dict):
        return None
    f = s.get("file")
    if not isinstance(f, str):
        return None
    # Prefer semantic identifiers over row numbers because rows shift when items are inserted.
    for key in ("index", "section", "code"):
        v = s.get(key)
        if v is not None and str(v):
            return f"{f}|{key}={v}"
    if "row" in s:
        return f"{f}|row={s['row']}"
    return f


def diff_normalized(old_path: Path, new_path: Path, detail_limit: int = 80) -> dict[str, Any]:
    old = read_json(old_path)
    new = read_json(new_path)
    old_records = {r["id"]: r for r in old.get("records", []) if isinstance(r, dict) and isinstance(r.get("id"), str)}
    new_records = {r["id"]: r for r in new.get("records", []) if isinstance(r, dict) and isinstance(r.get("id"), str)}
    added_ids = sorted(set(new_records) - set(old_records))
    removed_ids = sorted(set(old_records) - set(new_records))
    shared = sorted(set(old_records) & set(new_records))
    changed_ids = [rid for rid in shared if old_records[rid] != new_records[rid]]

    old_source = defaultdict(list)
    new_source = defaultdict(list)
    for rid in removed_ids:
        ident = stable_source_identity(old_records[rid])
        if ident:
            old_source[ident].append(rid)
    for rid in added_ids:
        ident = stable_source_identity(new_records[rid])
        if ident:
            new_source[ident].append(rid)
    likely_renames = []
    for ident in sorted(set(old_source) & set(new_source)):
        for old_id in old_source[ident]:
            for new_id in new_source[ident]:
                likely_renames.append({"source": ident, "old_id": old_id, "new_id": new_id})

    changed = []
    for rid in changed_ids:
        a, b = old_records[rid], new_records[rid]
        diffs = deep_diff(a, b, limit=detail_limit)
        changed.append({
            "id": rid,
            "tab": b.get("tab") or a.get("tab"),
            "name": b.get("name") or a.get("name"),
            "diffs": diffs,
            "diff_truncated": len(diffs) >= detail_limit,
        })

    def tab_count(ids: Iterable[str], records: dict[str, dict[str, Any]]) -> dict[str, int]:
        c = Counter(records[i].get("tab", "<unknown>") for i in ids)
        return dict(sorted(c.items()))

    return {
        "old": str(old_path),
        "new": str(new_path),
        "old_record_count": len(old_records),
        "new_record_count": len(new_records),
        "added_count": len(added_ids),
        "removed_count": len(removed_ids),
        "changed_count": len(changed_ids),
        "unchanged_count": len(shared) - len(changed_ids),
        "added_by_tab": tab_count(added_ids, new_records),
        "removed_by_tab": tab_count(removed_ids, old_records),
        "changed_by_tab": tab_count(changed_ids, new_records),
        "added": [{"id": rid, "tab": new_records[rid].get("tab"), "name": new_records[rid].get("name")} for rid in added_ids],
        "removed": [{"id": rid, "tab": old_records[rid].get("tab"), "name": old_records[rid].get("name")} for rid in removed_ids],
        "likely_renames": likely_renames,
        "changed": changed,
        "old_provenance": old.get("provenance"),
        "new_provenance": new.get("provenance"),
    }


def format_normalized_diff(report: dict[str, Any]) -> str:
    lines = []
    lines.append("D2RR normalized data change report")
    lines.append("=" * 36)
    lines.append(f"Old records: {report['old_record_count']}")
    lines.append(f"New records: {report['new_record_count']}")
    lines.append(f"Added:       {report['added_count']} {report['added_by_tab']}")
    lines.append(f"Removed:     {report['removed_count']} {report['removed_by_tab']}")
    lines.append(f"Changed:     {report['changed_count']} {report['changed_by_tab']}")
    lines.append(f"Unchanged:   {report['unchanged_count']}")
    if report["likely_renames"]:
        lines += ["", "Possible renames / ID changes:"]
        for r in report["likely_renames"]:
            lines.append(f"  {r['old_id']} -> {r['new_id']}  ({r['source']})")
    if report["added"]:
        lines += ["", "Added records:"]
        for r in report["added"]:
            lines.append(f"  + [{r['tab']}] {r['name']} ({r['id']})")
    if report["removed"]:
        lines += ["", "Removed records:"]
        for r in report["removed"]:
            lines.append(f"  - [{r['tab']}] {r['name']} ({r['id']})")
    if report["changed"]:
        lines += ["", "Changed records:"]
        for rec in report["changed"]:
            lines.append(f"  ~ [{rec['tab']}] {rec['name']} ({rec['id']})")
            for d in rec["diffs"]:
                lines.append(
                    f"      {d['path']}: {_summarize_scalar(d['old'])} -> {_summarize_scalar(d['new'])}"
                )
            if rec.get("diff_truncated"):
                lines.append("      ... detail limit reached ...")
    lines.append("")
    return "\n".join(lines)


class SectionHTMLParser(html.parser.HTMLParser):
    def __init__(self) -> None:
        super().__init__(convert_charrefs=True)
        self.sections: dict[str, list[str]] = defaultdict(list)
        self.current = "<document>"
        self.in_heading = False
        self.heading_parts: list[str] = []

    def handle_starttag(self, tag: str, attrs: list[tuple[str, str | None]]) -> None:
        if tag.lower() in {"h1", "h2", "h3", "h4"}:
            self.in_heading = True
            self.heading_parts = []

    def handle_endtag(self, tag: str) -> None:
        if tag.lower() in {"h1", "h2", "h3", "h4"} and self.in_heading:
            h = " ".join("".join(self.heading_parts).split())
            if h:
                self.current = h
            self.in_heading = False

    def handle_data(self, data: str) -> None:
        text = " ".join(data.split())
        if not text:
            return
        if self.in_heading:
            self.heading_parts.append(text)
        else:
            self.sections[self.current].append(text)


def html_sections(data: bytes) -> dict[str, str]:
    p = SectionHTMLParser()
    p.feed(data.decode("utf-8", errors="replace"))
    return {k: " ".join(v) for k, v in p.sections.items() if " ".join(v).strip()}


def infer_json_list_key(items: list[Any], rel: str) -> str | None:
    if rel.endswith("sets.json"):
        return "Index"
    candidates = ["Index", "NameKey", "Key", "Code", "id", "name"]
    objs = [x for x in items if isinstance(x, dict)]
    if not objs:
        return None
    for c in candidates:
        vals = [o.get(c) for o in objs]
        if all(v is not None and str(v) for v in vals) and len({str(v) for v in vals}) == len(vals):
            return c
    return None


def flatten_source_json(obj: Any, rel: str) -> dict[str, Any] | None:
    # sets.json: report individual set items rather than only whole-set blobs.
    if rel.endswith("sets.json") and isinstance(obj, list):
        out: dict[str, Any] = {}
        for s in obj:
            if not isinstance(s, dict):
                continue
            set_name = str(s.get("Index", "<set>"))
            header = {k: v for k, v in s.items() if k != "SetItems"}
            out[f"SET::{set_name}"] = header
            for item in s.get("SetItems", []) or []:
                if isinstance(item, dict):
                    item_name = str(item.get("Index", "<item>"))
                    out[f"ITEM::{set_name}::{item_name}"] = item
        return out
    if isinstance(obj, dict):
        return {str(k): v for k, v in obj.items()}
    if isinstance(obj, list):
        key = infer_json_list_key(obj, rel)
        if key:
            return {str(x[key]): x for x in obj if isinstance(x, dict) and key in x}
    return None


def source_file_diff(old_data: bytes, new_data: bytes, rel: str, detail_limit: int = 50) -> dict[str, Any]:
    result: dict[str, Any] = {
        "path": rel,
        "old_sha256": sha256_bytes(old_data),
        "new_sha256": sha256_bytes(new_data),
        "changed": old_data != new_data,
    }
    if old_data == new_data:
        return result
    suffix = Path(rel).suffix.lower()
    if suffix == ".json":
        try:
            a = json.loads(old_data.decode("utf-8"))
            b = json.loads(new_data.decode("utf-8"))
            am = flatten_source_json(a, rel)
            bm = flatten_source_json(b, rel)
            if am is not None and bm is not None:
                added = sorted(set(bm) - set(am))
                removed = sorted(set(am) - set(bm))
                shared = sorted(set(am) & set(bm))
                changed = [k for k in shared if am[k] != bm[k]]
                result.update({
                    "format": "json-keyed",
                    "added_count": len(added),
                    "removed_count": len(removed),
                    "changed_count": len(changed),
                    "added": added,
                    "removed": removed,
                    "changed_entries": [
                        {"key": k, "diffs": deep_diff(am[k], bm[k], limit=detail_limit)}
                        for k in changed
                    ],
                })
            else:
                result.update({"format": "json", "diffs": deep_diff(a, b, limit=detail_limit)})
        except Exception as e:
            result.update({"format": "json-unparsed", "note": str(e)})
    elif suffix in {".html", ".htm"}:
        a = html_sections(old_data)
        b = html_sections(new_data)
        result.update({
            "format": "html-sections",
            "added_sections": sorted(set(b) - set(a)),
            "removed_sections": sorted(set(a) - set(b)),
            "changed_sections": sorted(k for k in set(a) & set(b) if a[k] != b[k]),
        })
    else:
        a = old_data.decode("utf-8", errors="replace").splitlines()
        b = new_data.decode("utf-8", errors="replace").splitlines()
        result.update({
            "format": "text",
            "unified_diff": list(difflib.unified_diff(a, b, fromfile="old", tofile="new", n=2))[:detail_limit],
        })
    return result


def format_source_diff(report: dict[str, Any]) -> str:
    lines = ["D2RR upstream source change report", "=" * 34]
    lines.append(f"Old snapshot: {report.get('old_revision_summary','')}")
    lines.append(f"New snapshot: {report.get('new_revision_summary','')}")
    lines.append("")
    changed_files = [f for f in report["files"] if f.get("changed")]
    lines.append(f"Changed source files: {len(changed_files)} / {len(report['files'])}")
    for f in report["files"]:
        mark = "~" if f.get("changed") else "="
        lines.append(f"{mark} {f['path']}")
        if not f.get("changed"):
            continue
        fmt = f.get("format")
        if fmt == "json-keyed":
            lines.append(
                f"    entries: +{f.get('added_count',0)} -{f.get('removed_count',0)} ~{f.get('changed_count',0)}"
            )
            entry_limit = 50
            added = f.get("added", [])
            removed = f.get("removed", [])
            changed_entries = f.get("changed_entries", [])
            for x in added[:entry_limit]:
                lines.append(f"      + {x}")
            if len(added) > entry_limit:
                lines.append(f"      ... {len(added) - entry_limit} more added entries in JSON report")
            for x in removed[:entry_limit]:
                lines.append(f"      - {x}")
            if len(removed) > entry_limit:
                lines.append(f"      ... {len(removed) - entry_limit} more removed entries in JSON report")
            for c in changed_entries[:entry_limit]:
                lines.append(f"      ~ {c['key']}")
                for d in c.get("diffs", []):
                    lines.append(
                        f"          {d['path']}: {_summarize_scalar(d['old'])} -> {_summarize_scalar(d['new'])}"
                    )
            if len(changed_entries) > entry_limit:
                lines.append(f"      ... {len(changed_entries) - entry_limit} more changed entries in JSON report")
        elif fmt == "html-sections":
            for x in f.get("added_sections", []):
                lines.append(f"      + section {x}")
            for x in f.get("removed_sections", []):
                lines.append(f"      - section {x}")
            for x in f.get("changed_sections", []):
                lines.append(f"      ~ section {x}")
    lines.append("")
    return "\n".join(lines)


def snapshot_sources(data_dir: Path, out_dir: Path, use_latest: bool) -> dict[str, Any]:
    lock = load_lock(data_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    manifest = {
        "tool_version": TOOL_VERSION,
        "groups": {},
    }
    for group, cfg in SOURCE_GROUPS.items():
        lock_entry = lock.get(group, {}) if isinstance(lock.get(group), dict) else {}
        repo = str(lock_entry.get("repo") or cfg["repo"])
        branch = str(lock_entry.get("branch") or cfg["default_branch"])
        revision = github_latest_sha(repo, branch) if use_latest else str(lock_entry.get("sha"))
        if not revision:
            raise RuntimeError(f"No revision available for {group}")
        g = {"repo": repo, "branch": branch, "revision": revision, "files": {}}
        for prov_key, repo_path in cfg["sources"].items():
            data = github_raw(repo, revision, repo_path)
            dest = out_dir / group / prov_key
            dest.parent.mkdir(parents=True, exist_ok=True)
            dest.write_bytes(data)
            g["files"][prov_key] = {
                "repo_path": repo_path,
                "sha256": sha256_bytes(data),
                "size": len(data),
                "snapshot_path": str(Path(group) / prov_key).replace("\\", "/"),
            }
        manifest["groups"][group] = g
    write_json(out_dir / "snapshot_manifest.json", manifest)
    return manifest


def compare_snapshot_dirs(old_dir: Path, new_dir: Path, detail_limit: int = 50) -> dict[str, Any]:
    old_m = read_json(old_dir / "snapshot_manifest.json")
    new_m = read_json(new_dir / "snapshot_manifest.json")
    files = []
    for group in SOURCE_GROUPS:
        old_g = old_m.get("groups", {}).get(group, {})
        new_g = new_m.get("groups", {}).get(group, {})
        keys = sorted(set(old_g.get("files", {})) | set(new_g.get("files", {})))
        for key in keys:
            op = old_dir / group / key
            np = new_dir / group / key
            if not op.exists() or not np.exists():
                files.append({
                    "path": f"{group}/{key}",
                    "changed": True,
                    "missing_old": not op.exists(),
                    "missing_new": not np.exists(),
                })
                continue
            fd = source_file_diff(op.read_bytes(), np.read_bytes(), key, detail_limit=detail_limit)
            fd["group"] = group
            fd["path"] = f"{group}/{key}"
            files.append(fd)
    old_summary = ", ".join(
        f"{g}={old_m.get('groups',{}).get(g,{}).get('revision','?')[:12]}" for g in SOURCE_GROUPS
    )
    new_summary = ", ".join(
        f"{g}={new_m.get('groups',{}).get(g,{}).get('revision','?')[:12]}" for g in SOURCE_GROUPS
    )
    return {"old_revision_summary": old_summary, "new_revision_summary": new_summary, "files": files}


def check_upstream(data_dir: Path) -> dict[str, Any]:
    lock = load_lock(data_dir)
    report = {"groups": {}}
    for group, cfg in SOURCE_GROUPS.items():
        norm = read_json(data_dir / cfg["normalized_file"])
        prov = norm.get("provenance", {})
        pinned_hashes = prov.get("source_hashes", {}) if isinstance(prov, dict) else {}
        lock_entry = lock.get(group, {}) if isinstance(lock.get(group), dict) else {}
        repo = str(lock_entry.get("repo") or cfg["repo"])
        branch = str(lock_entry.get("branch") or cfg["default_branch"])
        pinned_revision = str(lock_entry.get("sha") or prov.get("revision") or "")
        latest_revision = github_latest_sha(repo, branch)
        files = []
        for prov_key, repo_path in cfg["sources"].items():
            latest_data = github_raw(repo, latest_revision, repo_path)
            latest_hash = sha256_bytes(latest_data)
            pinned_hash = pinned_hashes.get(prov_key)
            files.append({
                "source": prov_key,
                "repo_path": repo_path,
                "pinned_sha256": pinned_hash,
                "latest_sha256": latest_hash,
                "changed": pinned_hash != latest_hash,
            })
        report["groups"][group] = {
            "repo": repo,
            "branch": branch,
            "pinned_revision": pinned_revision,
            "latest_revision": latest_revision,
            "revision_changed": pinned_revision != latest_revision,
            "files": files,
        }
    return report


def format_upstream_check(report: dict[str, Any]) -> str:
    lines = ["D2RR upstream check", "=" * 19]
    any_changes = False
    for group, g in report["groups"].items():
        changed = [f for f in g["files"] if f["changed"]]
        any_changes |= bool(changed)
        lines.append(
            f"{group}: {g['pinned_revision'][:12]} -> {g['latest_revision'][:12]} "
            f"({len(changed)} source file(s) changed)"
        )
        for f in g["files"]:
            mark = "CHANGED" if f["changed"] else "same"
            lines.append(f"  {mark:7} {f['source']}")
    lines.append("")
    lines.append("UPDATE NEEDED" if any_changes else "No tracked source content changed.")
    lines.append("")
    return "\n".join(lines)


def stage_candidate(baseline_dir: Path, candidate_dir: Path, out_dir: Path) -> dict[str, Any]:
    db = candidate_dir / "database.json"
    guides = candidate_dir / "guides.json"
    if not db.exists() or not guides.exists():
        raise FileNotFoundError("candidate directory must contain database.json and guides.json")
    validations = [validate_normalized(db, "database"), validate_normalized(guides, "guides")]
    errors = [e for v in validations for e in v["errors"]]
    if errors:
        raise RuntimeError("Candidate validation failed:\n- " + "\n- ".join(errors[:100]))
    if out_dir.exists():
        shutil.rmtree(out_dir)
    out_dir.mkdir(parents=True)
    # Start from baseline so docs/lock are preserved unless candidate explicitly supplies replacements.
    for name in ("database.json", "guides.json", "SOURCES.md", "upstream.lock.json"):
        src = candidate_dir / name
        if not src.exists():
            src = baseline_dir / name
        if src.exists():
            shutil.copy2(src, out_dir / name)
    sums = []
    for name in ("database.json", "guides.json", "SOURCES.md", "upstream.lock.json"):
        p = out_dir / name
        if p.exists():
            sums.append(f"{sha256_file(p)}  {name}")
    (out_dir / "SHA256SUMS.txt").write_text("\n".join(sums) + "\n", encoding="utf-8", newline="\n")
    db_diff = diff_normalized(baseline_dir / "database.json", out_dir / "database.json")
    guide_diff = diff_normalized(baseline_dir / "guides.json", out_dir / "guides.json")
    write_json(out_dir / "CHANGE_REPORT.database.json", db_diff)
    (out_dir / "CHANGE_REPORT.database.txt").write_text(format_normalized_diff(db_diff), encoding="utf-8")
    write_json(out_dir / "CHANGE_REPORT.guides.json", guide_diff)
    (out_dir / "CHANGE_REPORT.guides.txt").write_text(format_normalized_diff(guide_diff), encoding="utf-8")
    return {
        "out_dir": str(out_dir),
        "validations": validations,
        "database": {k: db_diff[k] for k in ("added_count", "removed_count", "changed_count", "unchanged_count")},
        "guides": {k: guide_diff[k] for k in ("added_count", "removed_count", "changed_count", "unchanged_count")},
    }


def command_verify(args: argparse.Namespace) -> int:
    d = Path(args.data_dir)
    validations = []
    for name, kind in (("database.json", "database"), ("guides.json", "guides")):
        p = d / name
        if not p.exists():
            validations.append({"path": str(p), "errors": ["missing"], "warnings": []})
        else:
            validations.append(validate_normalized(p, kind))
    sum_errors, sum_notes = verify_sha256s(d)
    errors = sum((v.get("errors", []) for v in validations), []) + sum_errors
    warnings = sum((v.get("warnings", []) for v in validations), [])
    for v in validations:
        print(f"{Path(v['path']).name}: records={v.get('record_count','?')} tabs={v.get('tab_counts',{})}")
        for w in v.get("warnings", []):
            print(f"  WARNING: {w}")
        for e in v.get("errors", []):
            print(f"  ERROR: {e}")
    for n in sum_notes:
        print(n)
    for e in sum_errors:
        print(f"ERROR: {e}")
    print(f"Result: {'PASS' if not errors else 'FAIL'} ({len(errors)} errors, {len(warnings)} warnings)")
    return 0 if not errors else 2


def command_check_upstream(args: argparse.Namespace) -> int:
    report = check_upstream(Path(args.data_dir))
    text = format_upstream_check(report)
    print(text, end="")
    if args.report_json:
        write_json(Path(args.report_json), report)
    return 0


def command_snapshot(args: argparse.Namespace) -> int:
    manifest = snapshot_sources(Path(args.data_dir), Path(args.out), args.latest)
    print(f"Wrote snapshot to {args.out}")
    for group, g in manifest["groups"].items():
        print(f"  {group}: {g['revision']} ({len(g['files'])} files)")
    return 0


def command_diff_sources(args: argparse.Namespace) -> int:
    report = compare_snapshot_dirs(Path(args.old), Path(args.new), args.detail_limit)
    txt = format_source_diff(report)
    print(txt, end="")
    if args.report_dir:
        rd = Path(args.report_dir)
        rd.mkdir(parents=True, exist_ok=True)
        write_json(rd / "source_changes.json", report)
        (rd / "source_changes.txt").write_text(txt, encoding="utf-8")
    return 0


def command_diff_data(args: argparse.Namespace) -> int:
    report = diff_normalized(Path(args.old), Path(args.new), args.detail_limit)
    txt = format_normalized_diff(report)
    print(txt, end="")
    if args.report_dir:
        rd = Path(args.report_dir)
        rd.mkdir(parents=True, exist_ok=True)
        stem = args.label or Path(args.new).stem
        write_json(rd / f"{stem}.changes.json", report)
        (rd / f"{stem}.changes.txt").write_text(txt, encoding="utf-8")
    return 0


def command_validate(args: argparse.Namespace) -> int:
    result = validate_normalized(Path(args.file), args.kind)
    print(json.dumps(result, ensure_ascii=False, indent=2))
    return 0 if not result["errors"] else 2


def command_stage(args: argparse.Namespace) -> int:
    result = stage_candidate(Path(args.baseline), Path(args.candidate), Path(args.out))
    print(json.dumps(result, ensure_ascii=False, indent=2))
    return 0


def make_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(
        description="Maintenance helper for the D2RR Item Database normalized data package."
    )
    p.add_argument("--version", action="version", version=TOOL_VERSION)
    sub = p.add_subparsers(dest="command", required=True)

    s = sub.add_parser("verify-baseline", help="Validate normalized files and SHA256SUMS.txt")
    s.add_argument("data_dir")
    s.set_defaults(func=command_verify)

    s = sub.add_parser("check-upstream", help="Compare tracked source hashes to the current upstream branches")
    s.add_argument("data_dir")
    s.add_argument("--report-json")
    s.set_defaults(func=command_check_upstream)

    s = sub.add_parser("snapshot", help="Download all tracked upstream source files")
    s.add_argument("data_dir", help="Directory containing upstream.lock.json")
    s.add_argument("out")
    g = s.add_mutually_exclusive_group()
    g.add_argument("--latest", action="store_true", help="Snapshot current branch heads")
    g.add_argument("--pinned", action="store_true", help="Snapshot revisions pinned in upstream.lock.json (default)")
    s.set_defaults(func=command_snapshot)

    s = sub.add_parser("diff-sources", help="Compare two source snapshots")
    s.add_argument("old")
    s.add_argument("new")
    s.add_argument("--report-dir")
    s.add_argument("--detail-limit", type=int, default=50)
    s.set_defaults(func=command_diff_sources)

    s = sub.add_parser("diff-data", help="Compare two normalized database/guides JSON files by record ID")
    s.add_argument("old")
    s.add_argument("new")
    s.add_argument("--report-dir")
    s.add_argument("--label")
    s.add_argument("--detail-limit", type=int, default=80)
    s.set_defaults(func=command_diff_data)

    s = sub.add_parser("validate", help="Validate one normalized JSON file")
    s.add_argument("file")
    s.add_argument("--kind", choices=["database", "guides"])
    s.set_defaults(func=command_validate)

    s = sub.add_parser("stage", help="Validate and stage a candidate data package with reports/checksums")
    s.add_argument("--baseline", required=True)
    s.add_argument("--candidate", required=True)
    s.add_argument("--out", required=True)
    s.set_defaults(func=command_stage)

    return p


def main(argv: list[str] | None = None) -> int:
    parser = make_parser()
    args = parser.parse_args(argv)
    try:
        return int(args.func(args))
    except KeyboardInterrupt:
        print("Interrupted.", file=sys.stderr)
        return 130
    except Exception as e:
        print(f"ERROR: {e}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
