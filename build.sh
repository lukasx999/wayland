#!/bin/sh
set -euxo pipefail

wayland-scanner private-code \
< /usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml \
> xdg-shell-protocol.c

wayland-scanner client-header \
< /usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml \
> xdg-shell-client-protocol.h

cc main.c xdg-shell-protocol.c \
-Wno-attributes                \
-o out                         \
-std=c11                       \
-Wall -Wextra -pedantic -ggdb  \
-lwayland-client -lrt
