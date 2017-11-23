if (NOT GUROBI_ROOT_DIR)
    set(GUROBI_ROOT_DIR "" CACHE PATH "Folder contains Gurobi Optimizer")
endif ()

if (GUROBI_ROOT_DIR)
    set(_GUROBI_INCLUDE_LOCATIONS "${GUROBI_ROOT_DIR}/include")
    set(_GUROBI_LIB_LOCATIONS "${GUROBI_ROOT_DIR}/lib")
else ()
    set(_GUROBI_INCLUDE_LOCATIONS "")
    set(_GUROBI_LIB_LOCATIONS "")
endif ()

find_package(PkgConfig QUIET)
if (PKG_CONFIG_FOUND)
    pkg_check_modules(GUROBI_PKGCONF QUIET Gurobi)
endif ()

find_path(GUROBI_INCLUDE_DIR gurobi_c++.h
        HINTS "${_GUROBI_INCLUDE_LOCATIONS}"
        PATHS "${GUROBI_PKGCONF_INCLUDE_DIRS}" "/usr/local/include")

find_library(GUROBI_LIBRARY gurobi
        HINTS "${_GUROBI_LIB_LOCATIONS}"
        PATHS "${GUROBI_PKGCONF_LIBRARY_DIRS}" "/usr/local/lib"
        PATH_SUFFIXES lib)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(GUROBI DEFAULT_MSG GUROBI_INCLUDE_DIR GUROBI_LIBRARY)