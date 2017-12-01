# - Try to find JSON
#
# The following variables are optionally searched for defaults
#  JSON_ROOT_DIR:            Base directory where all JSON components are found
#
# The following are set after configuration is done:
#  JSON_FOUND
#  JSON_INCLUDE_DIRS
#  JSON_LIBRARIES

if (NOT JSON_ROOT_DIR)
    set(JSON_ROOT_DIR "" CACHE PATH "Folder contains JSON for modern C++")
endif ()

if (JSON_ROOT_DIR)
    set(_JSON_INCLUDE_LOCATIONS "${JSON_ROOT_DIR}/include")
else ()
    set(_JSON_INCLUDE_LOCATIONS "")
endif ()

find_package(PkgConfig QUIET)
if (PKG_CONFIG_FOUND)
    pkg_check_modules(JSON_PKGCONF QUIET json)
endif ()

# We are testing only a couple of files in the include directories
find_path(JSON_INCLUDE_DIR nlohmann/json.hpp
        HINTS "${_JSON_INCLUDE_LOCATIONS}"
        PATHS "${JSON_PKGCONF_INCLUDE_DIRS}" "/usr/local/include")

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(JSON DEFAULT_MSG JSON_INCLUDE_DIR)

if (JSON_FOUND)
    set(JSON_INCLUDE_DIR ${JSON_INCLUDE_DIR})
endif ()

mark_as_advanced(JSON_LIBRARY_DEBUG JSON_LIBRARY_RELEASE _JSON_INCLUDE_LOCATIONS)