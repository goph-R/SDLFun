# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project overview

SDLFun is a small FPS engine targeting the full span from **Windows 98** (Dev-C++ / MinGW 3.4, SDL 1.2, fixed-function OpenGL, OpenAL Soft 1.9.563) up to modern Linux/Windows. The "runs on a Pentium 4" constraint is deliberate and drives most architectural decisions — no C++11 features, no shaders, no modern GL, header-only modules, static-libgcc linking for the Win10 build.

## Build commands

There are **four** independent build systems, each for a different target. They all compile `main.cpp` + Bullet, but differ in compiler/toolchain/include paths.

| Target | Command | Output |
|---|---|---|
| Linux (system SDL) | `make` (needs `libsdl1.2-dev`, `libopenal-dev`) | `sdlfun` |
| Windows 98 / Dev-C++ | `build.bat` (uses `C:\Dev-Cpp\bin\g++.exe`) | `SDLFun.exe` |
| Windows 10 (portable WinLibs MinGW in `vendor_win10/`) | `build_win10.bat` | `SDLFun_w10.exe` |
| CMake | `mkdir build && cd build && cmake .. && make` — add `-DUSE_VENDOR_SDL=ON` to use vendored SDL | `SDLFun` |

Audio is OpenAL 1.1 / OpenAL Soft via the header-only wrapper in `sound.h`. The repo vendors `OpenAL32.dll` (1.25.1, used on Win10) and `OpenAL32-win98.dll` (1.9.563, the only release tested working on Win98 — rename to `OpenAL32.dll` before running on a Win98 target). Migration history in `docs/plan-openal.md`.

Bullet is compiled from the three unity-build files `vendor/bullet3-3.25/src/btLinearMathAll.cpp`, `btBulletCollisionAll.cpp`, `btBulletDynamicsAll.cpp`. These produce `raw/obj/bl.o`, `bc.o`, `bd.o` (batch scripts) or `bullet_linear_math.o` etc. (Makefile, still in repo root). Together they take ~60–90s to compile on a modern machine and ~only change when you bump the Bullet version.

**Caching**: the Linux Makefile tracks `.cpp` → `.o` dependencies normally. The Windows batch scripts (`build.bat`, `build_win10.bat`) write all `.o` output to `raw/obj/` (gitignored) and skip the Bullet compile if the cached files exist (`if exist raw\obj\bl.o ...`). They do **not** track Bullet header changes, so if you edit a Bullet header or switch compilers/toolchains, delete `raw\obj\bl.o bc.o bd.o` to force a rebuild.

There are no tests and no lint step.

## Running

Run the built executable from the repo root — it reads everything under `assets/` (levels, models, textures, sounds) as **relative paths**. `cd build && ./SDLFun` will fail to find assets.

Flow: boot lands on the **main menu** (mouse free). Buttons CONTINUE / NEW GAME / OPTIONS / EXIT navigate with Up/Down + Enter or mouse. CONTINUE is disabled until a game has been started at least once in this session. NEW GAME tears down any prior session and inits a fresh one. In-game **Esc returns to the menu** (game state preserved); CONTINUE is then enabled. EXIT opens a confirm dialog (OK/Cancel, Left/Right to switch, Enter fires, Esc cancels).

In-game controls: WASD move, mouse look, Space jump, F flashlight, Left-click fire, B collider wireframes, N nav graph, P print player position, Esc back to menu. The window grabs the mouse while in-game and releases it in the menu.

Windows-specific: SDL 1.2's fullscreen path drops the monitor's refresh rate to 60Hz because it calls `ChangeDisplaySettings` without a frequency. `main.cpp` samples the configured desktop rate via `EnumDisplaySettings(ENUM_REGISTRY_SETTINGS)` before `SDL_Init` and re-applies it with `ChangeDisplaySettingsEx` after `SDL_SetVideoMode` when `-fullscreen` is passed. No-op on Linux (the helpers are `#ifdef _WIN32`). If the chosen resolution doesn't support the saved rate, the override is skipped with a log line and the driver's default stays in effect.

## Architecture

### Header-only modules included from main.cpp

The whole engine is `main.cpp` plus header-only modules with `static` functions. Include order in `main.cpp` matters — e.g. `entity.h` depends on `obj_loader.h`, `texture.h`, and `iqm.h`.

