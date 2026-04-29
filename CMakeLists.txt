cmake_minimum_required(VERSION 2.8)
project(SDLFun)

option(USE_VENDOR_SDL "Use vendored SDL from vendor/ directory" OFF)

if(USE_VENDOR_SDL)
    set(SDL_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/vendor/include)
    set(SDL_LIBRARY ${CMAKE_SOURCE_DIR}/vendor/lib/libSDL.dll.a)
    set(SDLMAIN_LIBRARY ${CMAKE_SOURCE_DIR}/vendor/lib/libSDLmain.a)
    set(SDL_LIBS mingw32 ${SDLMAIN_LIBRARY} ${SDL_LIBRARY})
else()
    find_package(SDL REQUIRED)
    set(SDL_LIBS ${SDL_LIBRARY})
endif()

find_package(OpenGL REQUIRED)
find_package(OpenAL REQUIRED)

include_directories(${SDL_INCLUDE_DIR} ${OPENAL_INCLUDE_DIR})

add_executable(SDLFun main.cpp vendor/stb/stb_vorbis.c)
target_link_libraries(SDLFun ${SDL_LIBS} ${OPENGL_LIBRARIES} ${OPENAL_LIBRARY})
