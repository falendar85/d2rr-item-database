# Installation

## Requirements

- Windows x64
- Diablo II: Resurrected
- D2R Reimagined 3.0.12
- D2RLoader with Plugin ABI 4 support

Close the game and D2RLoader before installing or updating the plugin.

## D2RLoader Extension installer

The standard D2RLoader installer artifact is `d2rl-item-database.dll`. The DLL
contains its complete offline catalog and guide data, so the installer does not
need to place companion files or alter the Reimagined launcher. Once the release
is approved and published in D2RLoader Extension, install it from that catalog.

Reimagined ladder approval is a separate step after the D2RLoader installation
has been verified.

## Manual GitHub Release install

1. Download `D2RR-Item-Database-v0.5.2.zip` and `SHA256SUMS.txt` from the
   GitHub Release.
2. Verify the ZIP's SHA-256 hash against `SHA256SUMS.txt`.
3. Extract the ZIP into:
   `<Diablo II Resurrected>\mods\Reimagined\`
4. Confirm `d2rloader\plugins\d2rl-item-database.dll` exists.
5. Start D2R Reimagined through D2RLoader.
6. Press **Alt+S** to open the overlay. Press **Alt+S** again to close it.

The overlay can also be toggled with the `itemdb` D2RLoader console command.

## Update

Close the game and install the new package over the old DLL. Version 0.5.2 and
later contain the catalog inside the DLL.

## Uninstall

Close the game, then delete:

- `mods\Reimagined\d2rloader\plugins\d2rl-item-database.dll`

The legacy `mods\Reimagined\d2rloader\plugins\item-database\` folder from an
older release can also be removed; version 0.5.2 does not use it.

The plugin creates no save data and has no separate configuration to remove.
