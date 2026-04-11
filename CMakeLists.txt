#
# authors: Felix Lubbe
#

cmake_policy(VERSION 3.22.0)
cmake_minimum_required(VERSION 3.22.0)

project(swr LANGUAGES CXX)
set(SWR_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")

option(SWR_BUILD_DEMOS "Build swr demos" ${PROJECT_IS_TOP_LEVEL})
option(SWR_BUILD_APP "Build swr app" ${PROJECT_IS_TOP_LEVEL})
option(SWR_BUILD_BENCHMARKS "Build swr benchmarks" ${PROJECT_IS_TOP_LEVEL})
option(SWR_BUILD_TESTS "Build swr tests" ${PROJECT_IS_TOP_LEVEL})

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
    GIT_TAG 5bb7eb5
)
FetchContent_MakeAvailable(ml_src)

FetchContent_Declare(
    concurrency_utils_src
    GIT_REPOSITORY https://github.com/flubbe/concurrency_utils.git
    GIT_TAG 825ba86
)
FetchContent_MakeAvailable(concurrency_utils_src)

#
# Project-wide target-based defaults.
#

add_library(swr_project_options INTERFACE)
add_library(swr_project_warnings INTERFACE)

target_compile_features(swr_project_options INTERFACE cxx_std_23)

# Position independent code for swr targets only.
set_property(TARGET swr_project_options PROPERTY INTERFACE_POSITION_INDEPENDENT_CODE ON)

target_compile_options(swr_project_warnings INTERFACE
    -Wall
    -Wextra
    -Wdouble-promotion
    -Wchar-subscripts
    -Wnull-dereference
    -Wmisleading-indentation
    -Wuninitialized
)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        target_compile_options(swr_project_options INTERFACE
            -stdlib=libc++
        )
    elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        target_compile_options(swr_project_options INTERFACE
            -Og
        )
    endif()
else()
    if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        target_compile_options(swr_project_options INTERFACE
            -O3
            -stdlib=libc++
        )
    elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        target_compile_options(swr_project_options INTERFACE
            -O3
            -fno-omit-frame-pointer
        )
    else()
        target_compile_options(swr_project_options INTERFACE
            -O3
        )
    endif()
endif()

if(CMAKE_SYSTEM_PROCESSOR STREQUAL "arm64" OR CMAKE_SYSTEM_PROCESSOR STREQUAL "aarch64")
    message(STATUS "Detected ARM64 architecture")
elseif(CMAKE_SYSTEM_PROCESSOR STREQUAL "x86_64" OR CMAKE_SYSTEM_PROCESSOR STREQUAL "AMD64")
    message(STATUS "Detected x86_64 architecture")
    target_compile_options(swr_project_options INTERFACE
        -msse
        -msse2
        -msse3
        -msse4
        -msse4.1
        -msse4.2
        -mfpmath=sse
    )
else()
    message(WARNING "Unknown architecture: ${CMAKE_SYSTEM_PROCESSOR}")
endif()

#
# Optional dependencies only when needed.
#

if(SWR_BUILD_DEMOS OR SWR_BUILD_APP)
    find_package(SDL3 REQUIRED CONFIG COMPONENTS SDL3)
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
