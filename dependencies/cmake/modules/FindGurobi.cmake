#
# Copyright 2018 Mateusz Polnik
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in all
# copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.

if (NOT GUROBI_ROOT_DIR)
    set(GUROBI_ROOT_DIR "" CACHE PATH "Folder contains Gurobi library")
endif ()

if (GUROBI_ROOT_DIR)
    set(_GUROBI_INCLUDE_LOCATIONS "${GUROBI_ROOT_DIR}/linux64/include")
    set(_GUROBI_LIB_LOCATIONS "${GUROBI_ROOT_DIR}/linux64/lib")
else ()
    set(_GUROBI_INCLUDE_LOCATIONS "")
    set(_GUROBI_LIB_LOCATIONS "")
endif ()

find_path(GUROBI_INCLUDE_DIR gurobi_c++.h
        HINTS "${_GUROBI_INCLUDE_LOCATIONS}"
        PATHS "/usr/local/include")

find_library(GUROBI_LIBRARY gurobi gurobi81
        HINTS  "${_GUROBI_LIB_LOCATIONS}"
        PATHS "/usr/local/lib/gurobi"
        PATH_SUFFIXES lib)

find_library(GUROBI_STATIC_LIBRARY gurobi_g++5.2
        HINTS  "${_GUROBI_LIB_LOCATIONS}"
        PATHS "/usr/local/lib/gurobi"
        PATH_SUFFIXES lib)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(GUROBI DEFAULT_MSG GUROBI_INCLUDE_DIR GUROBI_STATIC_LIBRARY GUROBI_LIBRARY)