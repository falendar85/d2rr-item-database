# D2RR Item Database

Work-in-progress, standalone client addon for D2RLoader SDK 0.2.0 / ABI 4.
The project never installs itself into a game directory. Current source references are
pinned in `upstream.lock.json`. The generated catalog is offline and read-only.

Architecture: pinned website keyed JSON -> Python normalizer -> versioned JSON ->
independent C++ query/state library -> D2RLoader client plugin -> native launch panel
an in-game renderer. See `docs/RESEARCH.md` for API constraints and `docs/DATA.md`
for the completed normalization pipeline, schema, regeneration, and validation commands.
