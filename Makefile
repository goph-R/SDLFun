# Makefile for Linux
CPP = g++
BIN = sdlfun
OBJ = main.o

CXXFLAGS = $(shell sdl-config --cflags)
LIBS = $(shell sdl-config --libs) -lGL

all: $(BIN)

$(BIN): $(OBJ)
	$(CPP) $(OBJ) -o $(BIN) $(LIBS)

main.o: main.cpp
	$(CPP) -c main.cpp -o main.o $(CXXFLAGS)

clean:
	rm -f $(OBJ) $(BIN)

.PHONY: all clean
