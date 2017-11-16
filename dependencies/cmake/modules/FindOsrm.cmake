if (NOT OSRM_ROOT_DIR)
    set(OSRM_ROOT_DIR "" CACHE PATH "Folder contains the OSRM backend library")
endif ()

if (OSRM_ROOT_DIR)
    set(_OSRM_INCLUDE_LOCATIONS "${OSRM_ROOT_DIR}/include")
    set(_OSRM_LIB_LOCATIONS "${OSRM_ROOT_DIR}/lib")
else ()
    set(_OSRM_INCLUDE_LOCATIONS "")
    set(_OSRM_LIB_LOCATIONS "")
endif ()

find_package(PkgConfig QUIET)
if (PKG_CONFIG_FOUND)
    pkg_check_modules(OSRM_PKGCONF QUIET Osrm)
endif ()

find_path(OSRM_INCLUDE_DIR tsl/htrie_hash.h
        HINTS "${_OSRM_INCLUDE_LOCATIONS}"
        PATHS "${OSRM_PKGCONF_INCLUDE_DIRS}" "/usr/local/include")

find_library(OSRM_LIBRARY osrm
        HINTS "${_OSRM_LIB_LOCATIONS}"
        PATHS "${OSRM_PKGCONF_LIBRARY_DIRS}" "/usr/local/lib"
        PATH_SUFFIXES lib)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(OSRM DEFAULT_MSG OSRM_INCLUDE_DIR OSRM_LIBRARY)