#/bin/bash

#
# Dependencies.
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

# Download lodepng
git clone https://github.com/lvandeve/lodepng.git

# mathematics library
cd ..
git clone https://github.com/flubbe/ml.git
cd ml
chmod +x ./scripts/*
./scripts/pre-build.sh

cd ../..

#
# Set up folder structure for build.
#

mkdir bin
mkdir build