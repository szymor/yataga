.PHONY: all clean

TARGET=yataga
SRC=$(wildcard *.c)
INC=$(wildcard *.h)

all: $(TARGET)

clean:
	-rm $(TARGET)

$(TARGET): $(SRC) $(INC)
	gcc -g -o $@ $(SRC) `pkg-config --cflags --libs sdl SDL_gfx` -Ichipmunk2d/include/chipmunk -Lchipmunk2d/build/src -l:libchipmunk.a -lm
