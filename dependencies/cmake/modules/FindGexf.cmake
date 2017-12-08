if (NOT GEXF_ROOT_DIR)
    set(GEXF_ROOT_DIR "" CACHE PATH "Folder contains GEXF library")
endif ()

if (GEXF_ROOT_DIR)
    set(_GEXF_INCLUDE_LOCATIONS "${GEXF_ROOT_DIR}/include")
    set(_GEXF_LIB_LOCATIONS "${GEXF_ROOT_DIR}/lib")
else ()
    set(_GEXF_INCLUDE_LOCATIONS "")
    set(_GEXF_LIB_LOCATIONS "")
endif ()

find_package(PkgConfig QUIET)
if (PKG_CONFIG_FOUND)
    pkg_check_modules(GEXF_PKGCONF QUIET Gexf)
endif ()

find_path(GEXF_INCLUDE_DIR libgexf/gexf.h
        HINTS "${_GEXF_INCLUDE_LOCATIONS}"
        PATHS "${GEXF_PKGCONF_INCLUDE_DIRS}" "/usr/local/include")

find_library(GEXF_LIBRARY gexf
        HINTS "${_GEXF_LIB_LOCATIONS}"
        PATHS "${GEXF_PKGCONF_LIBRARY_DIRS}" "/usr/local/lib"
        PATH_SUFFIXES lib)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(GEXF DEFAULT_MSG GEXF_INCLUDE_DIR GEXF_LIBRARY)