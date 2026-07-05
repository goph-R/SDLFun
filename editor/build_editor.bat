@echo off
setlocal

echo === SOOB Level Editor - Win98/Dev-C++ Build ===
echo.

REM ----------------------------------------------------------------
REM  Run this from the editor\ folder (where this .bat lives). REPO
REM  points at the repo root (its parent); every path below is relative
REM  to REPO, so the build works without %~dp0 (which command.com lacks
REM  on Win98). The exe is written to the repo root so it sits beside
REM  assets\ (assets are relative-pathed, exactly like the game).
REM ----------------------------------------------------------------
set "REPO=.."

REM ----------------------------------------------------------------
REM  FLTK 1.3.11 source lives under <repo>\vendor\fltk-1.3\FL and its
REM  static libs are produced by build_fltk.bat (run that FIRST). Headers
REM  are under %FLTK%\FL, libs libfltk.a / libfltk_gl.a under %FLTK%\lib.
REM  FLTK 1.4 needs C++11 -- the 1.3.x series is the one for Win98.
REM ----------------------------------------------------------------
set "FLTK=%REPO%\vendor\fltk-1.3\FL"
if not exist "%FLTK%\FL\Fl.H" (
    echo ERROR: FLTK headers not found at %FLTK%\FL\.
    echo Run this .bat from the editor\ folder, with FLTK at
    echo   ^<repo^>\vendor\fltk-1.3\FL  ^(or edit FLTK at the top^).
    goto error
)
if not exist "%FLTK%\lib\libfltk_gl.a" (
    echo ERROR: FLTK libraries not built. Run build_fltk.bat first.
    goto error
)

set "ENGINE=%REPO%\..\SOOB-Core"
set "OBJDIR=%REPO%\raw\obj"
if not exist "%OBJDIR%" mkdir "%OBJDIR%"

REM ----------------------------------------------------------------
REM  Bullet Physics (shared with the game build; delete <repo>\raw\obj\
REM  bl.o bc.o bd.o to force a rebuild). The editor never simulates, but
REM  struct Game embeds PhysWorld/NavGraph by value, so it links Bullet.
REM ----------------------------------------------------------------
if exist %OBJDIR%\bl.o (
    echo Skipping Bullet Linear Math ^(bl.o cached^)
) else (
    echo Compiling Bullet Linear Math...
    C:\Dev-Cpp\bin\g++.exe -I%REPO%\vendor\bullet3-3.25\src -O2 -c %REPO%\vendor\bullet3-3.25\src\btLinearMathAll.cpp -o %OBJDIR%\bl.o
    if errorlevel 1 goto error
)
if exist %OBJDIR%\bc.o (
    echo Skipping Bullet Collision ^(bc.o cached^)
) else (
    echo Compiling Bullet Collision...
    C:\Dev-Cpp\bin\g++.exe -I%REPO%\vendor\bullet3-3.25\src -O2 -c %REPO%\vendor\bullet3-3.25\src\btBulletCollisionAll.cpp -o %OBJDIR%\bc.o
    if errorlevel 1 goto error
)
if exist %OBJDIR%\bd.o (
    echo Skipping Bullet Dynamics ^(bd.o cached^)
) else (
    echo Compiling Bullet Dynamics...
    C:\Dev-Cpp\bin\g++.exe -I%REPO%\vendor\bullet3-3.25\src -O2 -c %REPO%\vendor\bullet3-3.25\src\btBulletDynamicsAll.cpp -o %OBJDIR%\bd.o
    if errorlevel 1 goto error
)

REM ----------------------------------------------------------------
REM  Lua 5.1.5 (unity build) — edit_assets.h runs assets.lua in a bare
REM  Lua state to register models/textures. Same object the game build
REM  produces; delete <repo>\raw\obj\lua.o to force a rebuild.
REM ----------------------------------------------------------------
if exist %OBJDIR%\lua.o (
    echo Skipping Lua ^(lua.o cached^)
) else (
    echo Compiling Lua...
    C:\Dev-Cpp\bin\gcc.exe -I%ENGINE%\vendor\lua-5.1.5\src -Dluaall_c -DLUA_ANSI -O2 -c %ENGINE%\vendor\lua-5.1.5\src\lua_all.c -o %OBJDIR%\lua.o
    if errorlevel 1 goto error
)

REM ----------------------------------------------------------------
REM  Compile the editor from the editor\ folder (CWD). editor.cpp is
REM  local; its edit_*.h resolve next to it. -I%REPO% adds the engine-root
REM  headers it includes by bare name (obj_loader.h, game.h,
REM  render_level.h, ...); -I%ENGINE% resolves SOOB-Core headers
REM  (texture.h, asset_registry.h); -I%FLTK% resolves <FL/...>; the lua
REM  src path resolves edit_assets.h's <lua.h>.
REM ----------------------------------------------------------------
echo Compiling editor...
C:\Dev-Cpp\bin\g++.exe -DWIN32 -DWINVER=0x0500 -D_WIN32_WINNT=0x0500 -I%REPO% -I%ENGINE% -I%FLTK% -I%REPO%\vendor\bullet3-3.25\src -I%ENGINE%\vendor\lua-5.1.5\src -O2 -c editor.cpp -o %OBJDIR%\editor.o
if errorlevel 1 goto error

REM ----------------------------------------------------------------
REM  Link: FLTK (GL + core) + Bullet + Win32 GL/GDI. No -mwindows so the
REM  conLogf stdout stays visible. Output to the repo root (beside assets\).
REM ----------------------------------------------------------------
echo Linking...
C:\Dev-Cpp\bin\g++.exe %OBJDIR%\editor.o %OBJDIR%\bl.o %OBJDIR%\bc.o %OBJDIR%\bd.o %OBJDIR%\lua.o -o %REPO%\SoobEditor.exe -L%FLTK%\lib -lfltk_gl -lfltk -lopengl32 -lglu32 -lole32 -luuid -lcomctl32 -lcomdlg32 -lgdi32 -lwsock32
if errorlevel 1 goto error

echo.
echo === Build successful! Run SoobEditor.exe from the repo root ===
goto end

:error
echo.
echo === Build FAILED ===
pause

:end
endlocal
