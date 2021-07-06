project (swdemo)

#
# Set up project include directories
#
include_directories(../../include ../../src/library ../../deps/3rd-party/lodepng)

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
	../../deps/3rd-party/lodepng/lodepng.cpp
	alpha_blend/main.cpp
)
target_link_libraries(demo_alpha_blend swrast cpu_features fmt swr_app ${EXTRA_LIBS})

add_executable(demo_bitmap_font
	${COMMON_SOURCES}
	../../deps/3rd-party/lodepng/lodepng.cpp
	common/font.cpp
	bitmap_font/main.cpp
)
target_link_libraries(demo_bitmap_font swrast cpu_features fmt swr_app ${EXTRA_LIBS})

add_executable(demo_blinn_phong
	${COMMON_SOURCES}
	../../deps/3rd-party/lodepng/lodepng.cpp
	blinn_phong/main.cpp
)
target_link_libraries(demo_blinn_phong swrast cpu_features fmt swr_app ${EXTRA_LIBS})

add_executable(demo_clear
	${COMMON_SOURCES}
	clear/main.cpp
)
target_link_libraries(demo_clear swrast cpu_features fmt swr_app ${EXTRA_LIBS})

add_executable(demo_color
	${COMMON_SOURCES}
	../../deps/3rd-party/lodepng/lodepng.cpp
	color/main.cpp
)
target_link_libraries(demo_color swrast cpu_features fmt swr_app ${EXTRA_LIBS})

add_executable(demo_emitter
	${COMMON_SOURCES}
	../../deps/3rd-party/lodepng/lodepng.cpp
	emitter/main.cpp
)
target_link_libraries(demo_emitter swrast cpu_features fmt swr_app ${EXTRA_LIBS})

add_executable(demo_fill_test
	${COMMON_SOURCES}
	common/mesh.cpp
	fill_test/main.cpp
)
target_link_libraries(demo_fill_test swrast cpu_features fmt swr_app ${EXTRA_LIBS})

add_executable(demo_gears
	${COMMON_SOURCES}
	gears/main.cpp
)
target_link_libraries(demo_gears swrast cpu_features fmt swr_app ${EXTRA_LIBS})

add_executable(demo_motion_blur
	${COMMON_SOURCES}
	../../deps/3rd-party/lodepng/lodepng.cpp
	common/mesh.cpp
	motion_blur/main.cpp
)
target_link_libraries(demo_motion_blur swrast cpu_features fmt swr_app ${EXTRA_LIBS})

add_executable(demo_normal_map
	${COMMON_SOURCES}
	../../deps/3rd-party/lodepng/lodepng.cpp
	normal_map/main.cpp
)
target_link_libraries(demo_normal_map swrast cpu_features fmt swr_app ${EXTRA_LIBS})

add_executable(demo_phong
	${COMMON_SOURCES}
	../../deps/3rd-party/lodepng/lodepng.cpp
	phong/main.cpp
)
target_link_libraries(demo_phong swrast cpu_features fmt swr_app ${EXTRA_LIBS})

add_executable(demo_scissor
	${COMMON_SOURCES}
	../../deps/3rd-party/lodepng/lodepng.cpp
	scissor/main.cpp
)
target_link_libraries(demo_scissor swrast cpu_features fmt swr_app ${EXTRA_LIBS})

add_executable(demo_texture
	${COMMON_SOURCES}
	../../deps/3rd-party/lodepng/lodepng.cpp
	textures/main.cpp
)
target_link_libraries(demo_texture swrast cpu_features fmt swr_app ${EXTRA_LIBS})

add_executable(demo_timings
	${COMMON_SOURCES}
	../../deps/3rd-party/lodepng/lodepng.cpp
	common/font.cpp
	timings/main.cpp
	timings/main.cpp
)
target_link_libraries(demo_timings swrast cpu_features fmt swr_app ${EXTRA_LIBS})
