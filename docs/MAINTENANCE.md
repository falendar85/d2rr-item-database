# Data maintenance workflow

The maintenance workflow preserves the plugin schema and runtime behavior. It
uses the original normalizers in `tools/generate_database.py` and
`tools/generate_guides.py`, with source inspection, diffing, validation, and
staging supplied by `tools/maintenance/d2rr_maintain.py`.

Python 3.11 or newer and network access to GitHub are required for update
checks. The workflow uses only the Python standard library.

## Dry run

Run this first. It does not modify tracked project data:

```powershell
python tools/maintain_data.py update --dry-run
```

It writes a review workspace to `.maintenance/current/` containing:

- pinned and candidate source snapshots;
- source-level text and JSON reports;
- regenerated candidate `database.json` and `guides.json`;
- normalized record-level text and JSON reports;
- validation results, a staged data package, and checksums; and
- `maintenance_summary.txt` and `maintenance_summary.json`.

Use exact revisions when testing or intentionally pinning a specific update:

```powershell
python tools/maintain_data.py update --dry-run `
  --website-revision <website-commit> `
  --wiki-revision <wiki-commit>
```

Review at least these files before applying:

```text
.maintenance/current/maintenance_summary.txt
.maintenance/current/reports/source_changes.txt
.maintenance/current/reports/database_changes.txt
.maintenance/current/reports/guides_changes.txt
```

## Apply reviewed data

Apply exactly the candidate already reviewed in the dry run:

```powershell
python tools/maintain_data.py update --apply `
  --from-work .maintenance/current
```

Apply updates only normalized data, generated data reports, `SOURCES.md`, and
`upstream.lock.json`. It does not build, commit, tag, push, upload, or publish.

## Prepare a local release

After applying and reviewing the tracked changes:

```powershell
python tools/maintain_data.py prepare-release `
  --version X.Y.Z `
  --mod-version 3.0.12
```

This command updates the existing version metadata, writes release notes, runs
the existing Release build and tests, verifies the embedded data, and creates:

- the self-contained D2RLoader DLL;
- the manual D2RLoader release ZIP;
- the Reimagined Hub wrapper package;
- a normalized-data/source package;
- `SOURCES.md`, `upstream.lock.json`, release notes, and SHA-256 sums.

All artifacts remain local under `dist/`. The command never commits, tags,
pushes, uploads, or modifies a public listing.

## Reproduce v0.5.3

The recovered generators reproduce the v0.5.3 baseline byte-for-byte from:

- website `c7db0b49f6f75e1c15e808533572e4ac69be2db8`;
- wiki `4293c6eba8dccb0968003ebda3d2efe30d3221d4`.

Expected hashes:

```text
527323eaa9a8d89af6e7798ba9d7893bf3071f0cbc7034f41d6134f389949b14  database.json
f1931cae646ade287a26d5c89910da132c2153d290aeed84dbf033a7716d7f4a  guides.json
```
