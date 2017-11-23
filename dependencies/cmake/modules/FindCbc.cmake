if (NOT CBC_ROOT_DIR)
    set(CBC_ROOT_DIR "" CACHE PATH "Folder contains Coin-or branch and cut")
endif ()

if (CBC_ROOT_DIR)
    set(_CBC_INCLUDE_LOCATIONS "${CBC_ROOT_DIR}/include")
    set(_CBC_LIB_LOCATIONS "${CBC_ROOT_DIR}/lib")
else ()
    set(_CBC_INCLUDE_LOCATIONS "")
    set(_CBC_LIB_LOCATIONS "")
endif ()

find_package(PkgConfig QUIET)
if (PKG_CONFIG_FOUND)
    pkg_check_modules(CBC_PKGCONF QUIET Cbc)
endif ()

find_path(CBC_INCLUDE_DIR coin/CbcConfig.h
        HINTS "${_CBC_INCLUDE_LOCATIONS}"
        PATHS "${CBC_PKGCONF_INCLUDE_DIRS}" "/usr/local/include")

find_library(CBC_LIBRARY Cbc
        HINTS "${_CBC_LIB_LOCATIONS}"
        PATHS "${CBC_PKGCONF_LIBRARY_DIRS}" "/usr/local/lib"
        PATH_SUFFIXES lib)

find_library(CBC_SOLVER_LIBRARY CbcSolver
        HINTS "${_CBC_LIB_LOCATIONS}"
        PATHS "${CBC_PKGCONF_LIBRARY_DIRS}" "/usr/local/lib"
        PATH_SUFFIXES lib)
list(APPEND CBC_LIBRARY ${CBC_SOLVER_LIBRARY})

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(CBC DEFAULT_MSG CBC_INCLUDE_DIR CBC_LIBRARY)