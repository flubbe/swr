find_package(Boost CONFIG REQUIRED)

add_library(swr_app SHARED
    ${SWR_SOURCE_DIR}/src/common/platform/cpu.cpp
    ${SWR_SOURCE_DIR}/src/common/platform/log.cpp
    ${SWR_SOURCE_DIR}/src/common/platform/platform.cpp
    main.cpp
    framework.cpp
)

target_include_directories(swr_app
    PUBLIC
    $<BUILD_INTERFACE:${SWR_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
    PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${SWR_SOURCE_DIR}/src
)

target_link_libraries(swr_app
    PRIVATE
    cpu_features
    swrast
    swr_project_options
    swr_project_warnings
)
