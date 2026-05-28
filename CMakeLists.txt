cmake_minimum_required(VERSION 2.8)
project(SDLFun)

# Shared engine (2D / audio / scripting) lives at ../SOOB-Core/.
# Bullet stays here -- 3D-only, FPS-demo-specific.
set(ENGINE ${CMAKE_SOURCE_DIR}/../SOOB-Core)

option(USE_VENDOR_SDL "Use vendored SDL from the shared engine" OFF)

if(USE_VENDOR_SDL)
    set(SDL_INCLUDE_DIR ${ENGINE}/vendor/include)
    set(SDL_LIBRARY ${ENGINE}/vendor/lib/libSDL.dll.a)
    set(SDLMAIN_LIBRARY ${ENGINE}/vendor/lib/libSDLmain.a)
    set(SDL_LIBS mingw32 ${SDLMAIN_LIBRARY} ${SDL_LIBRARY})
else()
    find_package(SDL REQUIRED)
    set(SDL_LIBS ${SDL_LIBRARY})
endif()

find_package(OpenGL REQUIRED)
find_package(OpenAL REQUIRED)

include_directories(${ENGINE} ${SDL_INCLUDE_DIR} ${OPENAL_INCLUDE_DIR}
                    ${ENGINE}/vendor/lua-5.1.5/src
                    vendor/bullet3-3.25/src)

add_executable(SDLFun main.cpp ${ENGINE}/vendor/stb/stb_vorbis.c)
target_link_libraries(SDLFun ${SDL_LIBS} ${OPENGL_LIBRARIES} ${OPENAL_LIBRARY})
