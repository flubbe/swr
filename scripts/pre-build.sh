#!/bin/bash

#
# Dependencies.
#

#
# --- 3rd party ---
#

# Create folder structure
mkdir -p deps/3rd-party
cd deps/3rd-party

# Download fmt 7.1.3 and put it in deps/3rd-party/fmt
wget https://github.com/fmtlib/fmt/archive/refs/tags/7.1.3.tar.gz
tar -xf 7.1.3.tar.gz
rm 7.1.3.tar.gz
mv fmt-7.1.3 fmt

# Download cpu_features
wget https://github.com/google/cpu_features/archive/refs/tags/v0.6.0.tar.gz
tar -xf v0.6.0.tar.gz
rm v0.6.0.tar.gz
mv cpu_features-0.6.0 cpu_features

# Download libmorton
git clone https://github.com/Forceflow/libmorton.git

# Download lodepng
git clone https://github.com/lvandeve/lodepng.git

#
# --- 1st party ---
#
cd ..

# concurrency utility library
git clone https://github.com/flubbe/concurrency_utils.git

# mathematics library
git clone https://github.com/flubbe/ml.git
cd ml
chmod +x ./scripts/*
./scripts/pre-build.sh
cd ..

#
# change directory to project root.
#
cd ..

#
# Set up folder structure for build.
#

mkdir bin
mkdir build