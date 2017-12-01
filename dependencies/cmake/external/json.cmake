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

        ExternalProject_Add(json
                PREFIX ${json_PREFIX}
                URL_HASH SHA256=faa2321beb1aa7416d035e7417fcfa59692ac3d8c202728f9bcc302e2d558f57
                URL "https://github.com/nlohmann/json/releases/download/v2.1.1/json.hpp"
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

        list(APPEND external_project_dependencies json)
    endif ()

endif ()