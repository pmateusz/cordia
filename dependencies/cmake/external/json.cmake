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

if (NOT __JSON_INCLUDED)
    set(__JSON_INCLUDED TRUE)

    find_package(Json)
    if (JSON_FOUND)
        set(JSON_EXTERNAL FALSE)
    else ()
        # build directory
        set(json_PREFIX ${CMAKE_BINARY_DIR}/external/json-prefix)
        # install directory
        set(json_INSTALL ${CMAKE_BINARY_DIR}/external/json-install)

        ExternalProject_Add(json-external
                PREFIX ${json_PREFIX}
                URL_HASH SHA256=63da6d1f22b2a7bb9e4ff7d6b255cf691a161ff49532dcc45d398a53e295835f
                URL "https://github.com/nlohmann/json/releases/download/v3.4.0/json.hpp"
                INSTALL_DIR ${json_INSTALL}
                BUILD_COMMAND ""
                PATCH_COMMAND ""
                UPDATE_COMMAND ""
                CONFIGURE_COMMAND ""
                INSTALL_COMMAND mkdir -p ${json_INSTALL}/include/nlohmann &&
                ${CMAKE_COMMAND} -E copy ${json_PREFIX}/src/json.hpp ${json_INSTALL}/include/nlohmann/
                DOWNLOAD_NO_EXTRACT 1)

        set(JSON_FOUND TRUE)
        set(JSON_INCLUDE_DIRS ${json_INSTALL}/include)
        set(JSON_INCLUDE_DIR ${JSON_INCLUDE_DIRS})
        set(JSON_EXTERNAL TRUE)
        set_property(GLOBAL APPEND PROPERTY EXTERNAL_PROJECT_DEPENDENCIES json-external)
    endif ()

endif ()