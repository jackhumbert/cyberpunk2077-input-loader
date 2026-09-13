# Input Loader

Cyberpunk 2077 RED4ext plugin: merges every `r6/input/*.xml` (and plugin-registered xml files) into the game's input config and writes the result to `r6/cache/`, plus `engine/config/platform/pc/input_loader.ini` so the game loads it. C++ only, no redscript, no hooks. Shipped under `red4ext/plugins/input_loader/`.

Sibling repos with the same build/release shape: `../flight_control` (Let There Be Flight; its CLAUDE.md has the full detail), `../mod_settings`, `../in_world_navigation`. LTBF and Mod Settings depend on this plugin's `InputLoader.hpp` API (`include/`).

## Layout

- `src/` - `Main.cpp` (entry, merge logic), `IO/`, `Utils.*`. `include/InputLoader.hpp` is the public header other plugins use.
- No vanilla xml copies are bundled (they went stale and users overwrote them with `r6/config`); `src/Main.cpp` holds `vanillaContexts`/`vanillaMappings`, a size + SHA-256 table of the game's `r6/config` xmls per patch, and warns at startup when a base file is not on it (another mod overwrote it, or a new patch).
- `deps/` - submodules: `red4ext.sdk` (jackhumbert fork, `new-types`), `cyberpunk_cmake`, `pugixml`, `spdlog`, `detours`. `CMAKE_POLICY_VERSION_MINIMUM` is set because pugixml/detours declare pre-3.5 minimums.
- `game_dir/`, `game_dir_debug/` build outputs zipped for release.

## Build

MSVC 2022 + Ninja + CMake 3.24+ (VS dev shell):

```
cmake -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo -G Ninja
cmake --build build
cmake --install build
```

## Startup order

Everything is merged synchronously in `Main(Load)`, and `Add()` merges on arrival for plugins that load later. RED4ext loads all plugins before the game's state machine starts, but its game-state callbacks are too late: plugin `BaseInitialization` OnEnter runs after the game's own `CBaseInitializationState::OnEnter` (which reads the options ini), and OnExit fires roughly 50 s after plugin load, after the game has read `r6/cache`. Merging only in OnExit was why fresh installs needed a second launch. The OnExit callback is kept only as a refresh.

## Versioning gotcha

Until 2026-09-13 `CMakeLists.txt` hardcoded `project(input_loader VERSION 0.1.1)`, so every release's `Query()` reported 0.1.1 and dependents (LTBF) had to keep their floor at 0.1.1. It now derives the version from the git tag (`configure_version_from_git`), so the next tag is the first release that reports its real version; dependents can raise their floors only after that release ships.

## Updating for a game patch

1. No address hashes to check (`python tools/check_hashes.py` confirms zero).
2. Add the new `r6/config/inputContexts.xml` / `inputUserMappings.xml` size and SHA-256 (`Get-FileHash` on a verified install) to the vanilla table in `src/Main.cpp`; otherwise every user gets the "not a known vanilla file" warning.
3. Bump `deps/red4ext.sdk` and `deps/cyberpunk_cmake` to the commits that know the patch (do the SDK work in `flight_control` first).
4. Build, install, launch, check `red4ext/logs/input_loader.log`.
5. Tag.

State on 2026-09-13: v0.2.3 (built for 2.30) works on game 2.31; pins bumped to the 2.31 SDK.

## Releases

Push a `vX.Y.Z` tag. `.github/workflows/release.yaml` builds, generates the changelog with git-cliff (`cliff.toml`, conventional commits), publishes the GitHub release, then uploads to Nexus mod 4575 (file id 2601067) via `Nexus-Mods/upload-action`. Needs the repo secret `NEXUSMODS_API_KEY`; the v3 mod id is resolved from the site id first. Full-depth checkout is required for the changelog.

## Conventions

- No em-dashes anywhere.
- Don't commit `build/`, `game_dir*/`, or `compile_commands.json`.
