# - Try to find GFLAGS
#
# The following variables are optionally searched for defaults
#  GFLAGS_ROOT_DIR:            Base directory where all GFLAGS components are found
#
# The following are set after configuration is done:
#  GFLAGS_FOUND
#  GFLAGS_INCLUDE_DIRS
#  GFLAGS_LIBRARIES
#  GFLAGS_LIBRARYRARY_DIRS

if (NOT GFLAGS_ROOT_DIR)
    set(GFLAGS_ROOT_DIR "" CACHE PATH "Folder contains Gflags")
endif ()

if (GFLAGS_ROOT_DIR)
    set(_GFLAGS_INCLUDE_LOCATIONS "${GFLAGS_ROOT_DIR}/include")
    set(_GFLAGS_LIB_LOCATIONS "${GFLAGS_ROOT_DIR}" "${GFLAGS_ROOT_DIR}/lib")
else ()
    set(_GFLAGS_INCLUDE_LOCATIONS "")
    set(_GFLAGS_LIB_LOCATIONS "")
endif ()

find_package(PkgConfig QUIET)
if (PKG_CONFIG_FOUND)
    pkg_check_modules(GFLAGS_PKGCONF QUIET gflags)
endif ()

# We are testing only a couple of files in the include directories
if (WIN32)
    find_path(GFLAGS_INCLUDE_DIR gflags/gflags.h
            PATHS ${GFLAGS_ROOT_DIR}/src/windows)
else ()
    find_path(GFLAGS_INCLUDE_DIR gflags/gflags.h
            HINTS "${_GFLAGS_INCLUDE_LOCATIONS}"
            PATHS "${GFLAGS_PKGCONF_INCLUDE_DIRS}" "/usr/local/include")
endif ()

if (MSVC)
    find_library(GFLAGS_LIBRARY_RELEASE
            NAMES libgflags
            PATHS ${GFLAGS_ROOT_DIR}
            PATH_SUFFIXES Release)

    find_library(GFLAGS_LIBRARY_DEBUG
            NAMES libgflags-debug
            PATHS ${GFLAGS_ROOT_DIR}
            PATH_SUFFIXES Debug)

    set(GFLAGS_LIBRARY optimized ${GFLAGS_LIBRARY_RELEASE} debug ${GFLAGS_LIBRARY_DEBUG})
else ()
    find_library(GFLAGS_LIBRARY
            NAMES gflags gflags_nothreads
            HINTS ${_GFLAGS_LIB_LOCATIONS}
            PATHS "${GFLAGS_PKGCONF_LIBRARY_DIRS}" "/usr/local/lib")
endif ()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(GFLAGS DEFAULT_MSG GFLAGS_INCLUDE_DIR GFLAGS_LIBRARY)

if (GFLAGS_FOUND)
    set(GFLAGS_INCLUDE_DIRS ${GFLAGS_INCLUDE_DIR})
    set(GFLAGS_LIBRARIES ${GFLAGS_LIBRARY})
endif ()

mark_as_advanced(GFLAGS_LIBRARY_DEBUG GFLAGS_LIBRARY_RELEASE _GFLAGS_INCLUDE_LOCATIONS _GFLAGS_LIB_LOCATIONS)