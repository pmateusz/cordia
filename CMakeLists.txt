cmake_minimum_required(VERSION 3.1 FATAL_ERROR)
project(rows LANGUAGES C CXX VERSION 0.0.1)
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(THREADS_PREFER_PTHREAD_FLAG ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-unused-but-set-variable -Wno-unused-variable -Wall -Werror -Wl,-z,relro,-z,now")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -ggdb -pg")
#set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -D_GLIBCXX_USE_CXX11_ABI=1")
#set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O2 -march=native")
#set(ORTOOLS_ROOT_DIR "/home/pmateusz/dev/or-tools/")

set(CMAKE_VERBOSE_MAKEFILE FALSE)
set(CMAKE_BUILD_TYPE Debug)
#set(CMAKE_BUILD_TYPE Release)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/dependencies/cmake/modules/")

if (UNIX AND NOT APPLE)
    set(MAYBE_RT_LIBRARY -lrt)
endif ()

get_filename_component(_GFLAGS_ROOT_DIR "${CMAKE_SOURCE_DIR}/dependencies/build/external/gflags-install" REALPATH)
get_filename_component(_GLOG_ROOT_DIR "${CMAKE_SOURCE_DIR}/dependencies/build/external/glog-install" REALPATH)
get_filename_component(_SPARSEHASH_ROOT_DIR "${CMAKE_SOURCE_DIR}/dependencies/build/external/sparsehash-install" REALPATH)
get_filename_component(_GTEST_ROOT "${CMAKE_SOURCE_DIR}/dependencies/build/external/gtest-install" REALPATH)
get_filename_component(_JSON_ROOT "${CMAKE_SOURCE_DIR}/dependencies/build/external/json-install" REALPATH)
get_filename_component(_GEXF_ROOT "${CMAKE_SOURCE_DIR}/dependencies/build/external/gexf-install" REALPATH)

set(GFLAGS_ROOT_DIR ${_GFLAGS_ROOT_DIR})
set(GLOG_ROOT_DIR ${_GLOG_ROOT_DIR})
set(GTEST_ROOT ${_GTEST_ROOT})
set(SPARSEHASH_ROOT_DIR ${_SPARSEHASH_ROOT_DIR})
set(OSRM_ROOT_DIR ${CMAKE_BINARY_DIR}/external/osrm-install)
set(JSON_ROOT_DIR ${_JSON_ROOT})
set(GEXF_ROOT_DIR ${_GEXF_ROOT})

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
find_package(Glog REQUIRED)
find_package(Json)
find_package(Gexf)

find_package(Boost REQUIRED COMPONENTS filesystem system thread iostreams regex)

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
        ${LIBXML2_INCLUDE_DIR}
        ${GFLAGS_INCLUDE_DIR}
        ${GLOG_INCLUDE_DIR})

set(LIBRARY_DEP ${ORTOOLS_LIBRARY}
        ${OSRM_LIBRARY}
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
set_property(TARGET rows PROPERTY CXX_STANDARD 14)

add_executable(rows-main "${CMAKE_SOURCE_DIR}/src/main/rows-solver.cpp")
target_include_directories(rows-main PUBLIC ${HEADERS} ${HEADER_DEP})
target_link_libraries(rows-main rows ${LIBRARY_DEP})
target_compile_definitions(rows-main PUBLIC ${ORTOOLS_COMPILER_DEFINITIONS} -D_GLIBCXX_USE_CXX11_ABI=1)
set_property(TARGET rows-main PROPERTY CXX_STANDARD 14)

add_executable(rows-fast "${CMAKE_SOURCE_DIR}/src/main/rows-fast.cpp")
target_include_directories(rows-fast PUBLIC ${HEADERS} ${HEADER_DEP})
target_link_libraries(rows-fast rows ${LIBRARY_DEP})
target_compile_definitions(rows-fast PUBLIC ${ORTOOLS_COMPILER_DEFINITIONS} -D_GLIBCXX_USE_CXX11_ABI=1)
set_property(TARGET rows-fast PROPERTY CXX_STANDARD 14)

get_filename_component(TEST_HEADERS "${CMAKE_SOURCE_DIR}/src/test" REALPATH)

file(GLOB_RECURSE TEST_SOURCES "${CMAKE_SOURCE_DIR}/src/test/*.cpp")
add_library(rows-test STATIC ${TEST_SOURCES})
target_include_directories(rows-test PUBLIC ${TEST_HEADERS} ${HEADERS} ${HEADER_DEP})
target_compile_definitions(rows-test PUBLIC ${ORTOOLS_COMPILER_DEFINITIONS} -D_GLIBCXX_USE_CXX11_ABI=1)
add_dependencies(rows-test rows)

if (${OSRM_EXTERNAL})
    add_dependencies(rows osrm)
endif ()

if (${JSON_EXTERNAL})
    add_dependencies(rows json)
endif ()

if (${GFLAGS_EXTERNAL})
    add_dependencies(rows gflags)
endif ()

if (${GEXF_EXTERNAL})
    add_dependencies(rows gexf)
endif ()

if (${GTEST_EXTERNAL})
    add_dependencies(rows gtest)
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