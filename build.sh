#!/bin/sh
set -e

wayland-scanner private-code xdg-shell.xml xdg-shell.c
wayland-scanner client-header xdg-shell.xml xdg-shell.h

cc xdg-shell.c -c
c++ -Wall -Wextra -I./glad/include/ main.cc xdg-shell.o -std=c++23 `pkg-config --libs --cflags wayland-client` -lEGL -lwayland-egl -lGL -ggdb -lgfx `pkg-config --cflags --libs freetype2`
