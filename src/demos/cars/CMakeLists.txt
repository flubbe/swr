project(cars)

include_directories(
    ../../../deps/3rd-party/simdjson/singleheader
)

add_executable(demo_cars
	../../common/platform/cpu.cpp
	../../common/platform/log.cpp
	../../common/platform/platform.cpp
	../../../deps/3rd-party/lodepng/lodepng.cpp
    ../../../deps/3rd-party/simdjson/singleheader/simdjson.cpp
	main.cpp
    ../common/font.cpp
)
target_link_libraries(demo_cars swrast swr_app ${EXTRA_LIBS})