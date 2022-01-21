#!/bin/bash

rm -r /tmp/uce/work/* ; ./build_linux.sh && bin/uce_fastcgi.debug.linux.bin
