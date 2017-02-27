cmake_minimum_required (VERSION 3.6.1)
project (BansheeScriptParser)

set(CMAKE_CXX_STANDARD 14)
mark_as_advanced(CMAKE_CONFIGURATION_TYPES)
mark_as_advanced(CMAKE_INSTALL_PREFIX)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/CMake/Modules/")
find_package(Clang)

include_directories(${clang_INCLUDE_DIRS})

add_executable(ScriptBindingParser Source/main.cpp)
target_link_libraries(ScriptBindingParser ${clang_LIBRARIES})

if(WIN32)
	target_link_libraries(ScriptBindingParser "version.lib")
endif()

