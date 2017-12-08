if (NOT __GEXF_INCLUDED) # guard against multiple includes
    set(__GEXF_INCLUDED TRUE)

    # use the system-wide gflags if present
    find_package(Gexf)
    if (GEXF_FOUND)
        set(GEXF_EXTERNAL FALSE)
    else()
        # build directory
        set(gexf_PREFIX ${CMAKE_BINARY_DIR}/external/gexf-prefix)
        # install directory
        set(gexf_INSTALL ${CMAKE_BINARY_DIR}/external/gexf-install)

        # we build gflags statically
        if (UNIX)
            set(GFLAGS_EXTRA_COMPILER_FLAGS "-fPIC")
        endif()

        ExternalProject_Add(gexf
                PREFIX ${gexf_PREFIX}
                GIT_REPOSITORY "https://github.com/pmateusz/libgexf.git"
                UPDATE_COMMAND ""
                INSTALL_DIR ${gexf_INSTALL}
                CMAKE_ARGS -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
                -DCMAKE_INSTALL_PREFIX=${gexf_INSTALL}
                -DBUILD_SHARED_LIBS=OFF
                -BUILD_CONFIG_TESTS=OFF
                -DCMAKE_CXX_FLAGS=${CMAKE_CXX_FLAGS}
                LOG_DOWNLOAD 1
                LOG_INSTALL 1)

        set(GEXF_FOUND TRUE)
        set(GEXF_ROOT_DIR ${gexf_INSTALL})
        set(GEXF_INCLUDE_DIRS ${gexf_INSTALL}/include)
        set(GEXF_INCLUDE_DIR ${gexf_INSTALL}/include)
        set(GEXF_LIBRARIES ${gexf_INSTALL}/lib/libgexf.a ${CMAKE_THREAD_LIBS_INIT})
        set(GEXF_LIBRARY ${GEXF_LIBRARIES})
        set(GEXF_LIBRARY_DIRS ${gexf_INSTALL}/lib)
        set(GEXF_EXTERNAL TRUE)

        list(APPEND external_project_dependencies gexf)
    endif()

endif()