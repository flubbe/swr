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
	rasterizer/line_st.cpp
	rasterizer/point_st.cpp
	rasterizer/sweep_st.cpp
	rasterizer/triangle_st.cpp
	assembly.cpp
	buffers.cpp
	context.cpp
	clipping.cpp
	culling.cpp
	draw.cpp
	immediate.cpp
	misc.cpp
	output_merger.cpp
	pipeline.cpp
	renderobject.cpp
	shaders.cpp
	states.cpp
	statistics.cpp
	textures.cpp
)

add_library(swrast SHARED ${SOURCES})
target_link_libraries(swrast fmt ${EXTRA_LIBS})
