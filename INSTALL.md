# Installation

## Requirements

- Windows x64
- Diablo II: Resurrected
- D2R Reimagined 3.0.12
- D2RLoader with Plugin ABI 4 support

Close the game and D2RLoader before installing or updating the plugin.

## Reimagined Hub

Install `D2RR-Item-Database-Hub-v0.5.0.zip` from the plugin's Reimagined Hub
post. The launcher reads `plugininfo.json` and places the native plugin and its
offline data in the mod-scoped plugin directory. Do not manually extract the Hub
archive.

The Hub post must include these metadata lines so the launcher can index it:

```text
PlugVer: 0.5.0
Desc: Offline searchable item, recipe, crafting, enchant, and orb reference overlay.
ModVer: 3.0.12
```

Attach the Hub ZIP to the post.

## Manual GitHub Release install

1. Download `D2RR-Item-Database-v0.5.0.zip` and `SHA256SUMS.txt` from the
   GitHub Release.
2. Verify the ZIP's SHA-256 hash against `SHA256SUMS.txt`.
3. Extract the ZIP into:
   `<Diablo II Resurrected>\mods\Reimagined\`
4. Confirm these files exist:
   - `d2rloader\plugins\d2rl-item-database.dll`
   - `d2rloader\plugins\item-database\database.json`
   - `d2rloader\plugins\item-database\guides.json`
5. Start D2R Reimagined through D2RLoader and press **Alt+S**.

The overlay can also be toggled with the `itemdb` D2RLoader console command.

## Update

Close the game and install the new package over the old files. Keep the DLL and
both JSON files from the same release together.

## Uninstall

Close the game, then delete:

- `mods\Reimagined\d2rloader\plugins\d2rl-item-database.dll`
- `mods\Reimagined\d2rloader\plugins\item-database\`

The plugin creates no save data and has no separate configuration to remove.
