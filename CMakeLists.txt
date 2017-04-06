cmake_minimum_required (VERSION 3.6.1)
project (BansheeSBGen)

set(CMAKE_CXX_STANDARD 14)
mark_as_advanced(CMAKE_CONFIGURATION_TYPES)
mark_as_advanced(CMAKE_INSTALL_PREFIX)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/CMake/Modules/")
find_package(Clang)

include_directories(${clang_INCLUDE_DIRS})

add_executable(BansheeSBGen Source/main.cpp Source/generator.cpp Source/parser.cpp Source/common.h Source/parser.h)
target_link_libraries(BansheeSBGen ${clang_LIBRARIES})

if(WIN32)
	target_link_libraries(BansheeSBGen "version.lib")
endif()

