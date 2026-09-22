# D2RR Item Database

D2RR Item Database is an offline, read-only in-game reference overlay for
Diablo II: Resurrected – Reimagined. Press **Alt+S** to browse and search:

- uniques, sets, runewords, and base-item families;
- cube recipes, item enchants, and item crafting; and
- Power Orbs and corruption affixes.

The overlay provides the website-style filters, eight-result paging, item and
recipe color cues, grouped set bonuses, three-tier base comparisons, and
scrollable details. It does not alter items, saves, drop tables, or gameplay.
It performs no network requests and sends no telemetry.

## Requirements

- Windows x64
- Diablo II: Resurrected with D2R Reimagined 3.0.12
- D2RLoader with Plugin ABI 4 support

See [`INSTALL.md`](INSTALL.md) for installation. After installation, launch D2R
Reimagined through D2RLoader and press **Alt+S** to open or close the overlay.

## Build

From a PowerShell prompt with Visual Studio 2022 C++ tools installed:

```powershell
python tools/bootstrap.py
.\tools\build.ps1 -Configuration Release -Stage
python -m unittest discover -s tests -p "test_*.py"
.\tools\package_release.ps1 -Version 0.5.3 -ModVersion 3.0.12
```

Pinned upstream revisions and dependency hashes are in `upstream.lock.json`.
The architecture is: pinned source JSON/HTML -> Python normalizer -> versioned
offline JSON -> C++ query/state library -> D2RLoader client plugin -> Win32
overlay. More detail is in [`docs/DATA.md`](docs/DATA.md),
[`docs/RESEARCH.md`](docs/RESEARCH.md), and
[`docs/UI-PROTOTYPE.md`](docs/UI-PROTOTYPE.md).

## Data maintenance

Future D2R Reimagined data updates use a review-first workflow:

```powershell
python tools/maintain_data.py update --dry-run
python tools/maintain_data.py update --apply --from-work .maintenance/current
python tools/maintain_data.py prepare-release --version X.Y.Z --mod-version 3.0.12
```

The dry run downloads and pins source revisions, regenerates both normalized
databases with the original generators, reports source and record changes, and
validates a staged candidate without changing tracked project files. See
[`docs/MAINTENANCE.md`](docs/MAINTENANCE.md) for the review and release process.

## Licensing and attribution

Project source code is released under the MIT License. Generated catalog and
guide data are distributed for this plugin with permission from the D2R
Reimagined owner. See [`LICENSE`](LICENSE),
[`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md),
[`CREDITS.md`](CREDITS.md), and [`docs/PERMISSIONS.md`](docs/PERMISSIONS.md).

This independent community addon is not affiliated with or endorsed by
Blizzard Entertainment, D2R Reimagined, or D2RLoader.
