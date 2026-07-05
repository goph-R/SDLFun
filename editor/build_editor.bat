@echo off
setlocal

echo === SOOB Level Editor - Win98/Dev-C++ Build ===
echo.

REM ----------------------------------------------------------------
REM  FLTK 1.3.11 source lives under vendor\fltk-1.3\FL and its static
REM  libs are produced by build_fltk.bat (run that FIRST). Headers are
REM  under %FLTK%\FL, libs libfltk.a / libfltk_gl.a under %FLTK%\lib.
REM  FLTK 1.4 needs C++11 -- the 1.3.x series is the one for Win98.
REM ----------------------------------------------------------------
set "FLTK=vendor\fltk-1.3\FL"
if not exist "%FLTK%\FL\Fl.H" (
    echo ERROR: FLTK headers not found at %FLTK%\FL\.
    echo Edit the FLTK variable at the top of build_editor.bat.
    goto error
)
if not exist "%FLTK%\lib\libfltk_gl.a" (
    echo ERROR: FLTK libraries not built. Run build_fltk.bat first.
    goto error
)

set "ENGINE=..\SOOB-Core"
set "OBJDIR=raw\obj"
if not exist "%OBJDIR%" mkdir "%OBJDIR%"

REM ----------------------------------------------------------------
REM  Bullet Physics (shared with the game build; delete raw\obj\bl.o
REM  bc.o bd.o to force a rebuild). The editor never simulates, but
REM  struct Game embeds PhysWorld/NavGraph by value, so it links Bullet.
REM ----------------------------------------------------------------
if exist %OBJDIR%\bl.o (
    echo Skipping Bullet Linear Math ^(bl.o cached^)
) else (
    echo Compiling Bullet Linear Math...
    C:\Dev-Cpp\bin\g++.exe -Ivendor\bullet3-3.25\src -O2 -c vendor\bullet3-3.25\src\btLinearMathAll.cpp -o %OBJDIR%\bl.o
    if errorlevel 1 goto error
)
if exist %OBJDIR%\bc.o (
    echo Skipping Bullet Collision ^(bc.o cached^)
) else (
    echo Compiling Bullet Collision...
    C:\Dev-Cpp\bin\g++.exe -Ivendor\bullet3-3.25\src -O2 -c vendor\bullet3-3.25\src\btBulletCollisionAll.cpp -o %OBJDIR%\bc.o
    if errorlevel 1 goto error
)
if exist %OBJDIR%\bd.o (
    echo Skipping Bullet Dynamics ^(bd.o cached^)
) else (
    echo Compiling Bullet Dynamics...
    C:\Dev-Cpp\bin\g++.exe -Ivendor\bullet3-3.25\src -O2 -c vendor\bullet3-3.25\src\btBulletDynamicsAll.cpp -o %OBJDIR%\bd.o
    if errorlevel 1 goto error
)

REM ----------------------------------------------------------------
REM  Lua 5.1.5 (unity build) — edit_assets.h runs assets.lua in a bare
REM  Lua state to register models/textures. Same object the game build
REM  produces; delete raw\obj\lua.o to force a rebuild.
REM ----------------------------------------------------------------
if exist %OBJDIR%\lua.o (
    echo Skipping Lua ^(lua.o cached^)
) else (
    echo Compiling Lua...
    C:\Dev-Cpp\bin\gcc.exe -I%ENGINE%\vendor\lua-5.1.5\src -Dluaall_c -DLUA_ANSI -O2 -c %ENGINE%\vendor\lua-5.1.5\src\lua_all.c -o %OBJDIR%\lua.o
    if errorlevel 1 goto error
)

REM ----------------------------------------------------------------
REM  Compile the editor. editor.cpp now lives in editor\, so -I. adds the
REM  engine-root headers it includes by bare name (obj_loader.h, game.h,
REM  render_level.h, ...). -I%ENGINE% resolves SOOB-Core headers (texture.h,
REM  asset_registry.h); -I%FLTK% resolves <FL/...>; the lua src path resolves
REM  edit_assets.h's <lua.h>. The editor's own edit_*.h resolve next to
REM  editor.cpp automatically. Run this from the repo root.
REM ----------------------------------------------------------------
echo Compiling editor...
C:\Dev-Cpp\bin\g++.exe -DWIN32 -DWINVER=0x0500 -D_WIN32_WINNT=0x0500 -I. -I%ENGINE% -I%FLTK% -Ivendor\bullet3-3.25\src -I%ENGINE%\vendor\lua-5.1.5\src -O2 -c editor\editor.cpp -o %OBJDIR%\editor.o
if errorlevel 1 goto error

REM ----------------------------------------------------------------
REM  Link: FLTK (GL + core) + Bullet + Win32 GL/GDI. No -mwindows so
REM  the conLogf stdout stays visible while bringing the editor up.
REM ----------------------------------------------------------------
echo Linking...
C:\Dev-Cpp\bin\g++.exe %OBJDIR%\editor.o %OBJDIR%\bl.o %OBJDIR%\bc.o %OBJDIR%\bd.o %OBJDIR%\lua.o -o SoobEditor.exe -L%FLTK%\lib -lfltk_gl -lfltk -lopengl32 -lglu32 -lole32 -luuid -lcomctl32 -lcomdlg32 -lgdi32 -lwsock32
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
