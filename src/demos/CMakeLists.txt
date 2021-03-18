project (swrdemos)

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

add_executable(demo_color
	${COMMON_SOURCES}
	../../deps/3rd-party/lodepng/lodepng.cpp
	color/main.cpp
)
target_link_libraries(demo_color swr cpu_features fmt swr_app ${EXTRA_LIBS})

add_executable(demo_texture
	${COMMON_SOURCES}
	../../deps/3rd-party/lodepng/lodepng.cpp
	textures/main.cpp
)
target_link_libraries(demo_texture swr cpu_features fmt swr_app ${EXTRA_LIBS})

add_executable(demo_phong
	${COMMON_SOURCES}
	../../deps/3rd-party/lodepng/lodepng.cpp
	phong/main.cpp
)
target_link_libraries(demo_phong swr cpu_features fmt swr_app ${EXTRA_LIBS})

add_executable(demo_blinn_phong
	${COMMON_SOURCES}
	../../deps/3rd-party/lodepng/lodepng.cpp
	blinn_phong/main.cpp
)
target_link_libraries(demo_blinn_phong swr cpu_features fmt swr_app ${EXTRA_LIBS})

add_executable(demo_scissor
	${COMMON_SOURCES}
	../../deps/3rd-party/lodepng/lodepng.cpp
	scissor/main.cpp
)
target_link_libraries(demo_scissor swr cpu_features fmt swr_app ${EXTRA_LIBS})
