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
2. stays hidden until the D2RLoader `Open Item Database` action (F8) is pressed;
3. uses a no-activate topmost window so mouse interaction does not take focus
   away from D2R;
4. draws four tabs, eight reusable result rows, Previous/Next controls, the
   current page number, selected details, and a bounded detail scrollbar;
5. renders complete Set groups and all members of each Base family;
6. hides when D2R is not the foreground process and shuts down its UI thread
   before the plugin unloads.

Paging is model-driven and does not create another window or native D2R widget.
Each tab remembers its page. Moving to another page selects the first result on
that page. Search is the next overlay milestone and will filter this same result
model.

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
2. Enter a game and press F8. Confirm the dark overlay appears over the D2R
   client without minimizing or pausing the game.
3. Switch through Uniques, Sets, Runewords, and Bases.
4. Select several rows and confirm the right-side content changes.
5. Use Previous and Next repeatedly on every tab. Verify the page number and
   eight result rows update without overlapping or corrupting.
6. On Sets, use the mouse wheel over the detail area and confirm scrolling stops
   at both ends.
7. On Bases, verify the Normal, Exceptional, and Elite members appear as
   separate columns when the family has all three.
8. Close with X or F8, reopen it, and repeat a page change.
9. Check `d2rloader/logs/item-database.log` for overlay load errors.

## Current verification boundary

The database load, paging state, grouped data, Release DLL, ABI exports, and
overlay compilation are tested. Placement, foreground tracking, click delivery,
font availability, scaling, and interaction with D2R's cursor must be verified
in the game. Search input is intentionally deferred until this reusable overlay
shell passes that test.
