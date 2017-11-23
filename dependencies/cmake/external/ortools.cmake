if (NOT __ORTOOLS_INCLUDED)
    set(__ORTOOLS_INCLUDED TRUE)

    find_package(ORtools)
    if (ORTOOLS_FOUND)
        set(ORTOOLS_EXTERNAL FALSE)
    else ()
        # build directory
        set(ortools_PREFIX ${CMAKE_BINARY_DIR}/external/ortools-prefix)
        # install directory
        set(ortools_INSTALL ${CMAKE_BINARY_DIR}/external/ortools-install)

        # Expose ortools dependencies
        include("cmake/External/gflags.cmake")

        # depend on gflags if we're also building it
        if(GFLAGS_EXTERNAL)
            list(APPEND ORTOOLS_DEPENDS gflags)
        endif()

        # ortools depends on sparsehash
        include("cmake/External/sparsehash.cmake")

        # depend on gflags if we're also building it
        if(SPARSEHASH_EXTERNAL)
            list(APPEND ORTOOLS_DEPENDS sparsehash)
        endif()

        ExternalProject_Add(ortools
                DEPENDS ${ORTOOLS_DEPENDS}
                PREFIX ${ortools_PREFIX}
                URL https://github.com/google/or-tools/archive/v5.1.zip
                URL_HASH SHA1=b7aedcc4cf183792d401af54d78ec17b75c94a7f
                INSTALL_DIR ${ortools_INSTALL}
                PATCH_COMMAND ""
                UPDATE_COMMAND ""
                CONFIGURE_COMMAND ""
                BUILD_COMMAND ${CMAKE_SOURCE_DIR}/cmake/Scripts/build_ortools.py
                INSTALL_COMMAND ""
                BUILD_IN_SOURCE 1)

        set(ORTOOLS_FOUND TRUE)
        set(ORTOOLS_INCLUDE_DIRS ${ortools_INSTALL}/include)
        set(ORTOOLS_LIBRARIES ${GFLAGS_LIBRARIES} ${SPARSEHASH_LIBRARIES} ${ortools_INSTALL}/lib/libortools.a)
        set(ORTOOLS_LIBRARY_DIRS ${ortools_INSTALL}/lib)
        set(ORTOOLS_EXTERNAL TRUE)

        list(APPEND external_project_dependencies ortools)
    endif ()

endif ()