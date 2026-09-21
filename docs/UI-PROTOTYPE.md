# Minimal in-game UI prototype

This milestone proves the connection between the normalized database and the
public D2RLoader UI services. It does not implement search or the complete
Sets, Runewords, and Bases views.

## Integration design

At plugin load, `src/plugin.cpp` reads `item-database/database.json` beside the
plugin, constructs `PrototypeViewModel`, and asks `buildPrototypeLayout` to
generate a D2R UI JSON resource. The plugin then:

1. registers `data/global/ui/layouts/item-database/ItemDatabasehd.json` through
   `ResourceService`;
2. registers the namespaced `item-database/ItemDatabase` panel through
   `PanelService`;
3. listens for its tab, result-selection, and close button messages through
   `SharedEventService`;
4. switches the bounded panes and prebuilt detail widgets through
   `WidgetService::setWidgetVisible`;
5. registers `Open Item Database` as a bindable input action, initially F8,
   and the `itemdb` developer-console command as a fallback.

This works within the public SDK's supported dynamic operations. The SDK does
not expose a general text setter, so the first eight alphabetically sorted
Unique rows and their details are materialized into the registered layout when
the database loads. Selection only changes visibility. That is enough to test
the integration without adding an unsupported widget hook.

## Build

Run from the repository root in PowerShell:

```powershell
python tools/bootstrap.py
.\tools\build.ps1 -Configuration Debug
.\tools\build.ps1 -Configuration Release
```

`bootstrap.py` downloads the exact D2RLoader SDK revision and JSON header pinned
in `upstream.lock.json` into ignored `.deps`. The builds run both the existing
backend test suite and the real-database prototype smoke test.

## Manual first in-game test

Nothing in this repository installs itself. To test a Release build, manually
copy only these files to either the global plugin root or the Reimagined
mod-scoped plugin root supported by your D2RLoader setup:

```text
d2rloader/plugins/
├── d2rl-item-database.dll       <- build-Release/d2rl-item-database.dll
└── item-database/
    └── database.json            <- data/database.json
```

For a mod-scoped test, the same `d2rloader/plugins` subtree belongs under
`<game>/mods/<Reimagined mod>/`. Do not put the database in the MPQ.

1. Start D2RLoader with Reimagined and confirm `D2RR Item Database` appears in
   the Extensions list as a client plugin.
2. Enter a game. Press F8, or enable the developer console and run `itemdb`.
3. Confirm the panel opens and shows exactly Uniques, Sets, Runewords, and Bases.
4. Confirm eight Unique names appear and the initially selected item has real
   base, type, tier, level, origin, applicable damage/defense/socket fields, and
   property lines.
5. Select several Unique rows and verify the right-side details change.
6. Switch through Sets, Runewords, and Bases. Each should show the milestone
   placeholder, then switch back to Uniques without closing or crashing.
7. Close with the panel's Close button, Escape, F8, or `itemdb`.
8. Review the plugin log for load, database counts, panel registration/open,
   tab changes, selection, detail updates, close-button/toggle close, and any
   UI lookup failure.

## Current verification boundary

The DLL, ABI exports, manifest resource section, normalized-database load,
bounded list, selection, detail projection, all tab states, and generated JSON
shape are tested on Windows. Actual D2R widget construction, sprite sizing,
message delivery, visibility behavior, controller focus, and ultrawide layout
remain unverified until the manual game test above. Escape closure is owned by
D2RLoader's `CloseOnEscape` behavior and therefore does not emit this plugin's
explicit close log line.
