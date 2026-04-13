cmake_minimum_required(VERSION 2.8)
project(SDLFun)

option(USE_VENDOR_SDL "Use vendored SDL from vendor/ directory" OFF)
option(USE_VENDOR_FMOD "Use vendored FMOD from vendor/ directory" OFF)

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

if(USE_VENDOR_FMOD)
    set(FMOD_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/vendor/include)
    set(FMOD_LIBRARY ${CMAKE_SOURCE_DIR}/vendor/lib/libfmod.a)
else()
    set(FMOD_INCLUDE_DIR "" CACHE PATH "Path to FMOD include directory")
    set(FMOD_LIBRARY "" CACHE FILEPATH "Path to FMOD library")
endif()

include_directories(${SDL_INCLUDE_DIR} ${FMOD_INCLUDE_DIR})

add_executable(SDLFun main.cpp)
target_link_libraries(SDLFun ${SDL_LIBS} ${OPENGL_LIBRARIES} ${FMOD_LIBRARY})
