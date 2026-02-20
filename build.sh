#!/bin/sh
set -e

wayland-scanner private-code xdg-shell.xml xdg-shell.c
wayland-scanner client-header xdg-shell.xml xdg-shell.h

wayland-scanner private-code wlr-layer-shell-unstable-v1.xml wlr-layer-shell-unstable-v1.c
wayland-scanner client-header wlr-layer-shell-unstable-v1.xml wlr-layer-shell-unstable-v1.h

cc xdg-shell.c -c
cc wlr-layer-shell-unstable-v1.c -c
c++ -Wall -Wextra -I./glad/include/ main.cc xdg-shell.o wlr-layer-shell-unstable-v1.o -std=c++23 `pkg-config --libs --cflags wayland-client` -lEGL -lwayland-egl -lGL -ggdb -lgfx `pkg-config --cflags --libs freetype2`
