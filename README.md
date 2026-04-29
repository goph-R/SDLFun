# SDLFun

A small FPS engine targeting everything from Windows 98 (Pentium 4, SDL 1.2, fixed-function OpenGL) through modern Linux and Windows. Bullet Physics, OpenAL Soft, header-only modules, no shaders.

![SDLFun running on Windows 10](docs/screenshot.jpg)

## Building

See `CLAUDE.md` for the full build matrix and toolchain details.

Quick reference:
- **Linux**: `make` (needs `libsdl1.2-dev`, `libopenal-dev`) → `sdlfun`
- **Windows 98 / Dev-C++**: `build.bat` → `SDLFun.exe`
- **Windows 10 / portable MinGW**: `build_win10.bat` → `SDLFun_w10.exe`
- **CMake**: `mkdir build && cd build && cmake .. && make`

## Controls

Boot lands on the main menu. Up/Down + Enter (or mouse) navigate the CONTINUE / NEW GAME / OPTIONS / EXIT buttons. The exit dialog uses Left/Right + Enter / Esc.

In-game: WASD move, mouse look, Space jump, F flashlight, Left-click fire, backtick (`) drops down the dev console (Lua REPL), Esc back to menu.

CLI flags: `-w <width>`, `-h <height>`, `-fullscreen`.

## Credits and licensing

### Code

The engine source code (everything under the repo except `vendor/`, `vendor_win10/`, and the assets called out below) is open source. Licenses for bundled third-party libraries:

| Library | License | Usage |
|---|---|---|
| SDL 1.2 | LGPL 2.1 | dynamically linked (`SDL.dll`) |
| OpenAL Soft | LGPL 2.1 | dynamically linked (`OpenAL32.dll`) |
| Bullet Physics | zlib | compiled from vendored source |

### Assets

All art, level, and texture assets in this repository are **© Dynart**, all rights reserved. This includes, but is not limited to:
- `assets/levels/test_level.obj` and its bakes (`diffuse.bmp`, `lightmap.bmp`)
- Everything under `assets/models/` and `assets/textures/` except the exceptions below
- Everything under `raw/` (Blender sources, GIMP sources, reference photos)

The following assets are **not** original and are bundled under their own terms:
- `assets/models/mrfixit.iqm`, `assets/textures/Head.tga`, `assets/textures/Body.tga` — from Lee Salzman's IQM sample pack. See the original IQM distribution for licensing.
- `assets/fonts/orbitron*.fnt` + `assets/fonts/orbitron*.tga` — BMFont bakes of Orbitron by Matt McInerney, licensed under the SIL Open Font License 1.1.
