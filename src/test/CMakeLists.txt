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
    swr_internal_config
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
    swr_internal_config
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
    swr_internal_config
    swr_project_options
    swr_project_warnings
)
add_test(NAME rasterizer_triangle COMMAND test_rasterizer_triangle --color_output=no)

add_executable(test_rasterizer_sparse_triangle library/rasterizer/sparse_triangle.cpp)
target_include_directories(test_rasterizer_sparse_triangle
    PRIVATE
    ${SWR_SOURCE_DIR}/src
    ${SWR_SOURCE_DIR}/src/library
)
target_compile_definitions(test_rasterizer_sparse_triangle PRIVATE BOOST_TEST_DYN_LINK)
target_link_libraries(test_rasterizer_sparse_triangle
    PRIVATE
    swrast
    Boost::unit_test_framework
    libmorton
    concurrency_utils
    swr_internal_config
    swr_project_options
    swr_project_warnings
)
add_test(NAME rasterizer_sparse_triangle COMMAND test_rasterizer_sparse_triangle --color_output=no)

add_executable(test_rasterizer_early_z library/rasterizer/early_z.cpp)
target_include_directories(test_rasterizer_early_z
    PRIVATE
    ${SWR_SOURCE_DIR}/src
    ${SWR_SOURCE_DIR}/src/library
)
target_compile_definitions(test_rasterizer_early_z PRIVATE BOOST_TEST_DYN_LINK)
target_link_libraries(test_rasterizer_early_z
    PRIVATE
    swrast
    Boost::unit_test_framework
    libmorton
    concurrency_utils
    swr_internal_config
    swr_project_options
    swr_project_warnings
)
add_test(NAME rasterizer_early_z COMMAND test_rasterizer_early_z --color_output=no)

add_executable(test_pipeline library/pipeline.cpp)
target_include_directories(test_pipeline
    PRIVATE
    ${SWR_SOURCE_DIR}/include
    ${SWR_SOURCE_DIR}/src
    ${SWR_SOURCE_DIR}/src/library
)
target_compile_definitions(test_pipeline PRIVATE BOOST_TEST_DYN_LINK)
target_link_libraries(test_pipeline
    PRIVATE
    swrast
    Boost::unit_test_framework
    libmorton
    concurrency_utils
    swr_internal_config
    swr_project_options
    swr_project_warnings
)
add_test(NAME pipeline COMMAND test_pipeline --color_output=no)

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
    swr_internal_config
    swr_project_options
    swr_project_warnings
)
add_test(NAME clipping COMMAND test_clipping --color_output=no)

add_executable(test_textures library/textures.cpp)
target_include_directories(test_textures
    PRIVATE
    ${SWR_SOURCE_DIR}/include
    ${SWR_SOURCE_DIR}/src
    ${SWR_SOURCE_DIR}/src/library
)
target_compile_definitions(test_textures PRIVATE BOOST_TEST_DYN_LINK)
target_link_libraries(test_textures
    PRIVATE
    swrast
    Boost::unit_test_framework
    libmorton
    concurrency_utils
    swr_internal_config
    swr_project_options
    swr_project_warnings
)
add_test(NAME textures COMMAND test_textures --color_output=no)

add_executable(test_output_merger library/output_merger.cpp)
target_include_directories(test_output_merger
    PRIVATE
    ${SWR_SOURCE_DIR}/include
    ${SWR_SOURCE_DIR}/src
    ${SWR_SOURCE_DIR}/src/library
)
target_compile_definitions(test_output_merger
    PRIVATE
    BOOST_TEST_DYN_LINK
)
target_link_libraries(test_output_merger
    PRIVATE
    swrast
    Boost::unit_test_framework
    libmorton
    concurrency_utils
    swr_internal_config
    swr_project_options
    swr_project_warnings
)
add_test(NAME output_merger COMMAND test_output_merger --color_output=no)

add_executable(test_framebuffer_blending library/framebuffer_blending.cpp)
target_include_directories(test_framebuffer_blending
    PRIVATE
    ${SWR_SOURCE_DIR}/include
    ${SWR_SOURCE_DIR}/src
    ${SWR_SOURCE_DIR}/src/library
)
target_compile_definitions(test_framebuffer_blending
    PRIVATE
    BOOST_TEST_DYN_LINK
)
target_link_libraries(test_framebuffer_blending
    PRIVATE
    swrast
    Boost::unit_test_framework
    libmorton
    concurrency_utils
    swr_internal_config
    swr_project_options
    swr_project_warnings
)
add_test(NAME framebuffer_blending COMMAND test_framebuffer_blending --color_output=no)

add_executable(test_framebuffer library/framebuffer.cpp)
target_include_directories(test_framebuffer
    PRIVATE
    ${SWR_SOURCE_DIR}/include
    ${SWR_SOURCE_DIR}/src
    ${SWR_SOURCE_DIR}/src/library
)
target_compile_definitions(test_framebuffer
    PRIVATE
    BOOST_TEST_DYN_LINK
)
target_link_libraries(test_framebuffer
    PRIVATE
    swrast
    Boost::unit_test_framework
    libmorton
    concurrency_utils
    swr_internal_config
    swr_project_options
    swr_project_warnings
)
add_test(NAME framebuffer COMMAND test_framebuffer --color_output=no)

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

add_executable(test_slot_map slot_map.cpp)
target_compile_definitions(test_slot_map PRIVATE BOOST_TEST_DYN_LINK)
target_link_libraries(test_slot_map
    PRIVATE
    Boost::unit_test_framework
    swr_project_options
    swr_project_warnings
)
add_test(NAME slot_map COMMAND test_slot_map --color_output=no)
