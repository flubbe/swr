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

    # end-to-end offscreen OBJ benchmark (manual timing, no Google Benchmark dependency)
    add_executable(obj_perf
        obj/main.cpp
    )
    target_include_directories(obj_perf
        PRIVATE
        ${SWR_SOURCE_DIR}/src
    )
    target_link_libraries(obj_perf
        PRIVATE
        tinyobjloader
        swrast
        swr_project_options
        swr_project_warnings
    )

    if(SWR_ENABLE_DEV_BENCH_TESTS)
        add_test(
            NAME obj_perf
            COMMAND $<TARGET_FILE:obj_perf>
                --model=${SWR_SOURCE_DIR}/src/demos/cars/assets/cars/cop/Cop_Body.obj
                --frames=60
                --width=400
                --height=400
                --threads=0
        )
    endif()
endif()
