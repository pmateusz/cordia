if (NOT __SPARSEHASH_INCLUDED) # guard against multiple includes
    set(__SPARSEHASH_INCLUDED TRUE)

    # use the system-wide gflags if present
    find_package(Sparsehash)
    if (SPARSEHASH_FOUND)
        set(SPARSEHASH_EXTERNAL FALSE)
    else()
        # build directory
        set(sparsehash_PREFIX ${CMAKE_BINARY_DIR}/external/sparsehash-prefix)
        # install directory
        set(sparsehash_INSTALL ${CMAKE_BINARY_DIR}/external/sparsehash-install)

        # we build gflags statically
        if (UNIX)
            set(SPARSEHASH_EXTRA_COMPILER_FLAGS -fPIC)
        endif()

        set(SPARSEHASH_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${SPARSEHASH_EXTRA_COMPILER_FLAGS}")
        set(SPARSEHASH_C_FLAGS "${CMAKE_C_FLAGS} ${SPARSEHASH_EXTRA_COMPILER_FLAGS}")

        ExternalProject_Add(sparsehash
                PREFIX ${sparsehash_PREFIX}
                GIT_REPOSITORY "https://github.com/sparsehash/sparsehash"
                GIT_TAG "sparsehash-2.0.3"
                UPDATE_COMMAND ""
                INSTALL_DIR ${sparsehash_INSTALL}
                PATCH_COMMAND autoreconf -i ${sparsehash_PREFIX}/src/sparsehash
                CONFIGURE_COMMAND env "CFLAGS=${SPARSEHASH_C_FLAGS}" "CXXFLAGS=${SPARSEHASH_CXX_FLAGS}" ${sparsehash_PREFIX}/src/sparsehash/configure --prefix=${sparsehash_INSTALL} --enable-shared=no --enable-static=yes
                LOG_DOWNLOAD 1
                LOG_INSTALL 1)

        set(SPARSEHASH_FOUND TRUE)
        set(SPARSEHASH_INCLUDE_DIRS ${sparsehash_INSTALL}/include)
        set(SPARSEHASH_INCLUDE_DIR ${SPARSEHASH_INCLUDE_DIRS})
        set(SPARSEHASH_LIBRARIES ${sparsehash_INSTALL}/lib/libsparsehash.a)
        set(SPARSEHASH_LIBRARY_DIRS ${sparsehash_INSTALL}/lib)
        set(SPARSEHASH_EXTERNAL TRUE)

        list(APPEND external_project_dependencies sparsehash)
    endif()

endif()
