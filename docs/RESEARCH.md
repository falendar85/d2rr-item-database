# Research and implementation decisions

Inspected September 20/21, 2026. Exact revisions are in `upstream.lock.json`.

* [Mod](https://github.com/D2R-Reimagined/d2r-reimagined-mod): default branch is **next**,
  not a release. `data/global/ui/layouts/pauselayouthd.json` has a Mod Info row sending
  `PanelManager:OpenPanel:modinfo_aa`. `modinfo_aahd.json` derives from SettingsPanel;
  TabBarWidget sends SettingsPanelMessage:CheckChanges to seven separate layouts.
  `modinfo_generalinformationhd.json` uses ScrollViewWidget + ScrollControllerWidget.
  Excel uniqueitems/setitems/sets/runes/armor/weapons/itemtypes/properties tables are
  the game source. The site's exported data avoids reimplementing D2 stat math.
* [Website](https://github.com/D2R-Reimagined/d2r-reimagined-website):
  `static/data/keyed/{uniques,sets,runewords,armors,weapons}.json` plus
  `static/data/strings/enUS.json` already expose numeric keyed lines and roll ranges.
  `ias-calculator.json` exposes weapon speed and inherited item-type codes.
  `src/lib/catalog.ts`, `catalog-controls.ts`, `i18n.ts`, `types.ts`,
  `src/utilities/format-template.ts` define filtering, localized display and keyed line semantics.
  Live `/data/uniques`, `/data/sets`, `/data/runewords`, `/data/bases` were inspected.
* [LAN fork](https://github.com/D2R-Reimagined/d2r-reimagined-LAN): separate compatibility
  build under Reimagined/Reimagined.mpq. Do not silently mix its tables with website data.
* [Official loader](https://d2rloader.net/docs.html), [changelog](https://d2rloader.net/changelog.html):
  distinct from sh4nks' similarly named multibox launcher. No public core source was
  located in the D2RLoader GitHub organization; the public contract is PluginSDK.
* [SDK](https://github.com/D2RLoader/PluginSDK): `api.h`, `context.h`, `lifecycle.h`,
  `resource.h` and examples/ui-panel, input-action, widget-localization, shared-events.
  PluginInfo Client flag, manifest RCDATA, Load/GetPluginInfo/Unload exports.
  `panels.h`: namespaced Panel registration during load, CloseOnEscape; active calls
  run on the UI thread. StockPanel child attachment currently supports PlayerInventory
  only, not PausePanel. Register/unregister is explicitly NOT layout hot reload.
  `widgets.h`: lookup, rect, visible/enabled, broadcast message; no text setter/getter.
  `input.h`: bindable actions in normal gameplay, suppressed during text editing;
  no raw character/mouse stream. `shared_events.h`: scoped UI target/command messages.
  `threads.h`: queue UI work, pending work discarded on unload.
  `data_tables.h`: read-only compiled table views, game-thread-only, revision-scoped
  raw row layouts. Not needed by this offline, source-versioned catalog.

## Supported design and explicit compromise

Use a native, embedded JSON launch panel with four tab buttons. The actual interactive
database uses a Win32 owned popup renderer with custom dark/gold painting, normal text
entry, keyboard and mouse controls, and a fixed number of result rows. This avoids
inventing SetText messages, private widget pointers, executable patches, or input hooks.
It is not a fully native D2R widget implementation. Compositing, focus and positioning
over D2R are unverified until tested in windowed/borderless D2R. No exclusive-fullscreen
guarantee. No native edit/list virtualization API was found in the public SDK.

Pause-menu integration is an optional OFFLINE layout merge into a staging copy;
it does not replace Mod Information. Default opening uses the bindable shortcut or
console command, which avoids overwriting a mod's pause layout.
