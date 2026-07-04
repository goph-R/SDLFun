#!/bin/sh
# ---------------------------------------------------------------------------
# Linux build for the SOOB Level Editor.
#
# Links FLTK 1.3 + Bullet + Lua + system GL only — NO SDL, OpenAL, or vorbis
# (the game.h / game_session.h split keeps the script/ui/audio runtime out of
# the editor TU; Lua is linked only for edit_assets.h's manifest loader).
# Needs libfltk1.3-dev (for fltk-config) and the Bullet + Lua objects the
# game's `make` already produces in the repo root.
# ---------------------------------------------------------------------------
set -e

ENGINE=../SOOB-Core
OBJDIR=raw/obj
mkdir -p "$OBJDIR"

if ! command -v fltk-config >/dev/null 2>&1; then
    echo "ERROR: fltk-config not found — install libfltk1.3-dev."; exit 1
fi
if [ ! -f bullet_linear_math.o ] || [ ! -f lua.o ]; then
    echo "ERROR: Bullet/Lua objects missing — run 'make' first to build them."; exit 1
fi

CXXFLAGS="$(fltk-config --use-gl --cxxflags) -I$ENGINE -Ivendor/bullet3-3.25/src -I$ENGINE/vendor/lua-5.1.5/src -O2"
LDFLAGS="$(fltk-config --use-gl --ldflags) -lGL -lm"

echo "Compiling editor.cpp..."
g++ -c editor.cpp -o "$OBJDIR/editor.o" $CXXFLAGS

echo "Linking soob_editor..."
g++ "$OBJDIR/editor.o" \
    bullet_linear_math.o bullet_collision.o bullet_dynamics.o lua.o \
    -o soob_editor $LDFLAGS

echo "=== Built ./soob_editor — run from the repo root ==="
