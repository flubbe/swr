project (swr_app)

#
# Set up project include directories
#
include_directories(. ../../../inc)

# add support for boost test framework
add_definitions(-DBOOST_TEST_DYN_LINK) 
link_directories ( ${Boost_LIBRARY_DIRS} )

add_library(swr_app SHARED 
	../common/platform/cpu.cpp
	../common/platform/log.cpp
	../common/platform/platform.cpp
	main.cpp 
	framework.cpp
	)

target_link_libraries(swr_app fmt ${EXTRA_LIBS})