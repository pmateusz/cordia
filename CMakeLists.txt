cmake_minimum_required(VERSION 3.1 FATAL_ERROR)
project(rows LANGUAGES C CXX VERSION 0.0.1)
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(THREADS_PREFER_PTHREAD_FLAG ON)


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

set(OSRM_ROOT_DIR ${CMAKE_BINARY_DIR}/external/osrm-install)

include(ExternalProject)
include(${CMAKE_SOURCE_DIR}/dependencies/cmake/external/osrm.cmake)
get_filename_component(NESTED_OSRM_INCLUDE_DIR osrm ABSOLUTE BASE_DIR ${OSRM_INCLUDE_DIR})

find_package(Threads REQUIRED)
find_package(ZLIB REQUIRED)
find_package(EXPAT REQUIRED)
find_package(Boost REQUIRED COMPONENTS filesystem system thread iostreams regex)
if(UNIX AND NOT APPLE)
    set(MAYBE_RT_LIBRARY -lrt)
endif()

# Fix for compilation error in GTEST headers
# ‘tuple_size’ is already declared in this scope
#  using ::std::tuple_size;
# add_definitions(-DGTEST_USE_OWN_TR1_TUPLE=0)

# Equivalent to enable_testing()
# include(CTest)

file(GLOB_RECURSE SOURCES "${CMAKE_SOURCE_DIR}/src/main/*.cpp")
add_library(rows STATIC ${SOURCES})
# target_compile_definitions(rows PUBLIC )
# set(CMAKE_LINK_LIBRARY_FLAG "pthread ")

target_include_directories(rows PUBLIC ${OSRM_INCLUDE_DIR} ${NESTED_OSRM_INCLUDE_DIR} ${Boost_INCLUDE_DIR})
set_property(TARGET rows PROPERTY CXX_STANDARD 14)
# set_source_files_properties(${SOURCES} PROPERTIES COMPILE_FLAGS "-Wno-unused-variable -Wno-deprecated")

if(${external_project_dependencies})
    add_dependencies(rows ${external_project_dependencies})
endif()

add_executable(rows-main "${CMAKE_SOURCE_DIR}/src/main/main.cpp")
target_include_directories(rows-main PUBLIC ${OSRM_INCLUDE_DIR} ${NESTED_OSRM_INCLUDE_DIR} ${Boost_INCLUDE_DIR})
target_link_libraries(rows-main rows ${OSRM_LIBRARY} ${Boost_LIBRARIES} ${ZLIB_LIBRARIES} ${EXPAT_LIBRARIES} ${CMAKE_THREAD_LIBS_INIT} ${MAYBE_RT_LIBRARY})
set_property(TARGET rows-main PROPERTY CXX_STANDARD 14)
add_dependencies(rows-main rows)