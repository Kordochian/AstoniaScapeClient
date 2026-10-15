# AstoniaScape client

RuneScape-inspired Astonia client, based on the Astonia community client (moac) with the
AstoniaScape changes: native Visual Studio build, options window, widescreen and zoom, movable /
resizable / scalable panels, quest log window, minimap and world map, skills panel with XP bars
and Effects tab, crafting windows, combat style panel, chat improvements.

## Building on Windows
`build-msvc.bat` (Visual Studio 2022/2026 with "C++ Clang tools", CMake, vcpkg). Output: `bin\moac.exe`.
The Makefile / zig builds from upstream are kept but are not what this project uses.

## Assets
Graphics, sounds and cursors (`res\gx1.zip`, `res\sx.zip`, `res\cursor\*.cur`, fonts) are not in
this repo. Take them from the client package (`F:\Claude_Astonia\64bit-port\astonia_client\res`).

## Other folders
- `launcher-src/` AstoniaLauncher (C#, `build-launcher.bat`)
- `history/source-changes/` the patch series from before this repo existed (0017-0025 are kept in the
  project files, not here)

Protocol: client and server protocol numbers must stay in step (see the server repo).
Upstream readme: `docs/upstream-readme.md`.
