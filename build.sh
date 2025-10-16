#!/usr/bin/env sh

set -eu  # exit on error or unset var

: "${CC:=cc}"  # allow CC to be overridden
: "${CFLAGS:=}" # allow custom flags

mkdir -p ./bin 

"$CC" $CFLAGS ./src/main.c -o ./bin/name

# it should work with whatever compiler you want but if it doesnt i used clang
