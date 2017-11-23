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

        ExternalProject_Add(gtest
         PREFIX ${gtest_PREFIX}
         URL https://github.com/google/googletest/archive/release-1.8.0.zip
         URL_HASH SHA1=667f873ab7a4d246062565fad32fb6d8e203ee73
         UPDATE_COMMAND ""
         INSTALL_DIR ${gtest_INSTALL}
         CMAKE_ARGS -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
                    -DCMAKE_INSTALL_PREFIX=${gtest_INSTALL}
                    -DBUILD_SHARED_LIBS=OFF
                    -DBUILD_STATIC_LIBS=ON
                    -DBUILD_TESTING=OFF
                    -DBUILD_NC_TESTS=OFF
                    -DBUILD_PACKAGING=OFF
                    -BUILD_CONFIG_TESTS=OFF
                    -DINSTALL_HEADERS=ON
                    -DCMAKE_C_FLAGS=${GTEST_C_FLAGS}
                    -DCMAKE_CXX_FLAGS=${GTEST_CXX_FLAGS}
         LOG_DOWNLOAD 1
         LOG_CONFIGURE 1
         LOG_INSTALL 1
         )

        set(GTEST_FOUND TRUE)
        set(GTEST_INCLUDE_DIRS ${gtest_INSTALL}/include)
        set(GTEST_LIBRARIES ${gtest_INSTALL}/lib/libgtest.a ${gtest_INSTALL}/lib/libgmock.a)
        set(GTEST_MAIN_LIBRARIES ${gtest_INSTALL}/lib/libgtest_main.a ${gtest_INSTALL}/lib/libgmock_main.a)
        set(GTEST_BOTH_LIBRARIES ${GTEST_LIBRARIES} ${GTEST_MAIN_LIBRARIES})
        set(GTEST_LIBRARY_DIRS ${gtest_INSTALL}/lib)
        set(GTEST_EXTERNAL TRUE)

        list(APPEND external_project_dependencies gtest)
    endif ()

endif ()