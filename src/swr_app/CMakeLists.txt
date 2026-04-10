#
# Boost.
#
find_package(Boost CONFIG REQUIRED)

#
# Set up project include directories
#
include_directories(
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/../
    ${CMAKE_CURRENT_LIST_DIR}/../../../inc)

# add support for boost test framework
add_definitions(-DBOOST_TEST_DYN_LINK)
link_directories(${Boost_LIBRARY_DIRS})

add_library(swr_app SHARED
    ../common/platform/cpu.cpp
    ../common/platform/log.cpp
    ../common/platform/platform.cpp
    main.cpp
    framework.cpp
)

target_include_directories(swr_app
    PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/../
)

target_link_libraries(swr_app cpu_features swrast ${EXTRA_LIBS})
