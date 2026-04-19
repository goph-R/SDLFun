@echo off
setlocal

echo === SDLFun Win98/Dev-C++ Build (OpenAL) ===
echo.

REM ----------------------------------------------------------------
REM  Check for OpenAL headers and import library.
REM  Options for Win98:
REM    - Creative's reference OpenAL32.dll (the historically correct pick;
REM      expose an MSVC .lib you'd convert to libOpenAL32.a via dlltool).
REM    - OpenAL Soft old release that still supports 9x — unverified.
REM  Either way, the files we expect in vendor/:
REM    vendor\include\AL\al.h, alc.h     ^(headers^)
REM    vendor\lib\libOpenAL32.a OR       ^(MinGW import library^)
REM    vendor\lib\libOpenAL32.dll.a      ^(OpenAL Soft MinGW variant^)
REM    OpenAL32.dll                      ^(next to the exe, or installed in System^)
REM ----------------------------------------------------------------
if not exist "vendor\include\AL\al.h" (
    echo ERROR: OpenAL headers missing.
    echo Place al.h and alc.h in vendor\include\AL\.
    goto error
)
if not exist "vendor\lib\libOpenAL32.a" (
    if not exist "vendor\lib\libOpenAL32.dll.a" (
        echo ERROR: OpenAL MinGW import library missing.
        echo Expected vendor\lib\libOpenAL32.a or libOpenAL32.dll.a.
        echo   From Creative SDK: convert OpenAL32.lib with dlltool.
        echo   From OpenAL Soft:  use libs\Win32\libOpenAL32.dll.a.
        goto error
    )
)

REM ----------------------------------------------------------------
REM  Bullet Physics (cached — delete bl.o/bc.o/bd.o to force rebuild)
REM ----------------------------------------------------------------
if exist bl.o (
    echo Skipping Bullet Linear Math ^(bl.o cached^)
) else (
    echo Compiling Bullet Linear Math...
    C:\Dev-Cpp\bin\g++.exe -Ivendor\bullet3-3.25\src -O2 -c vendor\bullet3-3.25\src\btLinearMathAll.cpp -o bl.o
    if errorlevel 1 goto error
)
if exist bc.o (
    echo Skipping Bullet Collision ^(bc.o cached^)
) else (
    echo Compiling Bullet Collision...
    C:\Dev-Cpp\bin\g++.exe -Ivendor\bullet3-3.25\src -O2 -c vendor\bullet3-3.25\src\btBulletCollisionAll.cpp -o bc.o
    if errorlevel 1 goto error
)
if exist bd.o (
    echo Skipping Bullet Dynamics ^(bd.o cached^)
) else (
    echo Compiling Bullet Dynamics...
    C:\Dev-Cpp\bin\g++.exe -Ivendor\bullet3-3.25\src -O2 -c vendor\bullet3-3.25\src\btBulletDynamicsAll.cpp -o bd.o
    if errorlevel 1 goto error
)

REM ----------------------------------------------------------------
REM  Compile main (with USE_OPENAL so sound.h picks the OpenAL backend)
REM ----------------------------------------------------------------
echo Compiling main...
C:\Dev-Cpp\bin\g++.exe -DUSE_OPENAL -Ivendor\include -Ivendor\bullet3-3.25\src -O2 -c main.cpp -o main.o
if errorlevel 1 goto error

REM ----------------------------------------------------------------
REM  Link (OpenAL32 instead of FMOD). Output name kept as SDLFun.exe
REM  so existing run/test habits don't change; delete SDLFun.exe first
REM  if you want to switch cleanly between FMOD and OpenAL builds.
REM ----------------------------------------------------------------
echo Linking...
C:\Dev-Cpp\bin\g++.exe main.o bl.o bc.o bd.o -o SDLFun.exe -Lvendor\lib -lmingw32 -lSDLmain -lSDL -lopengl32 -lOpenAL32
if errorlevel 1 goto error

echo.
echo === Build successful! Run SDLFun.exe ===
echo Make sure OpenAL32.dll is next to the exe or in the system directory.
goto end

:error
echo.
echo === Build FAILED ===
pause

:end
endlocal
