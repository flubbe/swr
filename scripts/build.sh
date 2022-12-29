#!/bin/bash
if [ ! -d "build" ]; then mkdir build; fi
if [ ! -d "bin" ]; then mkdir bin; fi

cd build
cmake .. -G Ninja
ninja 
