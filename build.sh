#!/bin/bash
if [ -n "$BUILD_DEBUG" ]; then
    CFLAGS="-g -O0"
else
    CFLAGS="-O2"
fi
./opgen.py
g++ toyconsole.c -o toyconsole $(sdl2-config --cflags --libs) $CFLAGS