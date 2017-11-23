cmake_minimum_required(VERSION 3.1 FATAL_ERROR)
project(rows LANGUAGES C CXX VERSION 0.0.1)
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(THREADS_PREFER_PTHREAD_FLAG ON)

# set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-unused-but-set-variable -Wno-unused-variable -Wall -Werror -Wl,-z,relro,-z,now")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -ggdb -pg")
#set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Ofast")

set(CMAKE_VERBOSE_MAKEFILE FALSE)
set(CMAKE_BUILD_TYPE Debug)
#set(CMAKE_BUILD_TYPE Release)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/dependencies/cmake/modules/")
set(THREADS_PREFER_PTHREAD_FLAG ON)

# Fix for compilation error in GTEST headers
# ‘tuple_size’ is already declared in this scope
#  using ::std::tuple_size;
add_definitions(-DGTEST_USE_OWN_TR1_TUPLE=0)

if(UNIX AND NOT APPLE)
    set(MAYBE_RT_LIBRARY -lrt)
endif()

get_filename_component(_GFLAGS_ROOT_DIR "${CMAKE_SOURCE_DIR}/dependencies/build/external/gflags-install" REALPATH)
get_filename_component(_GLOG_ROOT_DIR "${CMAKE_SOURCE_DIR}/dependencies/build/external/glog-install" REALPATH)
get_filename_component(_SPARSEHASH_ROOT_DIR "${CMAKE_SOURCE_DIR}/dependencies/build/external/sparsehash-install" REALPATH)
get_filename_component(_GTEST_ROOT "${CMAKE_SOURCE_DIR}/dependencies/build/external/gtest-install" REALPATH)

set(GFLAGS_ROOT_DIR ${_GFLAGS_ROOT_DIR})
set(GLOG_ROOT_DIR ${_GLOG_ROOT_DIR})
set(GTEST_ROOT ${_GTEST_ROOT})
set(SPARSEHASH_ROOT_DIR ${_SPARSEHASH_ROOT_DIR})
set(OSRM_ROOT_DIR ${CMAKE_BINARY_DIR}/external/osrm-install)

include(ExternalProject)
include(${CMAKE_SOURCE_DIR}/dependencies/cmake/external/osrm.cmake)
include(${CMAKE_SOURCE_DIR}/dependencies/cmake/external/gflags.cmake)
include(${CMAKE_SOURCE_DIR}/dependencies/cmake/external/gtest.cmake)
include(${CMAKE_SOURCE_DIR}/dependencies/cmake/external/glog.cmake)
include(${CMAKE_SOURCE_DIR}/dependencies/cmake/external/sparsehash.cmake)
include(${CMAKE_SOURCE_DIR}/dependencies/cmake/external/ortools.cmake)

find_package(ORtools REQUIRED)
find_package(Threads REQUIRED)
find_package(ZLIB REQUIRED)
find_package(EXPAT REQUIRED)
find_package(Boost REQUIRED COMPONENTS filesystem system thread iostreams regex)

file(GLOB_RECURSE SOURCES "${CMAKE_SOURCE_DIR}/src/main/*.cpp")
add_library(rows STATIC ${SOURCES})

if(${OSRM_EXTERNAL})
    add_dependencies(rows osrm)
    set(NESTED_OSRM_INCLUDE_DIR "${OSRM_INCLUDE_DIR}/osrm")
else()
    get_filename_component(NESTED_OSRM_INCLUDE_DIR "osrm" ABSOLUTE BASE_DIR "${OSRM_INCLUDE_DIR}")
endif()

if(${GFLAGS_EXTERNAL})
    add_dependencies(rows gflags)
endif()

if(${GTEST_EXTERNAL})
    add_dependencies(rows gtest)
endif()

if(${GLOG_EXTERNAL})
    add_dependencies(rows glog)
endif()

if(${SPARSEHASH_EXTERNAL})
    add_dependencies(rows sparsehash)
endif()

target_include_directories(rows PUBLIC ${OSRM_INCLUDE_DIR} ${NESTED_OSRM_INCLUDE_DIR} ${Boost_INCLUDE_DIR})
set_property(TARGET rows PROPERTY CXX_STANDARD 14)

add_executable(rows-main "${CMAKE_SOURCE_DIR}/src/main/main.cpp")
target_include_directories(rows-main PUBLIC ${OSRM_INCLUDE_DIR} ${NESTED_OSRM_INCLUDE_DIR} ${Boost_INCLUDE_DIR})
target_link_libraries(rows-main rows ${OSRM_LIBRARY} ${Boost_LIBRARIES} ${ZLIB_LIBRARIES} ${EXPAT_LIBRARIES} ${CMAKE_THREAD_LIBS_INIT} ${MAYBE_RT_LIBRARY})
set_property(TARGET rows-main PROPERTY CXX_STANDARD 14)
add_dependencies(rows-main rows)

include(ProcessorCount)
ProcessorCount(NPROC)
add_custom_target(build_all
    COMMAND ${CMAKE_MAKE_PROGRAM} --jobs=${NPROC}
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})