- `obj_loader.h` — OBJ/MTL parser. Parses custom `# lm_map`, `# tile_scale`, `# tile_offset` comments from MTL. `ObjMesh` holds verts/normals/texcoords/tris plus up to 32 materials and 32 **sectors**. `objBuildSectors()` sorts triangles by material to produce one draw batch per material.
- `texture.h` — 24/32-bit BMP and TGA loader, plus a `TexCache` keyed by filename so the same texture isn't uploaded twice. Wrap mode is a parameter — diffuse tiling textures use `GL_REPEAT`, lightmaps use `GL_CLAMP_TO_EDGE`.
- `iqm.h` — Inter-Quake Model loader with skeletal animation (bone matrix palette computed per frame, software-skinned on CPU because the target is fixed-function GL).
- `entity.h` — flat array of `Entity` structs (tagged union by `EntityType`). `EntityList` is ~4MB (256 entities each with inline `ObjMesh` + `IqmModel`) and is heap-allocated from `main()` to avoid a stack blowup. `entLoadFile()` parses `assets/levels/test_level.ent`.
- `physics.h` — Bullet world wrapper: static `btBvhTriangleMeshShape` for the level, `btKinematicCharacterController` for the player. `physStep()` includes an explicit ceiling-penetration raycast correction each frame — do not remove it; the character controller doesn't clamp correctly against tight ceilings on its own.
- `flashlight.h` — CPU-side dynamic lightmap (Half-Life 1 / GoldSrc style). At load time it rasterizes level triangles into UV space to build a `worldPosMap[y][x] → worldXYZ` table. Each frame the flashlight raycasts to a hit point, modifies a region of the lightmap's RGB pixels in RAM, and re-uploads via `glTexSubImage2D`. This is intentional: it gives per-texel flashlight resolution on fixed-function hardware where per-pixel shaders don't exist. See the top-of-file block comment for the full algorithm. **Island margin**: use **0.04** in Blender's Smart UV Project when unwrapping for the lightmap — tighter margins produce visible spill artifacts when the flashlight writes near triangle UV boundaries.
- `ui.h` — 2D/HUD primitives. Draws on a **virtual canvas**: 1080 units tall, width scales with aspect ratio, origin at screen center with Y growing down. `uiGetWidth()` / `uiGetHeight()` return the virtual dimensions for positioning against edges. `uiBegin`/`uiEnd` switch to ortho virtual-space, `uiText` / `uiQuad` / `uiBar` / `uiIcon` draw into it. `uiMouseToVirtual` converts SDL pixel-space mouse coords to this canvas for menu hit-testing. `uiText` supports `UI_ALIGN_*` flags (TOP/MIDDLE/BOTTOM bit-OR LEFT/CENTER/RIGHT) for anchoring. Two text paths: (1) a built-in 8x8 ASCII bitmap atlas built at `uiInit()` from embedded public-domain font8x8 data (no disk asset), used as fallback; (2) BMFont rendering — `uiFontLoad` parses AngelCode-format `.fnt` text files and uploads their 32-bit RGBA TGA atlases (alpha channel = antialiased glyph mask). Fonts are registered by name from the `fonts` table in `assets.lua` during `scriptLoadAssets`. Multiple logical names can point at the same `.fnt` file — `uiFontLoad` deduplicates by source path so alias entries (e.g. `button_font` → `orbitron_small.fnt`) share one atlas texture; the alias's `ownsTex=0` flag prevents a double-free at shutdown. `uiText`'s optional `fontName` argument (defaults to `"default"`) picks which font to draw with; unknown names fall back to `"default"`, and if no fonts are loaded at all the 8x8 atlas is used. The `scale` parameter keeps its legacy meaning (target line height = `scale * 8` virtual pixels) so existing call sites don't need to change when a BMFont is loaded.
- `game.h` — one active gameplay session. `Game` holds the level OBJ, entities list, Bullet world, nav graph, dynamic lightmap, per-game texture cache, and the player's transient state (yaw/pitch/velocity/flashlight/timers/fps). `gameInit` loads the level, inits physics (before sector-sorting), builds entity colliders, sets up the dynamic lightmap, builds the nav graph, and runs `scripts/main.lua` + `on_start()`. `gameFree` tears everything down symmetrically so "Start New Game" can run twice. Long-lived app subsystems (UI, audio, script runtime, asset registry) stay outside Game and are passed into `gameInit` by pointer — `gameInit` rebinds `ScriptSystem::entities` to its session's entity list and `gameFree` sets it back to NULL.
- `menu.h` — screen system layered on top of the game loop. `AppState` holds the top-level state machine (mode = `MODE_MENU` | `MODE_GAME`, running flag, `Game*` pointer that's NULL until first New Game, menu-scope `TexCache`, and a small fixed `ScreenStack`). Screens are tagged-union (`MainMenu` / `Options` / `Dialog`) dispatched through `screenKey` / `screenMouse` / `screenRender`. When in `MODE_MENU`, `menuTick` polls SDL events, routes them to the top screen, and draws screens bottom-up so overlays (Options, Dialog) render on top of MainMenu with 50% black dim quads. Actions that need game-state side effects (New Game, Continue, Exit) don't touch Game from inside menu.h — they set `AppState::pendingAction`, which `main.cpp` processes each frame to call `gameInit` / `gameFree` or flip `app.running`. When the user picks New Game with a running session, MainMenu pushes a `DLG_CONFIRM_NEW_GAME` dialog instead of firing the action directly — the dialog's OK maps to the same `PENDING_NEW_GAME` in `dialogFire`, so the confirm path and the first-time path share one teardown/reinit branch. Mouse grab is toggled on mode transitions (off in menu so the cursor can roam the UI; on in game for mouselook). `drawLoadingScreen` paints a cover-fit `loading_bg` + "LOADING" label bottom-right and swaps buffers once — called from the `PENDING_NEW_GAME` branch before `gameInit` blocks the thread, so the window shows something meaningful during the synchronous level+physics load. Assets used: `menu_bg`, `loading_bg` (cover-fit full-canvas backgrounds), `logo` (top-left with padding), `dialog_bg` (tiled in the padded dialog rect), `button_font` / `dialog_title` / `menu_title_font` (BMFont aliases of orbitron / orbitron_small).

### Rendering pipeline

`renderLevelSectored()` is the main level draw path. For each sector:
1. Diffuse UVs are computed from world position via `computeTilingUV()` (Quake-style box mapping — picks XZ/ZY/XY plane based on which axis the face normal is most aligned with). Diffuse textures **tile** via `GL_REPEAT` and the OBJ UV set is ignored for them.
2. Lightmap UVs come from the OBJ's UV set (unwrapped in Blender, baked in Cycles — see `docs/leveldes.md`). Lightmap uses `GL_CLAMP_TO_EDGE`.
3. Both are combined with ARB_multitexture (`GL_MODULATE` on both units). When a lightmap is active, `GL_LIGHTING` is disabled — the baked lightmap already contains all static lighting, and GL lighting would re-add a directional bias that darkens ceilings.

On Win98/old MinGW, `glActiveTextureARB` / `glMultiTexCoord2fARB` are not in the headers, so `initMultitexture()` loads them via `SDL_GL_GetProcAddress` into function pointers (`MT_ActiveTexture` / `MT_MultiTexCoord2f` macros). On Linux, the macros resolve directly to the GL symbols.

### Level data flow

1. Blender → Wavefront OBJ with UVs + 24-bit BMP bakes (`docs/leveldes.md` documents this pipeline).
2. Entities are authored separately in `assets/levels/test_level.ent` (plain text, one entity per line: `type name group x y z rotY key=value...`). Entity `mesh=` / `tex=` / `iqm=` paths point at `assets/models/…` and `assets/textures/…`.
3. `main()`: loads OBJ, loads entities, finds player spawn from entities, inits physics from OBJ triangles, **then** calls `objBuildSectors()` (sorting must happen after physics init because sorting reorders `mesh->tris`).
4. The legacy single-texture path (`renderLevel`) is still used when the OBJ has no materials — it loads `assets/levels/diffuse.bmp` + `assets/levels/lightmap.bmp` from the working directory.

## Constraints worth knowing before editing

- **No C++11.** Dev-C++ 4.x ships GCC 3.4. No `auto`, no `nullptr`, no range-for, no `<cstdint>`. Use C-style `NULL`, explicit types, classic `for (int i = 0; ...)`. C-style casts and `malloc`/`free` are used consistently rather than `new`/`delete`.
- **No shaders.** Fixed-function only. Matrix math is done manually (`glSetPerspective`, `glLookAt` implemented in `main.cpp` — the system `gluPerspective`/`gluLookAt` aren't linked).
- **Assets are relative-pathed.** Don't add `chdir` calls or absolute-path asset lookups.
- **Header-only modules with `static` functions.** Don't split a module into .h/.cpp — every target compiles a single TU (`main.cpp`) plus Bullet. If you add a new module, follow the header-only `static` convention.
- The `docs/PLAN_*.md` / `docs/leveldes.md` files are **design docs**, not generated output — read them to understand intended direction before large refactors.
