#!/bin/bash
cd "$(dirname "$0")"
cd ..

BUILDMODE=${2:-"debug"}
OPT_FLAG="O0"
GF="uce_fastcgi"

mkdir bin > /dev/null 2>&1
mkdir bin/tmp > /dev/null 2>&1
mkdir bin/assets > /dev/null 2>&1
mkdir work > /dev/null 2>&1

COMPILER="clang++"
FLAGS="-g -rdynamic -w -Wall -$OPT_FLAG -std=c++20 -fpermissive -ffast-math"

LIBS="-ldl -lm -lpthread -lpcre2-8 `mysql_config --cflags --libs`"
SRCFLAGS="-D EXEC_NAME=\"$GF\" -D PLATFORM_NAME=\"linux\""

echo "Compiling SQLite..."
clang -g -O2 -fPIC \
	-DSQLITE_THREADSAFE=1 \
	-DSQLITE_OMIT_LOAD_EXTENSION=1 \
	-DSQLITE_DQS=0 \
	-DSQLITE_DEFAULT_FOREIGN_KEYS=1 \
	-DSQLITE_DEFAULT_WAL_SYNCHRONOUS=1 \
	-c src/3rdparty/sqlite/sqlite3.c -o bin/sqlite3.o 2>&1
if [ $? -ne 0 ]; then exit 1; fi


echo "Compiling executable..."
time -p $COMPILER src/linux_fastcgi.cpp bin/sqlite3.o $SRCFLAGS $FLAGS $LIBS -o bin/$GF.linux.bin 2>&1

if [ $? -eq 0 ]
then
	ls -lh bin/ | grep $GF
	exit 0
else
	exit 1
fi
