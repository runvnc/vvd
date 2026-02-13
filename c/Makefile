CC = gcc
CFLAGS = -Wall -Wextra -O2 -std=c11
LDFLAGS = -lSDL2 -lSDL2_ttf -lm

.PHONY: all clean

all: vvd_demo

vvd_demo: demo.c vvd.h vvd_sdl.h
	$(CC) $(CFLAGS) -o $@ demo.c $(LDFLAGS)

clean:
	rm -f vvd_demo
