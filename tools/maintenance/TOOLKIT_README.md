# D2RR Item Database Maintenance Toolkit

This is a **standard-library-only** maintenance toolkit built for the v0.5.3 data package. It is designed to make future D2R Reimagined balance/content updates cheap and reviewable.

## What it automates

It can:

- verify the current data package and its SHA-256 checksums;
- check whether any of the 11 tracked upstream source files changed;
- download reproducible snapshots of the pinned or latest website/wiki data;
- identify which uniques, sets, set items, runewords, bases, strings, or guide sections changed upstream;
- compare an old and new normalized `database.json` / `guides.json` by stable record ID;
- generate human-readable and JSON change reports;
- validate candidate normalized data before release;
- stage a release-ready data folder and regenerate `SHA256SUMS.txt`.

## What it deliberately does not do yet

It **does not regenerate the normalized plugin data from the raw website/wiki sources**. The exact v0.5.3 normalization/rendering code was not included in the supplied data-source package, and re-creating it by inference would risk subtly changing item text or derived stats.

That means this toolkit removes most of the repetitive maintenance work, while leaving one bounded step for the existing generator/Codex:

> raw changed sources -> new normalized `database.json` / `guides.json`

Once the generator source is available, it can be plugged into this workflow without redesigning anything here.

## Recommended future update workflow

Assume your current v0.5.3 data package is in `baseline/`.

### 1. Verify the baseline

```bash
python d2rr_maintain.py verify-baseline baseline
```

### 2. Check whether D2RR actually changed any tracked data

```bash
python d2rr_maintain.py check-upstream baseline --report-json reports/upstream-check.json
```

A repository may have new commits while none of the tracked item files changed. The tool checks the **actual source hashes**, not just the repository revision.

### 3. Take pinned and latest source snapshots

```bash
python d2rr_maintain.py snapshot baseline snapshots/old --pinned
python d2rr_maintain.py snapshot baseline snapshots/new --latest
```

### 4. Produce a source-level change report

```bash
python d2rr_maintain.py diff-sources snapshots/old snapshots/new --report-dir reports
```

This tells you things such as:

```text
~ website/keyed/uniques.json
    entries: +7 -0 ~14
      + New Unique Name
      ~ Existing Unique Name
          Lines.ModStr...: ... -> ...
```

For `sets.json`, individual set items are flattened so a change to one set item does not appear merely as “the entire set changed.”

### 5. Regenerate only the affected normalized dataset(s)

This is the remaining generator/Codex step. The source report tells it exactly what changed and which dataset needs regeneration.

Put the regenerated outputs in a `candidate/` directory:

```text
candidate/
    database.json
    guides.json
    upstream.lock.json   # optional replacement
    SOURCES.md           # optional replacement
```

### 6. Diff the normalized output

```bash
python d2rr_maintain.py diff-data baseline/database.json candidate/database.json --report-dir reports --label database
python d2rr_maintain.py diff-data baseline/guides.json candidate/guides.json --report-dir reports --label guides
```

This report is record-aware and shows:

- new records;
- removed records;
- changed records;
- likely renames;
- exact field/property changes;
- per-tab totals.

### 7. Validate and stage the release data

```bash
python d2rr_maintain.py stage --baseline baseline --candidate candidate --out staged-vNEXT
```

`stage` refuses a candidate with structural errors. It creates fresh checksums and includes change reports in the staged directory.

## Validation checks

The validator currently checks:

- schema version;
- duplicate/missing IDs;
- expected tab names;
- required record fields;
- property object shape;
- guide-table shape;
- source references;
- provenance/source hash format;
- category counts when provided;
- searchable/display-name consistency warnings;
- package SHA-256 checksums.

## Files tracked from v0.5.3

### Website data

- `static/data/keyed/uniques.json`
- `static/data/keyed/sets.json`
- `static/data/keyed/runewords.json`
- `static/data/keyed/armors.json`
- `static/data/keyed/weapons.json`
- `static/data/keyed/ias-calculator.json`
- `static/data/strings/enUS.json`

### Wiki guide data

- `recipes/CubeRecipes.html`
- `recipes/ItemEnchants.html`
- `recipes/Crafting.html`
- `Items/Orbs.html`

## Safety philosophy

The tool never modifies your baseline in place. `stage` writes to a separate output directory. That gives you a reviewable checkpoint before the plugin DLL is rebuilt or a release is published.
