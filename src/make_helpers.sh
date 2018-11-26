#!/bin/sh

echo "Making fasty_helpers..."

g++ fasty_helpers.cpp \
../library/fasty_engine.c ../library/fasty_core.c \
-s -O3 \
-D ASYNC_HELPER \
-I. -I../lib \
-lrt \
-o ../bin/fasty_helpers
