# D2RR Item Database

Work-in-progress, standalone client addon for D2RLoader SDK 0.2.0 / ABI 4.
The project never installs itself into a game directory. Build and staging instructions
will be completed alongside implementation. Current source references are pinned in
`upstream.lock.json`. The runtime is offline and read-only.

Architecture: pinned website keyed JSON -> Python normalizer -> versioned JSON ->
independent C++ query/state library -> D2RLoader client plugin -> native launch panel
and Win32 game-owned database renderer. See `docs/RESEARCH.md` for API constraints.
