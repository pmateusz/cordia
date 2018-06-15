# - Try to find GLog
#
# The following variables are optionally searched for defaults
#  GLOG_ROOT_DIR:            Base directory where all GLOG components are found
#
# The following are set after configuration is done:
#  GLOG_FOUND
#  GLOG_INCLUDE_DIRS
#  GLOG_LIBRARIES
#  GLOG_LIBRARYRARY_DIRS

if (NOT GLOG_ROOT_DIR)
    set(GLOG_ROOT_DIR "" CACHE PATH "Folder contains Google glog")
endif ()

if (WIN32)
    find_path(GLOG_INCLUDE_DIR glog/logging.h
            HINTS ${GLOG_ROOT_DIR}/src/windows)
else ()
    find_path(GLOG_INCLUDE_DIR glog/logging.h
            HINTS ${GLOG_ROOT_DIR}/include)
endif ()

if (MSVC)
    find_library(GLOG_LIBRARY_RELEASE libglog_static
            PATHS ${GLOG_ROOT_DIR}
            PATH_SUFFIXES Release)

    find_library(GLOG_LIBRARY_DEBUG libglog_static
            PATHS ${GLOG_ROOT_DIR}
            PATH_SUFFIXES Debug)

    set(GLOG_LIBRARY optimized ${GLOG_LIBRARY_RELEASE} debug ${GLOG_LIBRARY_DEBUG})
else ()
    find_library(GLOG_LIBRARY glog glogd
            HINTS ${GLOG_ROOT_DIR}
            PATH_SUFFIXES lib lib64)
endif ()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(GLOG DEFAULT_MSG GLOG_INCLUDE_DIR GLOG_LIBRARY)

if (GLOG_FOUND)
    set(GLOG_INCLUDE_DIRS ${GLOG_INCLUDE_DIR})
    set(GLOG_LIBRARIES ${GLOG_LIBRARY})
endif ()
mark_as_advanced(GLOG_ROOT_DIR GLOG_LIBRARY_RELEASE GLOG_LIBRARY_DEBUG GLOG_LIBRARY GLOG_INCLUDE_DIR)