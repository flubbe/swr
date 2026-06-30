if(CMAKE_SYSTEM_PROCESSOR STREQUAL "x86_64")
    project(swbench)

    find_package(benchmark QUIET)

    if(benchmark_FOUND)
        # memset benchmark (Google Benchmark)
        add_executable(bench_memset memset/main.cpp)
        target_link_libraries(bench_memset
            PRIVATE
            benchmark::benchmark
            swr_project_options
        )
    else()
        message(STATUS "Google Benchmark not found; skipping bench_memset target.")
    endif()
endif()

# end-to-end offscreen OBJ benchmark
add_executable(bench_obj
    obj/main.cpp
)
target_include_directories(bench_obj
    PRIVATE
    ${SWR_SOURCE_DIR}/src
)
target_link_libraries(bench_obj
    PRIVATE
    tinyobjloader
    swrast
    swr_project_options
    swr_project_warnings
)

# dedicated clipping-heavy offscreen OBJ benchmark
add_executable(bench_clip
    clip/main.cpp
)
target_include_directories(bench_clip
    PRIVATE
    ${SWR_SOURCE_DIR}/src
)
target_link_libraries(bench_clip
    PRIVATE
    tinyobjloader
    swrast
    swr_project_options
    swr_project_warnings
)

# occlusion-heavy offscreen OBJ benchmark with 5x5 model grid and orbit camera
add_executable(bench_occlusion
    occlusion/main.cpp
)
target_include_directories(bench_occlusion
    PRIVATE
    ${SWR_SOURCE_DIR}/src
)
target_link_libraries(bench_occlusion
    PRIVATE
    tinyobjloader
    swrast
    swr_project_options
    swr_project_warnings
)

# dedicated clipping-heavy offscreen gears benchmark
add_executable(bench_gears_clip
    gears_clip/main.cpp
)
target_include_directories(bench_gears_clip
    PRIVATE
    ${SWR_SOURCE_DIR}/src
)
target_link_libraries(bench_gears_clip
    PRIVATE
    swrast
    swr_project_options
    swr_project_warnings
)

if(SWR_ENABLE_DEV_BENCH_TESTS)
    add_test(
        NAME bench_obj
        COMMAND $<TARGET_FILE:bench_obj>
        --model=${SWR_SOURCE_DIR}/src/demos/cars/assets/cars/cop/Cop_Body.obj
        --frames=60
        --width=400
        --height=400
        --threads=0
    )
    add_test(
        NAME bench_clip
        COMMAND $<TARGET_FILE:bench_clip>
        --model=${SWR_SOURCE_DIR}/src/demos/cars/assets/cars/cop/Cop_Body.obj
        --frames=60
        --width=400
        --height=400
        --threads=0
    )
    add_test(
        NAME bench_occlusion
        COMMAND $<TARGET_FILE:bench_occlusion>
        --model=${SWR_SOURCE_DIR}/src/demos/cars/assets/cars/cop/Cop_Body.obj
        --frames=60
        --width=400
        --height=400
        --threads=0
        --camera_angle=0
    )
    add_test(
        NAME bench_gears_clip
        COMMAND $<TARGET_FILE:bench_gears_clip>
        --frames=60
        --width=640
        --height=480
        --threads=0
        --clip_orbit_amplitude=6.0
    )
endif()
