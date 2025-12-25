#!/bin/bash

#
# Delete dependencies.
#

if [ -f "./deps/ml/scripts/clean-deps.sh" ]; 
then 
    cd ./deps/ml
    ./scripts/clean-deps.sh
    cd ../..
fi
if [ -d "./deps/ml" ]; then rm -Rf "./deps/ml"; fi 
if [ -d "./deps/concurrency_utils" ]; then rm -Rf "./deps/concurrency_utils"; fi 

if [ -d "./deps/3rd-party/cpu_features" ]; then rm -Rf "./deps/3rd-party/cpu_features"; fi
if [ -d "./deps/3rd-party/libmorton" ]; then rm -Rf "./deps/3rd-party/libmorton"; fi
if [ -d "./deps/3rd-party/lodepng" ]; then rm -Rf "./deps/3rd-party/lodepng"; fi
if [ -d "./deps/3rd-party/simdjson" ]; then rm -Rf "./deps/3rd-party/simdjson"; fi
if [ -d "./deps/3rd-party/stb" ]; then rm -Rf "./deps/3rd-party/stb"; fi
if [ -d "./deps/3rd-party/tinyobjloader" ]; then rm -Rf "./deps/3rd-party/tinyobjloader"; fi

#
# Do not delete directory (it still contains README.md and 3rd-party/README.md).
#
