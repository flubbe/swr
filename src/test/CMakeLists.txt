find_package(Boost CONFIG REQUIRED COMPONENTS unit_test_framework)

#
# tests
#

add_executable(test_rasterizer_point library/rasterizer/point.cpp)
target_include_directories(test_rasterizer_point
    PRIVATE
    ${SWR_SOURCE_DIR}/src
    ${SWR_SOURCE_DIR}/src/library
)
target_compile_definitions(test_rasterizer_point PRIVATE BOOST_TEST_DYN_LINK)
target_link_libraries(test_rasterizer_point
    PRIVATE
    swrast
    Boost::unit_test_framework
    libmorton
    concurrency_utils
    swr_project_options
    swr_project_warnings
)
add_test(NAME rasterizer_point COMMAND test_rasterizer_point --color_output=no)

add_executable(test_rasterizer_line library/rasterizer/line.cpp)
target_include_directories(test_rasterizer_line
    PRIVATE
    ${SWR_SOURCE_DIR}/src
    ${SWR_SOURCE_DIR}/src/library
)
target_compile_definitions(test_rasterizer_line PRIVATE BOOST_TEST_DYN_LINK)
target_link_libraries(test_rasterizer_line
    PRIVATE
    swrast
    Boost::unit_test_framework
    libmorton
    concurrency_utils
    swr_project_options
    swr_project_warnings
)
add_test(NAME rasterizer_line COMMAND test_rasterizer_line --color_output=no)

add_executable(test_rasterizer_triangle library/rasterizer/triangle.cpp)
target_include_directories(test_rasterizer_triangle
    PRIVATE
    ${SWR_SOURCE_DIR}/src
    ${SWR_SOURCE_DIR}/src/library
)
target_compile_definitions(test_rasterizer_triangle PRIVATE BOOST_TEST_DYN_LINK)
target_link_libraries(test_rasterizer_triangle
    PRIVATE
    swrast
    Boost::unit_test_framework
    libmorton
    concurrency_utils
    swr_project_options
    swr_project_warnings
)
add_test(NAME rasterizer_triangle COMMAND test_rasterizer_triangle --color_output=no)

add_executable(test_clipping library/clipping.cpp)
target_include_directories(test_clipping
    PRIVATE
    ${SWR_SOURCE_DIR}/src
    ${SWR_SOURCE_DIR}/src/library
)
target_compile_definitions(test_clipping PRIVATE BOOST_TEST_DYN_LINK)
target_link_libraries(test_clipping
    PRIVATE
    swrast
    Boost::unit_test_framework
    libmorton
    concurrency_utils
    swr_project_options
    swr_project_warnings
)
add_test(NAME clipping COMMAND test_clipping --color_output=no)

add_executable(test_coverage_mask library/coverage_mask.cpp)
target_include_directories(test_coverage_mask
    PRIVATE
    ${SWR_SOURCE_DIR}/include
    ${SWR_SOURCE_DIR}/src
    ${SWR_SOURCE_DIR}/src/library
)
target_compile_definitions(test_coverage_mask PRIVATE BOOST_TEST_DYN_LINK)
target_link_libraries(test_coverage_mask
    PRIVATE
    Boost::unit_test_framework
    SDL3::SDL3
    ml
    swr_project_options
    swr_project_warnings
)
add_test(NAME coverage_mask COMMAND test_coverage_mask --color_output=no)

add_executable(test_utils library/utils.cpp)
target_compile_definitions(test_utils PRIVATE BOOST_TEST_DYN_LINK)
target_link_libraries(test_utils
    PRIVATE
    Boost::unit_test_framework
    swr_project_options
    swr_project_warnings
)
add_test(NAME utils COMMAND test_utils --color_output=no)
