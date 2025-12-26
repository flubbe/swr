project (libswraster)

#
# Set up project include directories
#
include_directories(. ../../inc)

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
	statistics.cpp
	textures.cpp
)

add_library(swrast SHARED ${SOURCES})
target_link_libraries(swrast ${EXTRA_LIBS})
