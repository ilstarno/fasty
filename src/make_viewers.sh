#!/bin/sh

echo "Making fasty_viewer..."

gcc ../library/fasty_viewer.c \
../library/fasty_core.c \
-s -O3 \
-D fasty_viewer \
-I. -I../library \
-o ../bin/fasty_viewer
