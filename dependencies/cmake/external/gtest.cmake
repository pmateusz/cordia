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


if (NOT __GOOGLETEST_INCLUDED)
    set(__GOOGLETEST_INCLUDED TRUE)

    # try the system-wide glog first
    find_package(GTest)
    if (GTEST_FOUND)
        set(GTEST_EXTERNAL FALSE)
    else ()
        # fetch and build glog from github

        # build directory
        set(gtest_PREFIX ${CMAKE_BINARY_DIR}/external/gtest-prefix)
        # install directory
        set(gtest_INSTALL ${CMAKE_BINARY_DIR}/external/gtest-install)

        # we build glog statically
        if (UNIX)
            set(GTEST_EXTRA_COMPILER_FLAGS "-fPIC")
        endif ()

        set(GTEST_CXX_FLAGS ${CMAKE_CXX_FLAGS} ${GTEST_EXTRA_COMPILER_FLAGS})
        set(GTEST_C_FLAGS ${CMAKE_C_FLAGS} ${GTEST_EXTRA_COMPILER_FLAGS})

        ExternalProject_Add(gtest-external
                PREFIX ${gtest_PREFIX}
                URL https://github.com/google/googletest/archive/release-1.8.1.zip
                UPDATE_COMMAND ""
                INSTALL_DIR ${gtest_INSTALL}
                CMAKE_ARGS -DCMAKE_BUILD_TYPE=RELEASE
                -DCMAKE_INSTALL_PREFIX=${gtest_INSTALL}
                -DBUILD_SHARED_LIBS=OFF
                -DBUILD_STATIC_LIBS=ON
                -DBUILD_TESTING=OFF
                -DBUILD_NC_TESTS=OFF
                -DBUILD_PACKAGING=OFF
                -DBUILD_CONFIG_TESTS=OFF
                -DINSTALL_HEADERS=ON
                -DCMAKE_C_FLAGS=${GTEST_C_FLAGS}
                -DCMAKE_CXX_FLAGS=${GTEST_CXX_FLAGS}
                LOG_DOWNLOAD 1
                LOG_CONFIGURE 1
                LOG_INSTALL 1
                )

        set(GTEST_FOUND TRUE)
        set(GTEST_INCLUDE_DIRS ${gtest_INSTALL}/include)
        set(GTEST_INCLUDE_DIR ${GTEST_INCLUDE_DIRS})
        set(GTEST_LIBRARIES ${gtest_INSTALL}/lib/libgtest.a ${gtest_INSTALL}/lib/libgmock.a)
        set(GTEST_MAIN_LIBRARIES ${gtest_INSTALL}/lib/libgtest_main.a ${gtest_INSTALL}/lib/libgmock_main.a)
        set(GTEST_BOTH_LIBRARIES ${GTEST_LIBRARIES} ${GTEST_MAIN_LIBRARIES})
        set(GTEST_LIBRARY_DIRS ${gtest_INSTALL}/lib)
        set(GTEST_EXTERNAL TRUE)
        set_property(GLOBAL APPEND PROPERTY EXTERNAL_PROJECT_DEPENDENCIES gtest-external)
    endif ()

endif ()