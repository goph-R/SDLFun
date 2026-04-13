# SDLFun

A minimal SDL 1.2 project that runs on Windows 98 through modern Linux/Windows.

## What it does

Opens a 640x480 window with a dark blue background and a white rectangle. Press Escape or close the window to quit.

## Building

### Dev-C++ (Windows 98)

Open `SDLFun.dev` in Dev-C++ and press F9. The vendored SDL headers, libs, and DLL are included in the project.

### CMake - modern Linux/Windows (system SDL)

```
mkdir build && cd build
cmake ..
make
```

Requires `libsdl1.2-dev` (Debian/Ubuntu) or equivalent installed.

### CMake - using vendored SDL

```
mkdir build && cd build
cmake -DUSE_VENDOR_SDL=ON ..
make
```

### Makefile (Linux)

```
make
```

Requires `sdl-config` to be available (installed with `libsdl1.2-dev`).

## Project structure

```
SDLFun/
  main.cpp          - source code
  SDLFun.dev        - Dev-C++ project file
  Makefile           - Linux makefile
  CMakeLists.txt     - CMake build file
  SDL.dll            - SDL runtime DLL for Windows
  vendor/
    include/SDL/     - SDL 1.2.15 headers
    lib/             - SDL 1.2.15 MinGW libraries
```
