project(swtest)

# add support for boost test framework
add_definitions(-DBOOST_TEST_DYN_LINK) 
find_package(Boost COMPONENTS unit_test_framework REQUIRED)
link_directories ( ${Boost_LIBRARY_DIRS} )

# add library include directories
include_directories(../../include ../../src/library)

#
# build tests
#

add_executable(test_utils library/utils.cpp)
target_link_libraries(test_utils
    ${Boost_UNIT_TEST_FRAMEWORK_LIBRARY}
    )

add_executable(test_clipping library/clipping.cpp)
target_link_libraries(test_clipping
    swrast
    fmt
    ${Boost_UNIT_TEST_FRAMEWORK_LIBRARY}
    )
    