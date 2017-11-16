if (NOT __OSRM_INCLUDED) # guard against multiple includes
    set(__OSRM_INCLUDED TRUE)

    # use the system-wide htrie if present
    find_package(Osrm)
    if (OSRM_FOUND)
        set(OSRM_EXTERNAL FALSE)
    else()
        # build directory
        set(osrm_PREFIX ${CMAKE_BINARY_DIR}/external/osrm-prefix)
        # install directory
        set(osrm_INSTALL ${CMAKE_BINARY_DIR}/external/osrm-install)

        ExternalProject_Add(osrm
                PREFIX ${osrm_PREFIX}
                URL https://github.com/Project-OSRM/osrm-backend/archive/v5.13.0.zip
                URL_HASH SHA1=1e585e9405fea3336518c86b2116779222915391
                INSTALL_COMMAND ""
                UPDATE_COMMAND ""
                INSTALL_DIR ${osrm_INSTALL}
                CMAKE_ARGS -DCMAKE_BUILD_TYPE=Release
                -DCMAKE_INSTALL_PREFIX=${osrm_INSTALL}
                -DCMAKE_CXX_FLAGS=${CMAKE_CXX_FLAGS}
                LOG_DOWNLOAD 1
                LOG_INSTALL 1)

        set(OSRM_FOUND TRUE)
        set(OSRM_ROOT_DIR ${osrm_INSTALL})
        set(OSRM_INCLUDE_DIRS ${osrm_INSTALL}/include)
        set(OSRM_LIBRARIES ${osrm_INSTALL}/lib/libosrm.a ${CMAKE_THREAD_LIBS_INIT})
        set(OSRM_LIBRARY_DIRS ${osmr_INSTALL}/lib)
        set(OSRM_EXTERNAL TRUE)

        list(APPEND external_project_dependencies osrm)
    endif()

endif()