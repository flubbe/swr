set(SWR_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)
set(DEPS_ROOT ${SWR_ROOT}/deps)

find_package(Boost CONFIG REQUIRED)

set(SOURCES
    rasterizer/fragment.cpp
    rasterizer/line.cpp
    rasterizer/point.cpp
    rasterizer/sweep.cpp
    rasterizer/triangle.cpp
    assembly.cpp
    buffers.cpp
    context.cpp
    clipping.cpp
    draw.cpp
    misc.cpp
    output_merger.cpp
    pipeline.cpp
    renderbuffer.cpp
    renderobject.cpp
    shaders.cpp
    states.cpp
    textures.cpp
)

add_library(swrast SHARED ${SOURCES})

target_include_directories(swrast
    PUBLIC
    ${SWR_ROOT}/include
    PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(swrast
    PUBLIC
    ml
    SDL3::SDL3
    PRIVATE
    Boost::headers
    libmorton
    concurrency_utils
)
