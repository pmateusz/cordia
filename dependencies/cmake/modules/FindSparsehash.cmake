# - Find google-sparsehash
# Find the google-sparsehash includes
# This module defines
# SPARSEHASH_INCLUDE_DIR

if (NOT SPARSEHASH_ROOT_DIR)
    set(SPARSEHASH_ROOT_DIR "" CACHE PATH "Folder contains SparseHash")
endif ()

if (SPARSEHASH_ROOT_DIR)
    set(_SPARSEHASH_INCLUDE_LOCATIONS "${SPARSEHASH_ROOT_DIR}/include")
else ()
    set(_SPARSEHASH_INCLUDE_LOCATIONS "")
endif ()

find_package(PkgConfig QUIET)
if (PKGCONFIG_FOUND)
    pkg_check_modules(PKG_SPARSEHASH QUIET libsparsehash)
endif ()

find_path(SPARSEHASH_INCLUDE_DIR
        NAMES google/sparsetable
        HINTS "${_SPARSEHASH_INCLUDE_LOCATIONS}"
        PATHS
        ${PKG_SPARSEHASH_INCLUDEDIR}
        ${PKG_SPARSEHASH_INCLUDE_DIRS}
        ${CMAKE_INSTALL_INCLUDEDIR})

include(FindPackageHandleStandardArgs)
if (SPARSEHASH_INCLUDE_DIR AND NOT PKG_SPARSEHASH_VERSION)
    find_package_handle_standard_args(SPARSEHASH
            REQUIRED_VARS SPARSEHASH_INCLUDE_DIR)
else ()
    find_package_handle_standard_args(SPARSEHASH
            REQUIRED_VARS SPARSEHASH_INCLUDE_DIR
            VERSION_VAR PKG_SPARSEHASH_VERSION)
endif ()

mark_as_advanced(SPARSEHASH_INCLUDE_DIR _SPARSEHASH_INCLUDE_LOCATIONS)