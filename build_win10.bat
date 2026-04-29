@echo off
setlocal

echo === SDLFun Win10 MinGW Build ===
echo.

REM ----------------------------------------------------------------
REM  Portable MinGW from vendor_win10\mingw32
REM ----------------------------------------------------------------
set "GPP=vendor_win10\mingw32\bin\g++.exe"
set "GCC=vendor_win10\mingw32\bin\gcc.exe"

if not exist "%GPP%" (
    echo ERROR: MinGW not found at %GPP%
    echo Download WinLibs i686 and extract to vendor_win10\mingw32\
    echo https://github.com/brechtsanders/winlibs_mingw/releases
    goto error
)

REM ----------------------------------------------------------------
REM  Check for OpenAL headers and import library
REM ----------------------------------------------------------------
if not exist "vendor_win10\include\AL\al.h" (
    echo ERROR: OpenAL headers missing.
    echo Download OpenAL Soft 1.23.x prebuilt binaries and place:
    echo   AL\al.h, AL\alc.h -^> vendor_win10\include\AL\
    echo   libOpenAL32.dll.a or libopenal.dll.a -^> vendor_win10\lib\
    echo   OpenAL32.dll -^> next to the exe ^(or in PATH^)
    echo From: https://www.openal-soft.org/#download
    goto error
)

for /f "tokens=*" %%V in ('%GPP% -dumpversion') do set "GCC_VER=%%V"
echo Found GCC %GCC_VER%
echo.

set "OPTS=-O2 -Ivendor_win10\include -Ivendor\bullet3-3.25\src -Ivendor\lua-5.1.5\src"

REM ----------------------------------------------------------------
REM  Object output directory (gitignored)
REM ----------------------------------------------------------------
set "OBJDIR=raw\obj"
if not exist "%OBJDIR%" mkdir "%OBJDIR%"

REM ----------------------------------------------------------------
REM  Compile Bullet Physics (cached — delete raw\obj\bl.o/bc.o/bd.o to force rebuild)
REM ----------------------------------------------------------------
if exist %OBJDIR%\bl.o (
    echo Skipping Bullet Linear Math ^(bl.o cached^)
) else (
    echo Compiling Bullet Linear Math...
    %GPP% %OPTS% -c vendor\bullet3-3.25\src\btLinearMathAll.cpp -o %OBJDIR%\bl.o
    if errorlevel 1 goto error
)

if exist %OBJDIR%\bc.o (
    echo Skipping Bullet Collision ^(bc.o cached^)
) else (
    echo Compiling Bullet Collision...
    %GPP% %OPTS% -c vendor\bullet3-3.25\src\btBulletCollisionAll.cpp -o %OBJDIR%\bc.o
    if errorlevel 1 goto error
)

if exist %OBJDIR%\bd.o (
    echo Skipping Bullet Dynamics ^(bd.o cached^)
) else (
    echo Compiling Bullet Dynamics...
    %GPP% %OPTS% -c vendor\bullet3-3.25\src\btBulletDynamicsAll.cpp -o %OBJDIR%\bd.o
    if errorlevel 1 goto error
)

REM ----------------------------------------------------------------
REM  Lua 5.1.5 (cached — delete raw\obj\lua.o to force rebuild)
REM ----------------------------------------------------------------
if exist %OBJDIR%\lua.o (
    echo Skipping Lua ^(lua.o cached^)
) else (
    echo Compiling Lua...
    %GCC% -Ivendor\lua-5.1.5\src -Dluaall_c -O2 -c vendor\lua-5.1.5\src\lua_all.c -o %OBJDIR%\lua.o
    if errorlevel 1 goto error
)

REM ----------------------------------------------------------------
REM  stb_vorbis (cached — delete raw\obj\vorbis.o to force rebuild)
REM ----------------------------------------------------------------
if exist %OBJDIR%\vorbis.o (
    echo Skipping stb_vorbis ^(vorbis.o cached^)
) else (
    echo Compiling stb_vorbis...
    %GCC% -O2 -c vendor\stb\stb_vorbis.c -o %OBJDIR%\vorbis.o
    if errorlevel 1 goto error
)

REM ----------------------------------------------------------------
REM  Compile main
REM ----------------------------------------------------------------
echo Compiling SDL main stub...
%GCC% -c vendor_win10\sdl_main.c -o %OBJDIR%\sdl_main.o
if errorlevel 1 goto error

echo Compiling main...
%GPP% %OPTS% -c main.cpp -o %OBJDIR%\main.o
if errorlevel 1 goto error

REM ----------------------------------------------------------------
REM  Link
REM ----------------------------------------------------------------
echo Linking...
%GPP% %OBJDIR%\main.o %OBJDIR%\sdl_main.o %OBJDIR%\bl.o %OBJDIR%\bc.o %OBJDIR%\bd.o %OBJDIR%\lua.o %OBJDIR%\vorbis.o -o SDLFun_w10.exe -Lvendor_win10\lib -lmingw32 -lSDL -lopengl32 -lOpenAL32 -static-libgcc -static-libstdc++
if errorlevel 1 goto error

REM ----------------------------------------------------------------
REM  Copy DLLs next to exe (if not already there)
REM ----------------------------------------------------------------
if not exist "libwinpthread-1.dll" (
    copy vendor_win10\mingw32\bin\libwinpthread-1.dll . >nul
)
if not exist "OpenAL32.dll" (
    if exist "vendor_win10\OpenAL32.dll" (
        copy vendor_win10\OpenAL32.dll . >nul
    ) else (
        echo NOTE: OpenAL32.dll not found next to exe. Place the OpenAL Soft
        echo       runtime DLL ^(renamed from soft_oal.dll if needed^) here.
    )
)

echo.
echo === Build successful! Run SDLFun_w10.exe ===
goto end

:error
echo.
echo === Build FAILED ===
pause

:end
endlocal
