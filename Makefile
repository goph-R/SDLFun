# Makefile for Linux - FPS Demo
# Requires libsdl1.2-dev, libopenal-dev.
CPP = g++
BIN = sdlfun

BULLET_SRC = vendor/bullet3-3.25/src
BULLET_CXXFLAGS = -I$(BULLET_SRC)
BULLET_OBJS = bullet_linear_math.o bullet_collision.o bullet_dynamics.o

CXXFLAGS = $(shell sdl-config --cflags) $(BULLET_CXXFLAGS) -O2
LIBS = $(shell sdl-config --libs) -lGL -lopenal

OBJ = main.o $(BULLET_OBJS)

all: $(BIN)

$(BIN): $(OBJ)
	$(CPP) $(OBJ) -o $(BIN) $(LIBS)

main.o: main.cpp obj_loader.h physics.h sound.h ui.h
	$(CPP) -c main.cpp -o main.o $(CXXFLAGS)

bullet_linear_math.o: $(BULLET_SRC)/btLinearMathAll.cpp
	$(CPP) -c $< -o $@ $(BULLET_CXXFLAGS) -O2

bullet_collision.o: $(BULLET_SRC)/btBulletCollisionAll.cpp
	$(CPP) -c $< -o $@ $(BULLET_CXXFLAGS) -O2

bullet_dynamics.o: $(BULLET_SRC)/btBulletDynamicsAll.cpp
	$(CPP) -c $< -o $@ $(BULLET_CXXFLAGS) -O2

clean:
	rm -f $(OBJ) $(BIN)

.PHONY: all clean
