# Dynamic overlay milestone

This milestone replaces the native D2R panel prototype with a reusable Win32
overlay hosted by the D2RLoader client plugin. Native panels retain their widget
trees for the whole game session and the public Widget service cannot update
text, which made catalog paging exhaust D2R's UI allocation. The overlay keeps
one fixed set of controls and draws the current records dynamically.

## Integration design

At plugin load, `src/plugin.cpp` reads `item-database/database.json`, constructs
`PrototypeViewModel`, and starts `OverlayHost` on its own Windows UI thread. The
overlay:

1. locates and follows the D2R client window;
2. stays hidden until the D2RLoader `Open Item Database` action (Alt+S) is pressed;
3. activates while open so its text search and dropdown controls receive normal
   keyboard and mouse input, then restores focus to D2R when closed;
4. draws four tabs, site-matched per-tab filters, eight reusable result rows,
   Previous/Next controls, the current page number, selected details, and a
   bounded draggable detail scrollbar;
5. renders complete Set groups and all members of each Base family;
6. hides when D2R is not the foreground process and shuts down its UI thread
   before the plugin unloads.

Paging is model-driven and does not create another window or native D2R widget.
Each tab retains its own filters. Changing a filter resets that tab to page one
and selects its first result. Set-member matches expand back to the complete Set,
and Base matches expand back to the complete Base family.

## Build

Run from the repository root in a Visual Studio developer PowerShell:

```powershell
python tools/bootstrap.py
.\tools\build.ps1 -Configuration Debug
.\tools\build.ps1 -Configuration Release
```

The build runs the backend suite and the real-database prototype suite. The
latter checks first/last-page boundaries, invalid navigation, page retention
per tab, selection reset after paging, grouped Sets, and Base-family ordering.

## Manual overlay test

Copy only these files beneath the Reimagined mod directory:

```text
mods/Reimagined/d2rloader/plugins/
├── d2rl-item-database.dll
└── item-database/
    └── database.json
```

1. Start D2RLoader with Reimagined and confirm `D2RR Item Database` appears in
   Extensions as a client plugin.
2. Enter a game and press Alt+S. Confirm the dark overlay appears over the D2R
   client without minimizing or pausing the game.
3. Switch through Uniques, Sets, Runewords, and Bases.
4. Type in the search box and exercise each tab's dropdowns, checkboxes, and
   Reset Filters button. Confirm results and paging update immediately.
   Set details use the website palette: green set lines, blue magical
   properties, gray base/stat lines, red requirements, and cyan rarity lines.
5. Search Sets for `Afterlife`; confirm the result is Hades' Underworld and its
   complete member list remains visible.
6. Use Previous and Next repeatedly on every tab. Verify the page number and
   eight result rows update without overlapping or corrupting.
7. On Sets, test both the mouse wheel and dragging the scrollbar thumb; confirm
   scrolling stops at both ends.
8. On Bases, verify the Normal, Exceptional, and Elite members appear as
   separate columns when the family has all three.
9. Close with X or Alt+S, reopen it, and repeat a page change.
10. Check `d2rloader/logs/item-database.log` for overlay load errors.

## Current verification boundary

The database load, filter behavior, paging state, grouped data, Release DLL, ABI
exports, and overlay compilation are tested. Placement, native control styling,
focus restoration, font availability, scaling, and interaction with D2R's cursor
must be verified in the game.
