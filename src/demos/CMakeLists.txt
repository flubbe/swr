project (swdemo)

#
# Set up project include directories
#
include_directories(
	../../include
	../../src/library
	../../deps/3rd-party/stb
	../../deps/3rd-party/tinyobjloader
)

#
# Add files
#
set(COMMON_SOURCES
	../common/platform/cpu.cpp
	../common/platform/log.cpp
	../common/platform/platform.cpp
)

add_executable(demo_alpha_blend
	${COMMON_SOURCES}
    common/stb.cpp
	alpha_blend/main.cpp
)
target_link_libraries(demo_alpha_blend swrast swr_app ${EXTRA_LIBS})

add_executable(demo_bitmap_font
	${COMMON_SOURCES}
	common/font.cpp
    common/stb.cpp
	bitmap_font/main.cpp
)
target_link_libraries(demo_bitmap_font swrast swr_app ${EXTRA_LIBS})

add_executable(demo_blinn_phong
	${COMMON_SOURCES}
    common/stb.cpp
	blinn_phong/main.cpp
)
target_link_libraries(demo_blinn_phong swrast swr_app ${EXTRA_LIBS})

add_executable(demo_clear
	${COMMON_SOURCES}
	clear/main.cpp
)
target_link_libraries(demo_clear swrast swr_app ${EXTRA_LIBS})

add_executable(demo_color
	${COMMON_SOURCES}
	color/main.cpp
)
target_link_libraries(demo_color swrast swr_app ${EXTRA_LIBS})

add_executable(demo_emitter
	${COMMON_SOURCES}
    common/stb.cpp
	emitter/main.cpp
)
target_link_libraries(demo_emitter swrast swr_app ${EXTRA_LIBS})

add_executable(demo_fill_test
	${COMMON_SOURCES}
	common/mesh.cpp
	fill_test/main.cpp
)
target_link_libraries(demo_fill_test swrast swr_app ${EXTRA_LIBS})

add_executable(demo_gears
	${COMMON_SOURCES}
	gears/main.cpp
)
target_link_libraries(demo_gears swrast swr_app ${EXTRA_LIBS})

add_executable(demo_motion_blur
	${COMMON_SOURCES}
	common/mesh.cpp
    common/stb.cpp
	motion_blur/main.cpp
)
target_link_libraries(demo_motion_blur swrast swr_app ${EXTRA_LIBS})

add_executable(demo_normal_map
	${COMMON_SOURCES}
    common/stb.cpp
	normal_map/main.cpp
)
target_link_libraries(demo_normal_map swrast swr_app ${EXTRA_LIBS})

add_executable(demo_obj_viewer
	${COMMON_SOURCES}
	obj_viewer/main.cpp
)
target_link_libraries(demo_obj_viewer swrast swr_app ${EXTRA_LIBS})

add_executable(demo_phong
	${COMMON_SOURCES}
    common/stb.cpp
	phong/main.cpp
)
target_link_libraries(demo_phong swrast swr_app ${EXTRA_LIBS})

add_executable(demo_scissor
	${COMMON_SOURCES}
    common/stb.cpp
	scissor/main.cpp
)
target_link_libraries(demo_scissor swrast swr_app ${EXTRA_LIBS})

add_executable(demo_texture
	${COMMON_SOURCES}
    common/stb.cpp
	textures/main.cpp
)
target_link_libraries(demo_texture swrast swr_app ${EXTRA_LIBS})

add_executable(demo_timings
	${COMMON_SOURCES}
	common/font.cpp
    common/stb.cpp
	timings/main.cpp
	timings/main.cpp
)
target_link_libraries(demo_timings swrast swr_app ${EXTRA_LIBS})

add_subdirectory(cars)
