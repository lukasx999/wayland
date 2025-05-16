#!/bin/sh
set -euxo pipefail

c++ main.cc \
-o out \
-std=c++23 \
-Wall -Wextra -pedantic -ggdb \
-lwayland-client
