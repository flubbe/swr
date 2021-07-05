project(swtest)

# add support for boost test framework
add_definitions(-DBOOST_TEST_DYN_LINK) 
find_package(Boost COMPONENTS unit_test_framework REQUIRED)
link_directories ( ${Boost_LIBRARY_DIRS} )

#
# build tests
#

add_executable(test_utils library/utils.cpp)
target_link_libraries(test_utils
    ${Boost_UNIT_TEST_FRAMEWORK_LIBRARY}
    )
