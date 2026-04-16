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

for /f "tokens=*" %%V in ('%GPP% -dumpversion') do set "GCC_VER=%%V"
echo Found GCC %GCC_VER%
echo.

set "OPTS=-O2 -Ivendor_win10\include -Ivendor\bullet3-3.25\src"

REM ----------------------------------------------------------------
REM  Compile Bullet Physics
REM ----------------------------------------------------------------
echo Compiling Bullet Linear Math...
%GPP% %OPTS% -c vendor\bullet3-3.25\src\btLinearMathAll.cpp -o bl.o
if errorlevel 1 goto error

echo Compiling Bullet Collision...
%GPP% %OPTS% -c vendor\bullet3-3.25\src\btBulletCollisionAll.cpp -o bc.o
if errorlevel 1 goto error

echo Compiling Bullet Dynamics...
%GPP% %OPTS% -c vendor\bullet3-3.25\src\btBulletDynamicsAll.cpp -o bd.o
if errorlevel 1 goto error

REM ----------------------------------------------------------------
REM  Compile main
REM ----------------------------------------------------------------
echo Compiling SDL main stub...
%GCC% -c vendor_win10\sdl_main.c -o sdl_main.o
if errorlevel 1 goto error

echo Compiling main...
%GPP% %OPTS% -c main.cpp -o main.o
if errorlevel 1 goto error

REM ----------------------------------------------------------------
REM  Link
REM ----------------------------------------------------------------
echo Linking...
%GPP% main.o sdl_main.o bl.o bc.o bd.o -o SDLFun_w10.exe -Lvendor_win10\lib -lmingw32 -lSDL -lopengl32 -lfmod -static-libgcc -static-libstdc++
if errorlevel 1 goto error

REM ----------------------------------------------------------------
REM  Copy DLLs next to exe (if not already there)
REM ----------------------------------------------------------------
REM Copy MinGW pthread runtime next to exe (SDL.dll and fmod.dll are already present)
if not exist "libwinpthread-1.dll" (
    copy vendor_win10\mingw32\bin\libwinpthread-1.dll . >nul
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
