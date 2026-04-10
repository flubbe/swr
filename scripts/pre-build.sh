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

# Download stb
git clone https://github.com/nothings/stb.git --depth=1

# Download tinyobjloader
git clone https://github.com/tinyobjloader/tinyobjloader.git --depth=1

# Download simdjson
mkdir simdjson
wget https://github.com/simdjson/simdjson/releases/download/v4.2.4/singleheader.zip
echo "b4fd52b7e60e881050893613367516c0ebdf4aa50f18abe1a50819adfe750d9a singleheader.zip" | sha256sum -c -
unzip singleheader.zip -d simdjson/singleheader
rm singleheader.zip

#
# change directory to project root.
#
cd ..

#
# Set up folder structure for build.
#

mkdir bin
mkdir build