#!/bin/sh
set -euxo pipefail

wayland-scanner private-code \
< /usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml \
> xdg-shell-protocol.c

wayland-scanner client-header \
< /usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml \
> xdg-shell-client-protocol.h

cc xdg-shell-protocol.c -c

c++ main.cc xdg-shell-protocol.o \
-Wno-attributes \
-o out \
-std=c++23 \
-Wall -Wextra -pedantic -ggdb \
-lwayland-client -lrt
