@echo off
setlocal

echo === SDLFun Win98/Dev-C++ Build ===
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
REM  Object output directory (gitignored)
REM ----------------------------------------------------------------
set "OBJDIR=raw\obj"
if not exist "%OBJDIR%" mkdir "%OBJDIR%"

REM ----------------------------------------------------------------
REM  Bullet Physics (cached — delete raw\obj\bl.o/bc.o/bd.o to force rebuild)
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
REM  Lua 5.1.5 — always rebuilt. The mapped drive between Win98 and
REM  the Linux dev host makes `if exist lua.o` unreliable (stale object
REM  from the other toolchain gets picked up). Lua is small; recompile
REM  every build is cheap.
REM  -DLUA_ANSI disables loadlib dlopen / Win32 branches that Dev-C++ 3.4
REM  can't satisfy. -Dluaall_c is Lua's own unity-build switch.
REM ----------------------------------------------------------------
echo Compiling Lua...
C:\Dev-Cpp\bin\gcc.exe -Ivendor\lua-5.1.5\src -Dluaall_c -DLUA_ANSI -O2 -c vendor\lua-5.1.5\src\lua_all.c -o %OBJDIR%\lua.o
if errorlevel 1 goto error

REM ----------------------------------------------------------------
REM  stb_vorbis (cached — delete raw\obj\vorbis.o to force rebuild)
REM ----------------------------------------------------------------
if exist %OBJDIR%\vorbis.o (
    echo Skipping stb_vorbis ^(vorbis.o cached^)
) else (
    echo Compiling stb_vorbis...
    C:\Dev-Cpp\bin\gcc.exe -O2 -c vendor\stb\stb_vorbis.c -o %OBJDIR%\vorbis.o
    if errorlevel 1 goto error
)

REM ----------------------------------------------------------------
REM  Compile main
REM ----------------------------------------------------------------
echo Compiling main...
C:\Dev-Cpp\bin\g++.exe -Ivendor\include -Ivendor\bullet3-3.25\src -Ivendor\lua-5.1.5\src -O2 -c main.cpp -o %OBJDIR%\main.o
if errorlevel 1 goto error

REM ----------------------------------------------------------------
REM  Link
REM ----------------------------------------------------------------
echo Linking...
C:\Dev-Cpp\bin\g++.exe %OBJDIR%\main.o %OBJDIR%\bl.o %OBJDIR%\bc.o %OBJDIR%\bd.o %OBJDIR%\lua.o %OBJDIR%\vorbis.o -o SDLFun.exe -Lvendor\lib -lmingw32 -lSDLmain -lSDL -lopengl32 -lOpenAL32
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
