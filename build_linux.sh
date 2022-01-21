#!/bin/bash
cd "$(dirname "$0")"

BUILDMODE=${2:-"debug"}
OPT_FLAG="O2"
GF="uce_fastcgi"

if [ "$BUILDMODE" == "debug" ]; then
	OPT_FLAG="O0"
fi
echo "Build mode: $BUILDMODE"

mkdir bin > /dev/null 2>&1
mkdir bin/tmp > /dev/null 2>&1
mkdir bin/assets > /dev/null 2>&1
mkdir work > /dev/null 2>&1

COMPILER="clang++"
FLAGS="-g -rdynamic -w -Wall -$OPT_FLAG -std=c++17 -fpermissive -ffast-math"

LIBS="-ldl -lm -lpthread `mysql_config --cflags --libs`"
SRCFLAGS="-D EXEC_NAME=\"$GF\" -D PLATFORM_NAME=\"linux\""

echo "Compliling executable..."
time -p $COMPILER src/linux_fastcgi.cpp $SRCFLAGS $FLAGS $LIBS -o bin/$GF.$BUILDMODE.linux.bin 2>&1

if [ $? -eq 0 ]
then
	ls -lh bin/ | grep $GF
	exit 0
else
	exit 1
fi
