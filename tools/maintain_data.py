#!/usr/bin/env python3
"""Reproducible data-update and release-preparation workflow for this project."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
MAINTENANCE_DIR = Path(__file__).resolve().parent / "maintenance"
sys.path.insert(0, str(MAINTENANCE_DIR))
import d2rr_maintain as toolkit  # noqa: E402


DATA_FILES = ("database.json", "guides.json")
SUPPORT_FILES = ("SOURCES.md", "upstream.lock.json")
GENERATED_DATABASE_FILES = ("generation-report.json", "property-catalog.json")


def read_json(path: Path) -> Any:
    return json.loads(path.read_text(encoding="utf-8"))


def write_json(path: Path, value: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + "\n", encoding="utf-8", newline="\n")


def recreate(path: Path) -> None:
    if path.exists():
        shutil.rmtree(path)
    path.mkdir(parents=True)


def write_checksums(folder: Path, names: tuple[str, ...] | None = None) -> None:
    paths = [folder / name for name in names] if names else sorted(p for p in folder.iterdir() if p.is_file() and p.name != "SHA256SUMS.txt")
    lines = [f"{toolkit.sha256_file(path)}  {path.name}" for path in paths if path.exists()]
    (folder / "SHA256SUMS.txt").write_text("\n".join(lines) + "\n", encoding="ascii", newline="\n")


def manifest_from_normalized(lock: dict[str, Any], database: dict[str, Any], guides: dict[str, Any]) -> dict[str, Any]:
    result = {"tool_version": toolkit.TOOL_VERSION, "groups": {}}
    for group, normalized in (("website", database), ("wiki", guides)):
        cfg = toolkit.SOURCE_GROUPS[group]
        entry = lock[group]
        provenance = normalized["provenance"]
        files = {}
        for key, repo_path in cfg["sources"].items():
            files[key] = {
                "repo_path": repo_path,
                "sha256": provenance["source_hashes"][key],
                "snapshot_path": f"{group}/{key}",
            }
        result["groups"][group] = {
            "repo": entry.get("repo", cfg["repo"]),
            "branch": entry.get("branch", cfg["default_branch"]),
            "revision": entry["sha"],
            "files": files,
        }
    return result


def write_sources(path: Path, manifest: dict[str, Any]) -> None:
    groups = manifest["groups"]
    lines = [
        "# D2RR Item Database data sources",
        "",
        "The normalized files are generated offline from these exact upstream revisions.",
        "The game plugin performs no network requests.",
        "",
    ]
    labels = {
        "keyed/uniques.json": "Uniques",
        "keyed/sets.json": "Sets",
        "keyed/runewords.json": "Runewords",
        "keyed/armors.json": "Armor bases",
        "keyed/weapons.json": "Weapon bases",
        "keyed/ias-calculator.json": "Weapon-speed lookup",
        "strings/enUS.json": "English strings",
        "recipes/CubeRecipes.html": "Cube Recipes",
        "recipes/ItemEnchants.html": "Item Enchants",
        "recipes/Crafting.html": "Item Crafting",
        "Items/Orbs.html": "Orbs and corruption affixes",
    }
    for group in ("website", "wiki"):
        value = groups[group]
        revision = value["revision"]
        repo = value["repo"]
        lines.extend((
            f"## {group.title()}",
            "",
            f"- Repository: <https://github.com/{repo}>",
            f"- Revision: [`{revision}`](https://github.com/{repo}/tree/{revision})",
            "",
            "| Dataset/input | Pinned source | SHA-256 |",
            "| --- | --- | --- |",
        ))
        for key, info in value["files"].items():
            url = f"https://raw.githubusercontent.com/{repo}/{revision}/{info['repo_path']}"
            lines.append(f"| {labels.get(key, key)} | [`{info['repo_path']}`]({url}) | `{info['sha256']}` |")
        lines.append("")
    path.write_text("\n".join(lines), encoding="utf-8", newline="\n")


def prepare_baseline(out_dir: Path) -> dict[str, Any]:
    recreate(out_dir)
    for name in DATA_FILES:
        shutil.copy2(ROOT / "data" / name, out_dir / name)
    shutil.copy2(ROOT / "upstream.lock.json", out_dir / "upstream.lock.json")
    database = read_json(out_dir / "database.json")
    guides = read_json(out_dir / "guides.json")
    lock = read_json(out_dir / "upstream.lock.json")
    manifest = manifest_from_normalized(lock, database, guides)
    write_sources(out_dir / "SOURCES.md", manifest)
    write_checksums(out_dir, DATA_FILES + SUPPORT_FILES)
    errors = []
    for name, kind in (("database.json", "database"), ("guides.json", "guides")):
        errors.extend(toolkit.validate_normalized(out_dir / name, kind)["errors"])
    checksum_errors, _ = toolkit.verify_sha256s(out_dir)
    errors.extend(checksum_errors)
    if errors:
        raise RuntimeError("Current baseline is invalid:\n- " + "\n- ".join(errors))
    return manifest


def snapshot_at_revisions(baseline_dir: Path, out_dir: Path, revisions: dict[str, str | None]) -> dict[str, Any]:
    lock = toolkit.load_lock(baseline_dir)
    recreate(out_dir)
    manifest = {"tool_version": toolkit.TOOL_VERSION, "groups": {}}
    for group, cfg in toolkit.SOURCE_GROUPS.items():
        entry = lock[group]
        repo = str(entry.get("repo") or cfg["repo"])
        branch = str(entry.get("branch") or cfg["default_branch"])
        requested = revisions.get(group)
        revision = requested or toolkit.github_latest_sha(repo, branch)
        files = {}
        for key, repo_path in cfg["sources"].items():
            data = toolkit.github_raw(repo, revision, repo_path)
            destination = out_dir / group / key
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_bytes(data)
            files[key] = {
                "repo_path": repo_path,
                "sha256": toolkit.sha256_bytes(data),
                "size": len(data),
                "snapshot_path": str(Path(group) / key).replace("\\", "/"),
            }
        manifest["groups"][group] = {
            "repo": repo,
            "branch": branch,
            "revision": revision,
            "files": files,
        }
    write_json(out_dir / "snapshot_manifest.json", manifest)
    return manifest


def run_generators(snapshot_dir: Path, candidate_dir: Path, manifest: dict[str, Any]) -> None:
    recreate(candidate_dir)
    database_command = [
        sys.executable,
        str(ROOT / "tools" / "generate_database.py"),
        "--source", str(snapshot_dir / "website"),
        "--revision", manifest["groups"]["website"]["revision"],
        "--output", str(candidate_dir / "database.json"),
    ]
    guide_command = [
        sys.executable,
        str(ROOT / "tools" / "generate_guides.py"),
        "--source", str(snapshot_dir / "wiki"),
        "--revision", manifest["groups"]["wiki"]["revision"],
        "--output", str(candidate_dir / "guides.json"),
    ]
    subprocess.run(database_command, cwd=ROOT, check=True)
    subprocess.run(guide_command, cwd=ROOT, check=True)
    lock = read_json(ROOT / "upstream.lock.json")
    for group in ("website", "wiki"):
        lock[group]["sha"] = manifest["groups"][group]["revision"]
    write_json(candidate_dir / "upstream.lock.json", lock)
    write_sources(candidate_dir / "SOURCES.md", manifest)


def write_reports(baseline_dir: Path, old_snapshot: Path, new_snapshot: Path, candidate_dir: Path, report_dir: Path) -> dict[str, Any]:
    report_dir.mkdir(parents=True, exist_ok=True)
    source_report = toolkit.compare_snapshot_dirs(old_snapshot, new_snapshot)
    toolkit.write_json(report_dir / "source_changes.json", source_report)
    (report_dir / "source_changes.txt").write_text(toolkit.format_source_diff(source_report), encoding="utf-8", newline="\n")
    normalized = {}
    for name, label in (("database.json", "database"), ("guides.json", "guides")):
        report = toolkit.diff_normalized(baseline_dir / name, candidate_dir / name)
        normalized[label] = report
        toolkit.write_json(report_dir / f"{label}_changes.json", report)
        (report_dir / f"{label}_changes.txt").write_text(toolkit.format_normalized_diff(report), encoding="utf-8", newline="\n")
    return {"sources": source_report, **normalized}


def validation_results(candidate_dir: Path) -> list[dict[str, Any]]:
    return [
        toolkit.validate_normalized(candidate_dir / "database.json", "database"),
        toolkit.validate_normalized(candidate_dir / "guides.json", "guides"),
    ]


def command_update(args: argparse.Namespace) -> int:
    work_dir = Path(args.work_dir).resolve()
    if args.apply:
        source_work = Path(args.from_work or work_dir).resolve()
        staged = source_work / "staged"
        candidate = source_work / "candidate"
        if not staged.exists() or not candidate.exists():
            raise FileNotFoundError(f"Reviewed work directory is incomplete: {source_work}")
        checksum_errors, _ = toolkit.verify_sha256s(staged)
        if checksum_errors:
            raise RuntimeError("Staged checksums failed:\n- " + "\n- ".join(checksum_errors))
        validations = validation_results(staged)
        errors = [error for result in validations for error in result["errors"]]
        if errors:
            raise RuntimeError("Staged validation failed:\n- " + "\n- ".join(errors))
        for name in DATA_FILES:
            shutil.copy2(staged / name, ROOT / "data" / name)
        for name in GENERATED_DATABASE_FILES:
            if (candidate / name).exists():
                shutil.copy2(candidate / name, ROOT / "data" / name)
        shutil.copy2(staged / "upstream.lock.json", ROOT / "upstream.lock.json")
        shutil.copy2(staged / "SOURCES.md", ROOT / "SOURCES.md")
        print(f"Applied reviewed candidate from {source_work}")
        print("No build, commit, tag, push, or publication was performed.")
        return 0

    recreate(work_dir)
    baseline = work_dir / "baseline"
    snapshots = work_dir / "snapshots"
    pinned_snapshot = snapshots / "pinned"
    candidate_snapshot = snapshots / "candidate"
    candidate = work_dir / "candidate"
    reports = work_dir / "reports"
    staged = work_dir / "staged"
    baseline_manifest = prepare_baseline(baseline)
    pinned_revisions = {group: baseline_manifest["groups"][group]["revision"] for group in toolkit.SOURCE_GROUPS}
    snapshot_at_revisions(baseline, pinned_snapshot, pinned_revisions)
    requested = {"website": args.website_revision, "wiki": args.wiki_revision}
    candidate_manifest = snapshot_at_revisions(baseline, candidate_snapshot, requested)
    run_generators(candidate_snapshot, candidate, candidate_manifest)
    all_reports = write_reports(baseline, pinned_snapshot, candidate_snapshot, candidate, reports)
    validations = validation_results(candidate)
    errors = [error for result in validations for error in result["errors"]]
    write_json(reports / "validation.json", {"results": validations, "errors": errors})
    validation_lines = []
    for result in validations:
        validation_lines.append(f"{Path(result['path']).name}: {result.get('record_count', '?')} records")
        validation_lines.extend(f"  WARNING: {warning}" for warning in result["warnings"])
        validation_lines.extend(f"  ERROR: {error}" for error in result["errors"])
    (reports / "validation.txt").write_text("\n".join(validation_lines) + "\n", encoding="utf-8", newline="\n")
    if errors:
        raise RuntimeError("Generated candidate validation failed:\n- " + "\n- ".join(errors))
    stage_result = toolkit.stage_candidate(baseline, candidate, staged)
    for name in GENERATED_DATABASE_FILES:
        if (candidate / name).exists():
            shutil.copy2(candidate / name, staged / name)
    write_checksums(staged)
    changed_sources = [entry["path"] for entry in all_reports["sources"]["files"] if entry.get("changed")]
    summary = {
        "mode": "dry-run",
        "work_dir": str(work_dir),
        "source_revisions": {
            group: {
                "old": baseline_manifest["groups"][group]["revision"],
                "candidate": candidate_manifest["groups"][group]["revision"],
            }
            for group in toolkit.SOURCE_GROUPS
        },
        "changed_source_files": changed_sources,
        "database": {key: all_reports["database"][key] for key in ("added_count", "removed_count", "changed_count", "unchanged_count", "added_by_tab", "removed_by_tab", "changed_by_tab")},
        "guides": {key: all_reports["guides"][key] for key in ("added_count", "removed_count", "changed_count", "unchanged_count", "added_by_tab", "removed_by_tab", "changed_by_tab")},
        "validation_warnings": [warning for result in validations for warning in result["warnings"]],
        "stage": stage_result,
    }
    write_json(work_dir / "maintenance_summary.json", summary)
    lines = [
        "D2RR maintenance dry run",
        "========================",
        f"Changed source files: {len(changed_sources)}",
        *[f"  - {name}" for name in changed_sources],
        f"Database: +{summary['database']['added_count']} -{summary['database']['removed_count']} ~{summary['database']['changed_count']}",
        f"Guides:   +{summary['guides']['added_count']} -{summary['guides']['removed_count']} ~{summary['guides']['changed_count']}",
        f"Validation warnings: {len(summary['validation_warnings'])}",
        "",
        f"Review reports in: {reports}",
        f"Apply only after review: {sys.executable} tools/maintain_data.py update --apply --from-work \"{work_dir}\"",
    ]
    (work_dir / "maintenance_summary.txt").write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")
    print("\n".join(lines))
    return 0


def replace_version(path: Path, patterns: list[tuple[str, str]], version: str) -> None:
    text = path.read_text(encoding="utf-8")
    original = text
    for pattern, replacement in patterns:
        text, count = re.subn(pattern, replacement.format(version=version), text)
        if count == 0:
            raise RuntimeError(f"Could not find expected version field in {path}: {pattern}")
    if text != original:
        path.write_text(text, encoding="utf-8", newline="\n")


def change_summary(report_dir: Path) -> tuple[str, dict[str, Any], dict[str, Any]]:
    database = read_json(report_dir / "database_changes.json") if (report_dir / "database_changes.json").exists() else {}
    guides = read_json(report_dir / "guides_changes.json") if (report_dir / "guides_changes.json").exists() else {}
    line = (
        f"Database records: +{database.get('added_count', 0)} -{database.get('removed_count', 0)} ~{database.get('changed_count', 0)}; "
        f"guide records: +{guides.get('added_count', 0)} -{guides.get('removed_count', 0)} ~{guides.get('changed_count', 0)}."
    )
    return line, database, guides


def command_prepare_release(args: argparse.Namespace) -> int:
    version = args.version
    if not re.fullmatch(r"\d+\.\d+\.\d+", version):
        raise ValueError("--version must be MAJOR.MINOR.PATCH")
    work_dir = Path(args.work_dir).resolve()
    validations = validation_results(ROOT / "data")
    errors = [error for result in validations for error in result["errors"]]
    if errors:
        raise RuntimeError("Project data validation failed:\n- " + "\n- ".join(errors))
    replace_version(ROOT / "CMakeLists.txt", [(r"project\(D2RRItemDatabase VERSION \d+\.\d+\.\d+", "project(D2RRItemDatabase VERSION {version}")], version)
    replace_version(ROOT / "src" / "plugin.cpp", [(r'\.version = "\d+\.\d+\.\d+"', '.version = "{version}"')], version)
    replace_version(ROOT / "tools" / "package_release.ps1", [(r"\[string\]\$Version = '\d+\.\d+\.\d+'", "[string]$Version = '{version}'")], version)
    replace_version(ROOT / ".github" / "workflows" / "build.yml", [
        (r"-Version \d+\.\d+\.\d+", "-Version {version}"),
        (r"name: d2rr-item-database-\d+\.\d+\.\d+", "name: d2rr-item-database-{version}"),
    ], version)
    replace_version(ROOT / "README.md", [(r"-Version \d+\.\d+\.\d+", "-Version {version}")], version)
    replace_version(ROOT / "INSTALL.md", [
        (r"D2RR-Item-Database-Reimagined-Hub-v\d+\.\d+\.\d+\.zip", "D2RR-Item-Database-Reimagined-Hub-v{version}.zip"),
        (r"D2RR-Item-Database-v\d+\.\d+\.\d+\.zip", "D2RR-Item-Database-v{version}.zip"),
    ], version)
    report_dir = work_dir / "reports"
    summary_line, database, guides = change_summary(report_dir)
    notes = ROOT / f"RELEASE_NOTES_v{version}.md"
    notes.write_text(
        f"# D2RR Item Database v{version}\n\n## Data update\n\n- {summary_line}\n"
        "- Generated from pinned D2R Reimagined website and wiki revisions in `upstream.lock.json`.\n"
        "- Validated and packaged by the offline maintenance workflow.\n\n"
        "## Installation\n\nInstall through D2RLoader when approved, or use the manual release ZIP. "
        "Press **Alt+S** in game to open or close the overlay.\n",
        encoding="utf-8", newline="\n",
    )
    command = [
        "pwsh", "-NoProfile", "-File", str(ROOT / "tools" / "package_release.ps1"),
        "-Version", version, "-ModVersion", args.mod_version,
    ]
    subprocess.run(command, cwd=ROOT, check=True)
    data_stage = ROOT / "dist" / f"D2RR-Item-Database-Data-v{version}"
    recreate(data_stage)
    for source, name in (
        (ROOT / "data" / "database.json", "database.json"),
        (ROOT / "data" / "guides.json", "guides.json"),
        (ROOT / "SOURCES.md", "SOURCES.md"),
        (ROOT / "upstream.lock.json", "upstream.lock.json"),
    ):
        shutil.copy2(source, data_stage / name)
    write_checksums(data_stage)
    data_zip = ROOT / "dist" / f"D2RR-Item-Database-Data-v{version}.zip"
    if data_zip.exists():
        data_zip.unlink()
    shutil.make_archive(str(data_zip.with_suffix("")), "zip", data_stage)
    shutil.copy2(notes, ROOT / "dist" / notes.name)
    artifacts = [
        ROOT / "dist" / "d2rl-item-database.dll",
        ROOT / "dist" / f"D2RR-Item-Database-v{version}.zip",
        ROOT / "dist" / f"D2RR-Item-Database-Reimagined-Hub-v{version}.zip",
        data_zip,
    ]
    checksum_lines = [f"{toolkit.sha256_file(path)}  {path.name}" for path in artifacts]
    (ROOT / "dist" / "SHA256SUMS.txt").write_text("\n".join(checksum_lines) + "\n", encoding="ascii", newline="\n")
    print(f"Prepared v{version} locally in {ROOT / 'dist'}")
    print(f"Release notes: {notes}")
    print("No commit, tag, push, upload, or publication was performed.")
    return 0


def make_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Maintain normalized D2RR data without changing plugin runtime behavior.")
    subparsers = parser.add_subparsers(dest="command", required=True)
    update = subparsers.add_parser("update", help="Run a reviewable data update or apply a reviewed candidate")
    mode = update.add_mutually_exclusive_group(required=True)
    mode.add_argument("--dry-run", action="store_true", help="Download, regenerate, diff, validate, and stage without changing tracked project data")
    mode.add_argument("--apply", action="store_true", help="Apply a previously reviewed work directory")
    update.add_argument("--work-dir", default=str(ROOT / ".maintenance" / "current"))
    update.add_argument("--from-work", help="Reviewed dry-run directory to apply")
    update.add_argument("--website-revision", help="Use a specific website commit instead of the current branch head")
    update.add_argument("--wiki-revision", help="Use a specific wiki commit instead of the current branch head")
    update.set_defaults(func=command_update)
    release = subparsers.add_parser("prepare-release", help="Version, build, package, checksum, and write release notes locally")
    release.add_argument("--version", required=True)
    release.add_argument("--mod-version", default="3.0.12")
    release.add_argument("--work-dir", default=str(ROOT / ".maintenance" / "current"))
    release.set_defaults(func=command_prepare_release)
    return parser


def main(argv: list[str] | None = None) -> int:
    args = make_parser().parse_args(argv)
    try:
        return int(args.func(args))
    except KeyboardInterrupt:
        print("Interrupted.", file=sys.stderr)
        return 130
    except Exception as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
