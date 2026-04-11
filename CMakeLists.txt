#
# authors: Felix Lubbe
#

cmake_policy(VERSION 3.22.0)
cmake_minimum_required(VERSION 3.22.0)

set(SWR_IS_TOPLEVEL OFF)
if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    set(SWR_IS_TOPLEVEL ON)
endif()

option(SWR_BUILD_DEMOS "Build swr demos" ${SWR_IS_TOPLEVEL})
option(SWR_BUILD_APP "Build swr app" ${SWR_IS_TOPLEVEL})
option(SWR_BUILD_BENCHMARKS "Build swr benchmarks" ${SWR_IS_TOPLEVEL})
option(SWR_BUILD_TESTS "Build swr tests" ${SWR_IS_TOPLEVEL})

#
# Get dependencies.
#

include(FetchContent)

FetchContent_Declare(
    cpu_features_src
    GIT_REPOSITORY https://github.com/google/cpu_features.git
    GIT_TAG v0.10.1
)
FetchContent_MakeAvailable(cpu_features_src)

FetchContent_Declare(
    libmorton_src
    GIT_REPOSITORY https://github.com/Forceflow/libmorton.git
    GIT_TAG v0.2.12
    SOURCE_SUBDIR __unused__
)
FetchContent_MakeAvailable(libmorton_src)

add_library(libmorton INTERFACE)
target_include_directories(libmorton INTERFACE
    ${libmorton_src_SOURCE_DIR}/include
)

if(SWR_BUILD_DEMOS)
    FetchContent_Declare(
        simdjson_src
        GIT_REPOSITORY https://github.com/simdjson/simdjson.git
        GIT_TAG v4.6.1
    )
    FetchContent_MakeAvailable(simdjson_src)

    FetchContent_Declare(
        stb_src
        GIT_REPOSITORY https://github.com/nothings/stb.git
        GIT_TAG 28d546d
        SOURCE_SUBDIR __unused__
    )
    FetchContent_MakeAvailable(stb_src)

    add_library(stb INTERFACE)
    target_include_directories(stb INTERFACE
        ${stb_src_SOURCE_DIR}
    )

    FetchContent_Declare(
        tinyobjloader_src
        GIT_REPOSITORY https://github.com/tinyobjloader/tinyobjloader.git
        GIT_TAG 966edce
        SOURCE_SUBDIR __unused__
    )
    FetchContent_MakeAvailable(tinyobjloader_src)

    add_library(tinyobjloader INTERFACE)
    target_include_directories(tinyobjloader INTERFACE
        ${tinyobjloader_src_SOURCE_DIR}
    )
endif()

FetchContent_Declare(
    ml_src
    GIT_REPOSITORY https://github.com/flubbe/ml.git
    GIT_TAG 4bf382d
)
FetchContent_MakeAvailable(ml_src)

FetchContent_Declare(
    concurrency_utils_src
    GIT_REPOSITORY https://github.com/flubbe/concurrency_utils.git
    GIT_TAG 825ba86
)
FetchContent_MakeAvailable(concurrency_utils_src)

enable_testing()

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

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Test if linux
if(UNIX AND NOT APPLE)
    set(LINUX TRUE)
endif()

#
# Set compiler specific options.
#
add_compile_options(-Wall -Wextra -Wdouble-promotion -Wchar-subscripts -Wnull-dereference -Wmisleading-indentation -Wuninitialized)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
        add_compile_options(-stdlib=libc++)
    elseif("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
        add_compile_options(-Og)
    endif()
else()
    add_compile_options(-O3)
    if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
        add_compile_options(-stdlib=libc++)
    elseif("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
        add_compile_options(-fno-omit-frame-pointer)
    endif()
endif()

if(CMAKE_SYSTEM_PROCESSOR STREQUAL "arm64")
    message(STATUS "Detected ARM64 architecture")
elseif(CMAKE_SYSTEM_PROCESSOR STREQUAL "x86_64")
    message(STATUS "Detected x86_64 architecture")
    add_compile_options(-msse -msse2 -msse3 -msse4 -msse4.1 -msse4.2 -mfpmath=sse)
else()
    message(WARNING "Unknown architecture: ${CMAKE_SYSTEM_PROCESSOR}")
endif()

#
# SDL3.
#

find_package(SDL3 REQUIRED CONFIG REQUIRED COMPONENTS SDL3)
set(EXTRA_LIBS SDL3::SDL3)
message(STATUS ${EXTRA_LIBS})

#
# Set up output directories
#
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/bin)

#
# Set up project include directories
#
include_directories(
    include
)

#
# Add subdirectories
#

if(SWR_BUILD_DEMOS)
    add_subdirectory(src/demos)
endif()

if(SWR_BUILD_APP)
    add_subdirectory(src/swr_app)
endif()

if(SWR_BUILD_BENCHMARKS)
    add_subdirectory(src/bench)
endif()

if(SWR_BUILD_TESTS)
    add_subdirectory(src/test)
endif()

add_subdirectory(src/library)
