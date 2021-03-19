#
# authors: Felix Lubbe
#

cmake_policy(VERSION 3.6.0)
cmake_minimum_required (VERSION 3.6.0)

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

project(swr_root)

set(CMAKE_BUILD_TYPE Release)

#
# Set compiler specific options.
#
if("${CMAKE_CXX_COMPILER_ID}" MATCHES "Clang")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++17 -stdlib=libc++ -Wall -O3 -msse -msse2 -msse3 -msse4 -msse4.1 -msse4.2 -mfpmath=sse")
elseif ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++17 -Wall -O2 -msse -msse2 -msse3 -msse4 -msse4.1 -msse4.2 -mfpmath=sse")
endif()

#
# Set system specific options.
#
find_library(SDL_LIBRARY SDL2)
set(EXTRA_LIBS ${SDL_LIBRARY})

#
# Set up output directories
#
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/../bin )

#
# Set up project include directories
#
include_directories(
	./ 
	src 
	include
	deps/ml/include
	deps/3rd-party/lodepng 
	deps/3rd-party/cpu_features/include 
	deps/3rd-party/cnl/include
	deps/3rd-party/fmt/include
	)

#
# Add subdirectories
#
add_subdirectory(deps/3rd-party/cpu_features)
add_subdirectory(deps/3rd-party/fmt)
add_subdirectory(src/demos)
add_subdirectory(src/library)
add_subdirectory(src/swr_app)

