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
REM  Compile the editor. -I%ENGINE% resolves SOOB-Core headers
REM  (texture.h, asset_registry.h); -I%FLTK% resolves <FL/...>.
REM ----------------------------------------------------------------
echo Compiling editor...
C:\Dev-Cpp\bin\g++.exe -DWIN32 -DWINVER=0x0500 -D_WIN32_WINNT=0x0500 -I%ENGINE% -I%FLTK% -Ivendor\bullet3-3.25\src -O2 -c editor.cpp -o %OBJDIR%\editor.o
if errorlevel 1 goto error

REM ----------------------------------------------------------------
REM  Link: FLTK (GL + core) + Bullet + Win32 GL/GDI. No -mwindows so
REM  the conLogf stdout stays visible while bringing the editor up.
REM ----------------------------------------------------------------
echo Linking...
C:\Dev-Cpp\bin\g++.exe %OBJDIR%\editor.o %OBJDIR%\bl.o %OBJDIR%\bc.o %OBJDIR%\bd.o -o SoobEditor.exe -L%FLTK%\lib -lfltk_gl -lfltk -lopengl32 -lglu32 -lole32 -luuid -lcomctl32 -lcomdlg32 -lgdi32 -lwsock32
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
