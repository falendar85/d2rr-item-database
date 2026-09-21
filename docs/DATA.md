# Database generation and normalization

The checked-in `data/database.json` is a reproducible, offline snapshot of the
website's already-exported keyed catalogs. Its source commit and every input file
hash are stored in `upstream.lock.json` and in the generated database provenance.
The game runtime never fetches or scrapes the website.

## Regenerate

Python 3.11 or newer is sufficient; the scripts use only the standard library.

From a pinned website checkout:

```powershell
python tools/generate_database.py --source C:\path\to\d2r-reimagined-website
python tools/validate_database.py
python -m unittest discover -s tests -p test_*.py -v
```

With no `--source`, the generator downloads only the seven files named in the
script from the exact website commit in `upstream.lock.json`, caches them under
`.deps/website-data/<commit>/`, records SHA-256 hashes, and then works locally.
Pass `--revision <commit>` to intentionally build another revision. Review and
update `upstream.lock.json` separately before committing that database.

## Normalized record

Each record has a stable ID based on upstream semantic keys rather than row order,
one of the four tab names, display/search text, typed categorical fields, typed
numbers, structured properties, and an exact source file/row reference. The full
contract is in `data/schema.json`.

Property records preserve:

* the source property key and raw arguments;
* canonical IDs such as `ias`, `fcr`, `deadly_strike`, `aura`, and
  `enhanced_weapon_damage`;
* minimum and maximum values only when the source line is unambiguous;
* scope (`weapon`, `armor`, or `shield`), per-level behavior, and conditional
  status for random groups, automagic, partial-set, and full-set bonuses;
* the rendered English line used for details and free-text search.

Numeric values are deliberately omitted for multi-number concepts such as a
chance-to-cast line. Treating its chance, skill level, and skill ID as one range
would create false property-filter matches.

## Catalog-specific behavior

* Uniques: enabled rows only; base, tier, origin, class, requirements, damage,
  defense, weapon speed, sockets, and properties are normalized.
* Sets: one record per set item. Item, partial-set, and full-set bonuses are
  present but flagged conditional, and the complete member list is retained.
* Runewords: enabled rows only. Rune names are canonicalized (`Jah`, not the
  display label `Jah Rune (#31)`), sequence and duplicate counts are preserved,
  and compatible bases are expanded from item-family and class-family data.
* Bases: armor and weapon rows are combined, while source references retain the
  exact originating file and row. Normal/Exceptional/Elite, requirements,
  damage, defense, speed, item-level socket caps, and conditional automagic are
  represented.

`generation-report.json` records source/output counts, total properties, numeric
properties, conditional properties, output size, warnings, and the final
database SHA-256. `property-catalog.json` lists every canonical/source property
ID so future filters can be added without changing the database format.

## Known source warning

The pinned English strings export lacks a translation for one source key,
`res-curse-len`, used by `Stone Hide`. The generator preserves it as
`res-curse-len [20]` and records `unresolved_string_keys: 1`. It is not silently
dropped or guessed.
