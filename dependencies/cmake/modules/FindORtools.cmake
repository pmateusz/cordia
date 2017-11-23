if (NOT ORTOOLS_ROOT_DIR)
    set(ORTOOLS_ROOT_DIR "" CACHE PATH "Folder contains OR Tools")
endif ()

find_package(PkgConfig QUIET)
if (PKG_CONFIG_FOUND)
    pkg_check_modules(ORTOOLS_PKGCONF QUIET ortools)
endif ()

set(_ORTOOLS_INCLUDE_LOCATIONS "")
set(_ORTOOLS_LIB_LOCATIONS "")

if (ORTOOLS_ROOT_DIR)
    set(_ORTOOLS_INCLUDE_LOCATIONS "${ORTOOLS_ROOT_DIR}/src")
    set(_ORTOOLS_INCLUDE_GEN_LOCATIONS "${_ORTOOLS_INCLUDE_LOCATIONS}/gen")
    set(_ORTOOLS_LIB_LOCATIONS "${ORTOOLS_ROOT_DIR}/lib")

    set(CBC_ROOT_DIR "${ORTOOLS_ROOT_DIR}/dependencies/install")
    set(GFLAGS_ROOT_DIR "${ORTOOLS_ROOT_DIR}/dependencies/install")
    set(SPARSEHASH_ROOT_DIR "${ORTOOLS_ROOT_DIR}/dependencies/install")
    set(Protobuf_SRC_ROOT_FOLDER "${ORTOOLS_ROOT_DIR}/dependencies/sources/protobuf-3.0.0/src")
endif ()

find_path(ORTOOLS_INCLUDE_DIR
        NAMES linear_solver/linear_solver.h
        HINTS "${_ORTOOLS_INCLUDE_LOCATIONS}"
        PATHS ${ORTOOLS_PKGCONF_INCLUDE_DIRS} "/usr/local/include/ortools")

find_path(ORTOOLS_INCLUDE_GEN_DIR
        NAMES linear_solver/linear_solver.pb.h
        HINTS "${_ORTOOLS_INCLUDE_GEN_LOCATIONS}"
        PATHS ${ORTOOLS_PKGCONF_INCLUDE_DIRS} "/usr/local/include/ortools")

if (ORTOOLS_INCLUDE_GEN_DIR AND NOT (ORTOOLS_INCLUDE_GEN_DIR EQUAL ORTOOLS_INCLUDE_DIR))
    list(APPEND ORTOOLS_INCLUDE_DIR ${ORTOOLS_INCLUDE_GEN_DIR})
endif ()

find_library(ORTOOLS_LIBRARY
        NAMES ortools
        HINTS "${_ORTOOLS_LIB_LOCATIONS}"
        PATHS ${ORTOOLS_PKGCONF_LIBRARY_DIRS} "/usr/local/lib/ortools")

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(ORTOOLS DEFAULT_MSG ORTOOLS_INCLUDE_DIR ORTOOLS_LIBRARY)

# define ARCH_K8 on x64 architectures
# used in or-tools headers
if (CMAKE_SIZEOF_VOID_P EQUAL 8)
    add_definitions(-DARCH_K8)
endif ()

# ORtools depednencies
if (ORTOOLS_FOUND)
    # gflags should have 2.1.2 exact version cannot check it at the moment
    find_package(GFlags REQUIRED)
    # find sparsehash 2.0.3 header files
    find_package(Sparsehash REQUIRED)
    find_package(Protobuf 3.0.0 REQUIRED)
    find_package(Cbc REQUIRED)

    get_filename_component(UNIX_GFLAGS_DIR ${GFLAGS_INCLUDE_DIR} DIRECTORY)
    get_filename_component(UNIX_PROTOBUF_DIR ${PROTOBUF_INCLUDE_DIR} DIRECTORY)
    get_filename_component(UNIX_SPARSEHASH_DIR ${SPARSEHASH_INCLUDE_DIR} DIRECTORY)
    get_filename_component(UNIX_CBC_DIR ${CBC_INCLUDE_DIR} DIRECTORY)

    set(ORTOOLS_COMPILER_DEFINITIONS "")
    list(APPEND ORTOOLS_COMPILER_DEFINITIONS UNIX_GFLAGS_DIR=${UNIX_GFLAGS_DIR})
    list(APPEND ORTOOLS_COMPILER_DEFINITIONS UNIX_PROTOBUF_DIR=${UNIX_PROTOBUF_DIR})
    list(APPEND ORTOOLS_COMPILER_DEFINITIONS UNIX_SPARSEHASH_DIR=${UNIX_SPARSEHASH_DIR})
    list(APPEND ORTOOLS_COMPILER_DEFINITIONS UNIX_CBC_DIR=${UNIX_CBC_DIR})

    find_package(Gurobi QUIET)

    if(Gurobi_FOUND)
        if(UNIX)
            list(APPEND ORTOOLS_COMPILER_DEFINITIONS GUROBI_PLATFORM=linux64)
            list(APPEND ORTOOLS_COMPILER_DEFINITIONS UNIX_GUROBI_DIR=/opt/gurobi751)
        endif()

        list(APPEND ORTOOLS_COMPILER_DEFINITIONS USE_GUROBI)
    endif()

    list(APPEND ORTOOLS_COMPILER_DEFINITIONS USE_CLP)
    list(APPEND ORTOOLS_COMPILER_DEFINITIONS USE_GLOP)
    list(APPEND ORTOOLS_COMPILER_DEFINITIONS USE_CBC)

    include_directories(${GFLAGS_INCLUDE_DIR} ${Protobuf_INCLUDE_DIR} ${SPARSEHASH_INCLUDE_DIR})
endif ()

mark_as_advanced(_ORTOOLS_INCLUDE_LOCATIONS _ORTOOLS_INCLUDE_GEN_LOCATIONS _ORTOOLS_LIB_LOCATIONS)