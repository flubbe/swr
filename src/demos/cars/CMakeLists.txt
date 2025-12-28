project(cars)

include_directories(
    ../../../deps/3rd-party/simdjson/singleheader
)

add_executable(demo_cars
    ../../../deps/3rd-party/simdjson/singleheader/simdjson.cpp
	../../common/platform/cpu.cpp
	../../common/platform/log.cpp
	../../common/platform/platform.cpp
    ../common/stb.cpp
    ../common/font.cpp
	main.cpp
)
target_link_libraries(demo_cars swrast swr_app ${EXTRA_LIBS})