# Installation

## Requirements

- Windows x64
- Diablo II: Resurrected
- D2R Reimagined 3.0.12
- D2RLoader with Plugin ABI 4 support

Close the game and D2RLoader before installing or updating the plugin.

## Reimagined Hub listing

The project is listed on the Reimagined Hub with permission from the Reimagined
owner. The launcher's standard Hub package format currently installs assets
inside `Reimagined.mpq`, while D2RLoader native extensions must be installed in
the sibling `mods\Reimagined\d2rloader` directory. Until the launcher supports
that native-extension destination, use the manual GitHub Release package below.

For supported ladders, the Reimagined team can also distribute the DLL through
the launcher's approved optional-extension system.

## Manual GitHub Release install

1. Download `D2RR-Item-Database-v0.5.1.zip` and `SHA256SUMS.txt` from the
   GitHub Release.
2. Verify the ZIP's SHA-256 hash against `SHA256SUMS.txt`.
3. Extract the ZIP into:
   `<Diablo II Resurrected>\mods\Reimagined\`
4. Confirm these files exist:
   - `d2rloader\plugins\d2rl-item-database.dll`
   - `d2rloader\plugins\item-database\database.json`
   - `d2rloader\plugins\item-database\guides.json`
5. Start D2R Reimagined through D2RLoader.
6. Press **Alt+S** to open the overlay. Press **Alt+S** again to close it.

The overlay can also be toggled with the `itemdb` D2RLoader console command.

## Update

Close the game and install the new package over the old files. Keep the DLL and
both JSON files from the same release together.

## Uninstall

Close the game, then delete:

- `mods\Reimagined\d2rloader\plugins\d2rl-item-database.dll`
- `mods\Reimagined\d2rloader\plugins\item-database\`

The plugin creates no save data and has no separate configuration to remove.
