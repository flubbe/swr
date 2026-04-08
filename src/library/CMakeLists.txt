project (libswraster)

#
# Boost.
#
find_package(Boost CONFIG REQUIRED)

#
# Set up project include directories
#
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR} 
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include)

#
# Add files
#
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
target_link_libraries(swrast SDL3::SDL3 Boost::headers)