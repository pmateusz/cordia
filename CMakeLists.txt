cmake_minimum_required(VERSION 3.1 FATAL_ERROR)
project(rows LANGUAGES C CXX VERSION 0.0.1)
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(THREADS_PREFER_PTHREAD_FLAG ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_THREAD_PREFER_PTHREAD TRUE)
set(CMAKE_VERBOSE_MAKEFILE FALSE)

if (CMAKE_COMPILER_IS_GNUCXX)
    set(CMAKE_CXX_FLAGS_DEBUG "-O0 -ggdb")
    set(CMAKE_CXX_FLAGS_RELEASE "-O4 -DNDEBUG -march=native")
endif ()

# uncomment for a debug mode build
set(ORTOOLS_ROOT_DIR /home/pmateusz/dev/pmateusz-ortools-7.1)
set(AMPL_ROOT_DIR /home/pmateusz/dev/amplapi-linux64)

set(default_build_type "RELEASE")
if (EXISTS "${CMAKE_SOURCE_DIR}/.git")
    set(default_build_type "DEBUG")
endif ()

if (NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    message(STATUS "Setting build type to '${default_build_type}' as none was specified.")
    set(CMAKE_BUILD_TYPE "${default_build_type}" CACHE
            STRING "Choose the type of build." FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
            "DEBUG" "RELEASE")
endif ()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/dependencies/cmake/modules/")

if (UNIX AND NOT APPLE)
    find_package(Rt REQUIRED)
    set(MAYBE_RT_LIBRARY ${RT_LIBRARY})
endif ()

# get_filename_component(_GFLAGS_ROOT_DIR "${CMAKE_SOURCE_DIR}/dependencies/install" REALPATH)
# get_filename_component(_GLOG_ROOT_DIR "${CMAKE_SOURCE_DIR}/dependencies/install" REALPATH)
get_filename_component(_GFLAGS_ROOT_DIR "${ORTOOLS_ROOT_DIR}/dependencies/install" REALPATH)
get_filename_component(_GLOG_ROOT_DIR "${ORTOOLS_ROOT_DIR}/dependencies/install" REALPATH)
get_filename_component(_SPARSEHASH_ROOT_DIR "${CMAKE_SOURCE_DIR}/dependencies/build/external/sparsehash-install" REALPATH)
get_filename_component(_GTEST_ROOT "${CMAKE_SOURCE_DIR}/dependencies/build/external/gtest-install" REALPATH)
get_filename_component(_JSON_ROOT "${CMAKE_SOURCE_DIR}/dependencies/build/external/json-install" REALPATH)
get_filename_component(_GEXF_ROOT "${CMAKE_SOURCE_DIR}/dependencies/build/external/gexf-install" REALPATH)
get_filename_component(_GUROBI_ROOT "/opt/gurobi811" REALPATH)

set(GFLAGS_ROOT_DIR ${_GFLAGS_ROOT_DIR})
set(GLOG_ROOT_DIR ${_GLOG_ROOT_DIR})
set(GTEST_ROOT ${_GTEST_ROOT})
set(SPARSEHASH_ROOT_DIR ${_SPARSEHASH_ROOT_DIR})
set(OSRM_ROOT_DIR ${CMAKE_BINARY_DIR}/external/osrm-install)
set(JSON_ROOT_DIR ${_JSON_ROOT})
set(GEXF_ROOT_DIR ${_GEXF_ROOT})
set(GUROBI_ROOT_DIR ${_GUROBI_ROOT})

#include(${CMAKE_SOURCE_DIR}/dependencies/cmake/external/TBBGet.cmake)
#include(${CMAKE_SOURCE_DIR}/dependencies/cmake/external/TBBBuild.cmake)
#tbb_get(TBB_ROOT tbb_root RELEASE_TAG 2017_U7 SOURCE_CODE)
#tbb_build(TBB_ROOT ${tbb_root} CONFIG_DIR TBB_DIR MAKE_ARGS tbb_build_dir=${tbb_root}/build)
#find_package(TBB REQUIRED)

include(ExternalProject)
include(${CMAKE_SOURCE_DIR}/dependencies/cmake/external/osrm.cmake)
include(${CMAKE_SOURCE_DIR}/dependencies/cmake/external/gflags.cmake)
include(${CMAKE_SOURCE_DIR}/dependencies/cmake/external/gtest.cmake)
include(${CMAKE_SOURCE_DIR}/dependencies/cmake/external/glog.cmake)
include(${CMAKE_SOURCE_DIR}/dependencies/cmake/external/json.cmake)
include(${CMAKE_SOURCE_DIR}/dependencies/cmake/external/sparsehash.cmake)
include(${CMAKE_SOURCE_DIR}/dependencies/cmake/external/ortools.cmake)
include(${CMAKE_SOURCE_DIR}/dependencies/cmake/external/gexf.cmake)

find_package(LibXml2 REQUIRED)
find_package(ORtools REQUIRED)
find_package(Threads REQUIRED)
find_package(ZLIB REQUIRED)
find_package(EXPAT REQUIRED)
find_package(GFlags REQUIRED)
find_package(Gurobi REQUIRED)
find_package(Ampl REQUIRED)
find_package(Json)
find_package(Gexf)

find_package(Boost REQUIRED COMPONENTS filesystem system iostreams thread)

if (${OSRM_EXTERNAL})
    set(NESTED_OSRM_INCLUDE_DIR "${OSRM_INCLUDE_DIR}/osrm")
else ()
    get_filename_component(NESTED_OSRM_INCLUDE_DIR "osrm" ABSOLUTE BASE_DIR "${OSRM_INCLUDE_DIR}")
endif ()

get_filename_component(HEADERS "${CMAKE_SOURCE_DIR}/src/main" REALPATH)
set(HEADER_DEP ${OSRM_INCLUDE_DIR}
        ${NESTED_OSRM_INCLUDE_DIR}
        ${Boost_INCLUDE_DIR}
        ${JSON_INCLUDE_DIR}
        ${GEXF_INCLUDE_DIR}
        ${ORTOOLS_INCLUDE_DIR}
        ${AMPL_INCLUDE_DIR}
        ${LIBXML2_INCLUDE_DIR}
        ${GUROBI_INCLUDE_DIR}
        ${GFLAGS_INCLUDE_DIR}
        ${GLOG_INCLUDE_DIR})

set(LIBRARY_DEP ${ORTOOLS_LIBRARY}
        ${Protobuf_LIBRARIES}
        ${GUROBI_STATIC_LIBRARY}
        ${GUROBI_LIBRARY}
        ${OSRM_LIBRARY}
        ${AMPL_LIBRARY}
        ${Boost_LIBRARIES}
        ${ZLIB_LIBRARIES}
        ${EXPAT_LIBRARIES}
        ${GTEST_LIBRARIES}
        ${GEXF_LIBRARY}
        ${GLOG_LIBRARIES}
        ${GFLAGS_LIBRARY}
        ${LIBXML2_LIBRARIES}
        ${CMAKE_THREAD_LIBS_INIT}
        ${MAYBE_RT_LIBRARY})

file(GLOB_RECURSE SOURCES "${CMAKE_SOURCE_DIR}/src/main/*.cpp")
add_library(rows STATIC ${SOURCES})
target_include_directories(rows PUBLIC ${HEADERS} ${HEADER_DEP})

add_executable(rows-main "${CMAKE_SOURCE_DIR}/src/main/rows-solver.cpp")
target_include_directories(rows-main PUBLIC ${HEADERS} ${HEADER_DEP})
target_link_libraries(rows-main rows ${LIBRARY_DEP})
set_property(TARGET rows-main PROPERTY CXX_STANDARD 14)

add_executable(rows-benchmark "${CMAKE_SOURCE_DIR}/src/main/rows-benchmark.cpp")
target_include_directories(rows-benchmark PUBLIC ${HEADERS} ${HEADER_DEP})
target_link_libraries(rows-benchmark rows ${LIBRARY_DEP})
set_property(TARGET rows-benchmark PROPERTY CXX_STANDARD 14)

add_executable(rows-mip-solver "${CMAKE_SOURCE_DIR}/src/main/rows-mip.cpp")
target_include_directories(rows-mip-solver PUBLIC ${HEADERS} ${HEADER_DEP})
target_link_libraries(rows-mip-solver rows ${LIBRARY_DEP})
set_property(TARGET rows-mip-solver PROPERTY CXX_STANDARD 14)
add_dependencies(rows-mip-solver rows)

add_executable(rows-estimate "${CMAKE_SOURCE_DIR}/src/main/rows-estimate.cpp")
target_include_directories(rows-estimate PUBLIC ${HEADERS} ${HEADER_DEP})
target_link_libraries(rows-estimate rows ${LIBRARY_DEP})
set_property(TARGET rows-estimate PROPERTY CXX_STANDARD 14)

add_executable(rows-fast "${CMAKE_SOURCE_DIR}/src/main/rows-fast.cpp")
target_include_directories(rows-fast PUBLIC ${HEADERS} ${HEADER_DEP})
target_link_libraries(rows-fast rows ${LIBRARY_DEP} ${GUROBI_LIBRARY})
set_property(TARGET rows-fast PROPERTY CXX_STANDARD 14)

add_executable(rows-routing-server "${CMAKE_SOURCE_DIR}/src/main/rows-routing-server.cpp")
target_include_directories(rows-routing-server PUBLIC ${HEADERS} ${HEADER_DEP})
target_link_libraries(rows-routing-server rows ${LIBRARY_DEP})
set_property(TARGET rows-routing-server PROPERTY CXX_STANDARD 14)

get_filename_component(TEST_HEADERS "${CMAKE_SOURCE_DIR}/src/test" REALPATH)

file(GLOB_RECURSE TEST_SOURCES "${CMAKE_SOURCE_DIR}/src/test/*.cpp")
add_library(rows-test STATIC ${TEST_SOURCES})
target_include_directories(rows-test PUBLIC ${TEST_HEADERS} ${HEADERS} ${HEADER_DEP} ${GTEST_INCLUDE_DIR})
set_property(TARGET rows-test PROPERTY CXX_STANDARD 14)
add_dependencies(rows-test rows)

if (${OSRM_EXTERNAL})
    add_dependencies(rows osrm)
endif ()

if (${JSON_EXTERNAL})
    add_dependencies(rows json-external)
endif ()

if (${GFLAGS_EXTERNAL})
    add_dependencies(rows gflags)
endif ()

if (${GEXF_EXTERNAL})
    add_dependencies(rows gexf-external)
endif ()

if (${GTEST_EXTERNAL})
    add_dependencies(rows gtest-external)
endif ()

if (${GLOG_EXTERNAL})
    add_dependencies(rows glog)
endif ()

if (${SPARSEHASH_EXTERNAL})
    add_dependencies(rows sparsehash)
endif ()

include(ProcessorCount)
ProcessorCount(NPROC)
add_custom_target(build_all
        COMMAND ${CMAKE_MAKE_PROGRAM} --jobs=${NPROC}
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})

# Fix for compilation error in GTEST headers
# ‘tuple_size’ is already declared in this scope
#  using ::std::tuple_size;
add_definitions(-DGTEST_USE_OWN_TR1_TUPLE=0)

macro(m_add_executable GOAL SOURCE)
    add_executable(${GOAL} ${SOURCE})
    target_link_libraries(${GOAL}
            rows-test
            rows
            ${LIBRARY_DEP})
    target_include_directories(${GOAL} PUBLIC ${TEST_HEADERS} ${HEADERS} ${HEADER_DEP} ${GTEST_INCLUDE_DIR})
    set_property(TARGET ${GOAL} PROPERTY CXX_STANDARD 14)
    add_dependencies(${GOAL} rows-test)
endmacro()

file(GLOB_RECURSE _test_files src/test/*_test.cpp)
foreach (_test_file ${_test_files})
    get_filename_component(_test_file_name ${_test_file} NAME_WE)
    m_add_executable(${_test_file_name} ${_test_file})
endforeach ()

mark_as_advanced(_test_file_name _test_file _test_files)
