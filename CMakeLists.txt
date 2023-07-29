#
# authors: Felix Lubbe
#

cmake_policy(VERSION 3.6.0)
cmake_minimum_required (VERSION 3.6.0)

project(swr)

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Set a default build type if none was specified
set(default_build_type "Release")
if(EXISTS "${CMAKE_SOURCE_DIR}/.git")
	set(default_build_type "Debug")
endif()

if(NOT CMAKE_BUILD_TYPE)
	message(STATUS "Setting build type to '${default_build_type}' as none was specified.")
	set(CMAKE_BUILD_TYPE "${default_build_type}" CACHE STRING "Choose the type of build." FORCE)
	# Set the possible values of build type for cmake-gui
	set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release" "MinSizeRel" "RelWithDebInfo")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Test if linux
if(UNIX AND NOT APPLE)
    set(LINUX TRUE)
endif()

#
# Set compiler specific options.
#
if(CMAKE_BUILD_TYPE MATCHES "Debug")
	if("${CMAKE_CXX_COMPILER_ID}" MATCHES "Clang")
		set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++17 -stdlib=libc++ -Wall -Wextra -Wdouble-promotion -Wchar-subscripts -Wnull-dereference -Wmisleading-indentation -Wuninitialized -msse -msse2 -msse3 -msse4 -msse4.1 -msse4.2 -mfpmath=sse")
	elseif ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
		set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++17 -Wall -Wextra -Wdouble-promotion -Wchar-subscripts -Wnull-dereference -Wmisleading-indentation -Wuninitialized -Og -msse -msse2 -msse3 -msse4 -msse4.1 -msse4.2 -mfpmath=sse")
	endif()
else()
	if("${CMAKE_CXX_COMPILER_ID}" MATCHES "Clang")
		set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++17 -stdlib=libc++ -Wall -Wextra -Wdouble-promotion -Wchar-subscripts -Wnull-dereference -Wmisleading-indentation -Wuninitialized -O3 -msse -msse2 -msse3 -msse4 -msse4.1 -msse4.2 -mfpmath=sse")
	elseif ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
		set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++17 -Wall -Wextra -Wdouble-promotion -Wchar-subscripts -Wnull-dereference -Wmisleading-indentation -Wuninitialized -O3 -msse -msse2 -msse3 -msse4 -msse4.1 -msse4.2 -mfpmath=sse -fno-omit-frame-pointer")
	endif()
endif()

#
# Set system specific options.
#
if(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
	include_directories(/Library/Frameworks/SDL2.framework/Headers /opt/local/include)
	find_library(SDL_LIBRARY SDL2)
	set(EXTRA_LIBS ${SDL_LIBRARY})
elseif(LINUX)
	find_library(SDL_LIBRARY SDL2)
	set(EXTRA_LIBS ${SDL_LIBRARY})
endif()

message(STATUS ${EXTRA_LIBS})

#
# Thread library.
#
set(CMAKE_THREAD_PREFER_PTHREAD ON)
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

#
# Set up output directories
#
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/../bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/../bin)

#
# Disable testing (for cpu_features integration)
#
set(BUILD_TESTING OFF)

#
# Set up project include directories
#
include_directories(
	./ 
	src 
	include
	deps/concurrency_utils/include
	deps/ml/include
	deps/3rd-party/lodepng 
	deps/3rd-party/cpu_features/include 
	deps/3rd-party/cnl/include
	deps/3rd-party/fmt/include
	deps/3rd-party/libmorton/include
	)

#
# Add subdirectories
#
add_subdirectory(deps/3rd-party/cpu_features)
add_subdirectory(deps/3rd-party/fmt)
add_subdirectory(src/demos)
add_subdirectory(src/library)
add_subdirectory(src/swr_app)

# benchmarks
add_subdirectory(src/bench)

# tests
add_subdirectory(src/test)
