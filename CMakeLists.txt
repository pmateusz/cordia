cmake_minimum_required(VERSION 3.1 FATAL_ERROR)
project(rows LANGUAGES C CXX VERSION 0.0.1)
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)


# -Wall -> enable all warnings that are useful to avoid
# -Werror -> make all warnings errors
# -Wl,-z,relro,-z,now -> enable full memory corruption mitigation technique
# set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-unused-but-set-variable -Wno-unused-variable -Wall -Werror -Wl,-z,relro,-z,now")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -ggdb -pg")
#set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Ofast")

set(CMAKE_VERBOSE_MAKEFILE FALSE)
set(CMAKE_BUILD_TYPE Debug)
#set(CMAKE_BUILD_TYPE Release)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/dependencies/cmake/modules/")
set(THREADS_PREFER_PTHREAD_FLAG ON)

include(ExternalProject)
include(${CMAKE_SOURCE_DIR}/dependencies/cmake/external/osrm.cmake)

# Fix for compilation error in GTEST headers
# ‘tuple_size’ is already declared in this scope
#  using ::std::tuple_size;
# add_definitions(-DGTEST_USE_OWN_TR1_TUPLE=0)

# Equivalent to enable_testing()
# include(CTest)

file(GLOB_RECURSE SOURCES "src/main/*.cpp")
add_library(rows STATIC ${SOURCES})
# target_compile_definitions(rows PUBLIC )
# target_include_directories(rows PUBLIC )
# target_link_libraries(rows )
# set_source_files_properties(${SOURCES} PROPERTIES COMPILE_FLAGS "-Wno-unused-variable -Wno-deprecated")
add_dependencies(rows ${external_project_dependencies})