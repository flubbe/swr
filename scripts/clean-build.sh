#!/bin/bash

#
# Delete build directory
#
if [ -d "build" ]; then rm -Rf "build"; fi

#
# Delete binary directory
#
if [ -d "bin" ]; then rm -Rf "bin"; fi
