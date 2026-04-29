# Makefile for Linux - FPS Demo
# Requires libsdl1.2-dev, libopenal-dev.
CPP = g++
BIN = sdlfun

BULLET_SRC = vendor/bullet3-3.25/src
BULLET_CXXFLAGS = -I$(BULLET_SRC)
BULLET_OBJS = bullet_linear_math.o bullet_collision.o bullet_dynamics.o

LUA_SRC = vendor/lua-5.1.5/src
LUA_CFLAGS = -I$(LUA_SRC) -Dluaall_c -DLUA_USE_POSIX

CXXFLAGS = $(shell sdl-config --cflags) $(BULLET_CXXFLAGS) -I$(LUA_SRC) -O2
LIBS = $(shell sdl-config --libs) -lGL -lopenal

OBJ = main.o $(BULLET_OBJS) lua.o vorbis.o

all: $(BIN)

$(BIN): $(OBJ)
	$(CPP) $(OBJ) -o $(BIN) $(LIBS)

main.o: main.cpp obj_loader.h physics.h sound.h ui.h script.h
	$(CPP) -c main.cpp -o main.o $(CXXFLAGS)

# stb_vorbis (Ogg Vorbis decoder, public domain). Built as its own C TU
# so editing main.cpp doesn't pay its recompile cost. music.h includes
# the same file with STB_VORBIS_HEADER_ONLY for prototypes only.
vorbis.o: vendor/stb/stb_vorbis.c
	gcc -x c -c $< -o $@ -O2

bullet_linear_math.o: $(BULLET_SRC)/btLinearMathAll.cpp
	$(CPP) -c $< -o $@ $(BULLET_CXXFLAGS) -O2

bullet_collision.o: $(BULLET_SRC)/btBulletCollisionAll.cpp
	$(CPP) -c $< -o $@ $(BULLET_CXXFLAGS) -O2

bullet_dynamics.o: $(BULLET_SRC)/btBulletDynamicsAll.cpp
	$(CPP) -c $< -o $@ $(BULLET_CXXFLAGS) -O2

# Lua 5.1.5 compiled as a single C TU via the unity-build aggregator.
lua.o: $(LUA_SRC)/lua_all.c
	gcc -x c -c $< -o $@ $(LUA_CFLAGS) -O2

clean:
	rm -f $(OBJ) $(BIN)

.PHONY: all clean
