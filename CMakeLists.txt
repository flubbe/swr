#
# author: Felix Lubbe
#

cmake_minimum_required(VERSION 3.22.0)

project(swr LANGUAGES CXX)
set(SWR_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")

option(SWR_BUILD_DEMOS "Build swr demos" ${PROJECT_IS_TOP_LEVEL})
option(SWR_BUILD_APP "Build swr app" ${PROJECT_IS_TOP_LEVEL})
option(SWR_BUILD_BENCHMARKS "Build swr benchmarks" ${PROJECT_IS_TOP_LEVEL})
option(SWR_BUILD_TESTS "Build swr tests" ${PROJECT_IS_TOP_LEVEL})
option(SWR_ENABLE_X86_SIMD_FLAGS "Enable x86 SSE compiler flags." ON)
option(SWR_ENABLE_PIPELINE_PROFILING "Enable rdtsc-based pipeline profiling counters in swrast (SWR_ENABLE_PIPELINE_PROFILING)" OFF)
option(SWR_ENABLE_DEV_BENCH_TESTS "Enable development performance benchmark tests in CTest." OFF)
option(SWR_ENABLE_MULTI_THREADING "Enable multi-threaded processing in rasterizer." ON)
option(SWR_USE_MORTON_CODES "Use Morton codes for texture access." ON)
set(SWR_TILE_CACHE_PRIMITIVE_CAPACITY "1024" CACHE STRING "Max primitive entries per rasterizer tile cache bucket")
set(SWR_RASTERIZER_BLOCK_SHIFT "5" CACHE STRING "Rasterizer tile block shift (4=16px, 5=32px, 6=64px)")
set(SWR_RASTERIZER_THIN_MINOR_AXIS_LIMIT "6" CACHE STRING "Thin-triangle minor-axis threshold in pixels")
set(SWR_RASTERIZER_SPARSE_PAYLOAD_TIGHT_MINOR_SPAN "2" CACHE STRING "Max tight minor-axis pixel span for sparse thin-triangle payloads")
set(SWR_RASTERIZER_MIN_SPARSE_PAYLOAD_QUAD_COUNT "4" CACHE STRING "Minimum 2x2-quad count before sparse thin-triangle payloads are used")

if(NOT SWR_TILE_CACHE_PRIMITIVE_CAPACITY MATCHES "^[1-9][0-9]*$")
    message(FATAL_ERROR "SWR_TILE_CACHE_PRIMITIVE_CAPACITY must be a positive integer, got '${SWR_TILE_CACHE_PRIMITIVE_CAPACITY}'.")
endif()

if(NOT SWR_RASTERIZER_BLOCK_SHIFT MATCHES "^[0-9]+$")
    message(FATAL_ERROR "SWR_RASTERIZER_BLOCK_SHIFT must be a non-negative integer, got '${SWR_RASTERIZER_BLOCK_SHIFT}'.")
endif()

if(NOT SWR_RASTERIZER_THIN_MINOR_AXIS_LIMIT MATCHES "^[0-9]+$")
    message(FATAL_ERROR "SWR_RASTERIZER_THIN_MINOR_AXIS_LIMIT must be a non-negative integer, got '${SWR_RASTERIZER_THIN_MINOR_AXIS_LIMIT}'.")
endif()

if(NOT SWR_RASTERIZER_SPARSE_PAYLOAD_TIGHT_MINOR_SPAN MATCHES "^[0-9]+$")
    message(FATAL_ERROR "SWR_RASTERIZER_SPARSE_PAYLOAD_TIGHT_MINOR_SPAN must be a non-negative integer, got '${SWR_RASTERIZER_SPARSE_PAYLOAD_TIGHT_MINOR_SPAN}'.")
endif()

if(NOT SWR_RASTERIZER_MIN_SPARSE_PAYLOAD_QUAD_COUNT MATCHES "^[1-9][0-9]*$")
    message(FATAL_ERROR "SWR_RASTERIZER_MIN_SPARSE_PAYLOAD_QUAD_COUNT must be a positive integer, got '${SWR_RASTERIZER_MIN_SPARSE_PAYLOAD_QUAD_COUNT}'.")
endif()

#
# Log configurable options.
#

function(print_option name)
    string(LENGTH "${name}" len)
    math(EXPR pad "56 - ${len}")
    if(pad LESS 1)
        set(pad 1)
    endif()
    string(REPEAT " " ${pad} spaces)
    message(STATUS "${name}${spaces}= ${${name}}")
endfunction()

message(STATUS "==== SWR Configuration ====")

print_option(SWR_BUILD_DEMOS)
print_option(SWR_BUILD_APP)
print_option(SWR_BUILD_BENCHMARKS)
print_option(SWR_BUILD_TESTS)
print_option(SWR_ENABLE_X86_SIMD_FLAGS)
print_option(SWR_ENABLE_PIPELINE_PROFILING)
print_option(SWR_ENABLE_DEV_BENCH_TESTS)
print_option(SWR_ENABLE_MULTI_THREADING)
print_option(SWR_USE_MORTON_CODES)
print_option(SWR_TILE_CACHE_PRIMITIVE_CAPACITY)
print_option(SWR_RASTERIZER_BLOCK_SHIFT)
print_option(SWR_RASTERIZER_THIN_MINOR_AXIS_LIMIT)
print_option(SWR_RASTERIZER_SPARSE_PAYLOAD_TIGHT_MINOR_SPAN)
print_option(SWR_RASTERIZER_MIN_SPARSE_PAYLOAD_QUAD_COUNT)

message(STATUS "============================")

#
# Set up testing.
#

if(PROJECT_IS_TOP_LEVEL)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${SWR_SOURCE_DIR}/bin")
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${SWR_SOURCE_DIR}/bin")

    enable_testing()
endif()

# Set a default build type only for standalone single-config generators.
if(PROJECT_IS_TOP_LEVEL AND NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(default_build_type Release)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/.git")
        set(default_build_type Debug)
    endif()

    message(STATUS "Setting build type to '${default_build_type}' as none was specified.")
    set(CMAKE_BUILD_TYPE "${default_build_type}" CACHE STRING "Choose the type of build.")
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release MinSizeRel RelWithDebInfo)
endif()

include(FetchContent)

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

#
# Dependencies.
#

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
target_include_directories(libmorton SYSTEM INTERFACE
    "${libmorton_src_SOURCE_DIR}/include"
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
    target_include_directories(stb SYSTEM INTERFACE
        "${stb_src_SOURCE_DIR}"
    )

    FetchContent_Declare(
        tinyobjloader_src
        GIT_REPOSITORY https://github.com/tinyobjloader/tinyobjloader.git
        GIT_TAG 966edce
        SOURCE_SUBDIR __unused__
    )
    FetchContent_MakeAvailable(tinyobjloader_src)

    add_library(tinyobjloader INTERFACE)
    target_include_directories(tinyobjloader SYSTEM INTERFACE
        "${tinyobjloader_src_SOURCE_DIR}"
    )
endif()

FetchContent_Declare(
    ml_src
    GIT_REPOSITORY https://github.com/flubbe/ml.git
    GIT_TAG 39311ce
)
FetchContent_MakeAvailable(ml_src)

FetchContent_Declare(
    concurrency_utils_src
    GIT_REPOSITORY https://github.com/flubbe/concurrency_utils.git
    GIT_TAG 825ba86
)
FetchContent_MakeAvailable(concurrency_utils_src)

set(SWR_SDL_PROVIDER "AUTO" CACHE STRING "How to provide SDL3: AUTO|SYSTEM|FETCH")
set_property(CACHE SWR_SDL_PROVIDER PROPERTY STRINGS AUTO SYSTEM FETCH)

function(swr_provide_sdl3)
    if(TARGET SDL3::SDL3)
        message(STATUS "Using already-available SDL3::SDL3 target.")
        return()
    endif()

    if(SWR_SDL_PROVIDER STREQUAL "SYSTEM")
        find_package(SDL3 REQUIRED CONFIG)
        if(NOT TARGET SDL3::SDL3)
            message(FATAL_ERROR "System SDL3 package found, but target SDL3::SDL3 is missing.")
        endif()
        return()
    endif()

    if(SWR_SDL_PROVIDER STREQUAL "FETCH")
        FetchContent_Declare(
            SDL3_src
            GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
            GIT_TAG release-3.4.4
        )
        FetchContent_MakeAvailable(SDL3_src)
        if(NOT TARGET SDL3::SDL3)
            message(FATAL_ERROR "Fetched SDL3, but target SDL3::SDL3 is missing.")
        endif()
        return()
    endif()

    if(NOT SWR_SDL_PROVIDER STREQUAL "AUTO")
        message(FATAL_ERROR "Invalid SWR_SDL_PROVIDER='${SWR_SDL_PROVIDER}'. Use AUTO|SYSTEM|FETCH.")
    endif()

    find_package(SDL3 QUIET CONFIG)
    if(TARGET SDL3::SDL3)
        message(STATUS "Using system SDL3 via find_package().")
        return()
    endif()

    message(STATUS "SDL3 not found as a usable package; fetching SDL3 with FetchContent.")
    FetchContent_Declare(
        SDL3_src
        GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
        GIT_TAG release-3.4.4
    )
    FetchContent_MakeAvailable(SDL3_src)

    if(NOT TARGET SDL3::SDL3)
        message(FATAL_ERROR "Fetched SDL3, but target SDL3::SDL3 is still missing.")
    endif()
endfunction()

swr_provide_sdl3()

#
# Project-wide target-based defaults.
#

add_library(swr_project_options INTERFACE)
add_library(swr_project_warnings INTERFACE)
add_library(swr_internal_config INTERFACE)

target_compile_features(swr_project_options INTERFACE cxx_std_23)

set_property(TARGET swr_project_options PROPERTY
    INTERFACE_POSITION_INDEPENDENT_CODE ON
)

target_compile_definitions(swr_internal_config INTERFACE
    SWR_TILE_CACHE_PRIMITIVE_CAPACITY=${SWR_TILE_CACHE_PRIMITIVE_CAPACITY}
    SWR_RASTERIZER_BLOCK_SHIFT=${SWR_RASTERIZER_BLOCK_SHIFT}
    SWR_RASTERIZER_THIN_MINOR_AXIS_LIMIT=${SWR_RASTERIZER_THIN_MINOR_AXIS_LIMIT}
    SWR_RASTERIZER_SPARSE_PAYLOAD_TIGHT_MINOR_SPAN=${SWR_RASTERIZER_SPARSE_PAYLOAD_TIGHT_MINOR_SPAN}
    SWR_RASTERIZER_MIN_SPARSE_PAYLOAD_QUAD_COUNT=${SWR_RASTERIZER_MIN_SPARSE_PAYLOAD_QUAD_COUNT}
)

if(SWR_ENABLE_PIPELINE_PROFILING)
    target_compile_definitions(swr_internal_config INTERFACE SWR_ENABLE_PIPELINE_PROFILING)
endif()

if(SWR_ENABLE_MULTI_THREADING)
    target_compile_definitions(swr_internal_config INTERFACE SWR_ENABLE_MULTI_THREADING)
endif()

if(SWR_USE_MORTON_CODES)
    target_compile_definitions(swr_internal_config INTERFACE SWR_USE_MORTON_CODES)
endif()

#
# Warnings.
#

target_compile_options(swr_project_warnings INTERFACE
    $<$<CXX_COMPILER_ID:GNU,Clang>:
    -Wall
    -Wextra
    -Wdouble-promotion
    -Wchar-subscripts
    -Wnull-dereference
    -Wmisleading-indentation
    -Wuninitialized
    >
)

#
# Optimization / debug flags.
#

target_compile_options(swr_project_options INTERFACE
    # GNU Debug
    $<$<AND:$<CONFIG:Debug>,$<CXX_COMPILER_ID:GNU>>:
    -Og
    >

    # GNU non-Debug
    $<$<AND:$<NOT:$<CONFIG:Debug>>,$<CXX_COMPILER_ID:GNU>>:
    -O3
    -fno-omit-frame-pointer
    >

    # Clang non-Debug
    $<$<AND:$<NOT:$<CONFIG:Debug>>,$<CXX_COMPILER_ID:Clang>>:
    -O3
    >

    # Other compilers, non-Debug
    $<$<AND:$<NOT:$<CONFIG:Debug>>,$<NOT:$<CXX_COMPILER_ID:GNU,Clang>>>:
    -O3
    >
)

if(SWR_ENABLE_X86_SIMD_FLAGS)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64)$")
        target_compile_options(swr_project_options INTERFACE
            $<$<CXX_COMPILER_ID:GNU,Clang>:
            -msse
            -msse2
            -msse3
            -msse4.1
            -msse4.2
            >

            $<$<CXX_COMPILER_ID:GNU>:
            -mfpmath=sse
            >
        )
    endif()
endif()

#
# Add subdirectories.
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
