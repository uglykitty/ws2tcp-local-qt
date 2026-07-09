cmake_minimum_required(VERSION 3.21)

project(ws2tcp-local-qt VERSION 0.1.0 LANGUAGES CXX)

include(GNUInstallDirs)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets)

option(WS2TCP_LOCAL_FFI_AUTO_BUILD
    "Build ws2tcp-local-ffi with Cargo when no prebuilt FFI library is provided"
    ON)
set(WS2TCP_LOCAL_FFI_SOURCE_DIR
    "${CMAKE_CURRENT_LIST_DIR}/../ws2tcp-local-ffi"
    CACHE PATH "Path to the ws2tcp-local-ffi Rust crate")
set(WS2TCP_LOCAL_FFI_CRATE_VERSION
    "${PROJECT_VERSION}"
    CACHE STRING "ws2tcp-local-ffi crate version to fetch from crates.io when the local crate is missing")
set(WS2TCP_LOCAL_FFI_INCLUDE_DIR
    "${CMAKE_CURRENT_LIST_DIR}/../ws2tcp-local-ffi/include"
    CACHE PATH "Path to ws2tcp-local FFI headers")
set(WS2TCP_LOCAL_FFI_TARGET_DIR
    ""
    CACHE PATH "Cargo target directory for ws2tcp-local-ffi auto-build artifacts")
set(WS2TCP_LOCAL_FFI_LIBRARY
    ""
    CACHE FILEPATH "Path to ws2tcp-local FFI runtime library (.so, .dylib, or .dll)")
set(WS2TCP_LOCAL_FFI_IMPLIB
    ""
    CACHE FILEPATH "Path to ws2tcp-local FFI import library on Windows")

function(ws2tcp_local_ffi_artifacts profile out_runtime out_implib)
    if(WS2TCP_LOCAL_FFI_TARGET_DIR)
        set(artifact_dir "${WS2TCP_LOCAL_FFI_TARGET_DIR}/${profile}")
    else()
        set(artifact_dir "${WS2TCP_LOCAL_FFI_SOURCE_DIR}/target/${profile}")
    endif()

    if(WIN32)
        set(runtime "${artifact_dir}/ws2tcp_local_ffi.dll")
        if(MSVC)
            set(implib "${artifact_dir}/ws2tcp_local_ffi.dll.lib")
        else()
            set(implib "${artifact_dir}/libws2tcp_local_ffi.dll.a")
        endif()
    elseif(APPLE)
        set(runtime "${artifact_dir}/libws2tcp_local_ffi.dylib")
        set(implib "")
    else()
        set(runtime "${artifact_dir}/libws2tcp_local_ffi.so")
        set(implib "")
    endif()

    set("${out_runtime}" "${runtime}" PARENT_SCOPE)
    set("${out_implib}" "${implib}" PARENT_SCOPE)
endfunction()

function(ws2tcp_local_ffi_resolve_cratesio out_source_dir out_include_dir)
    set(resolve_dir "${CMAKE_BINARY_DIR}/_deps/ws2tcp-local-ffi-cratesio")
    set(resolve_manifest "${resolve_dir}/Cargo.toml")

    file(MAKE_DIRECTORY "${resolve_dir}/src")
    file(WRITE "${resolve_dir}/src/lib.rs" "")
    file(WRITE "${resolve_manifest}"
"[package]
name = \"ws2tcp-local-ffi-resolver\"
version = \"0.0.0\"
edition = \"2024\"
publish = false

[dependencies]
ws2tcp-local-ffi = \"=${WS2TCP_LOCAL_FFI_CRATE_VERSION}\"
")

    execute_process(
        COMMAND
            "${CMAKE_COMMAND}" -E env
                "--unset=http_proxy"
                "--unset=https_proxy"
                "--unset=all_proxy"
                "--unset=HTTP_PROXY"
                "--unset=HTTPS_PROXY"
                "--unset=ALL_PROXY"
                "${CARGO_EXECUTABLE}" metadata
                --format-version=1
                --manifest-path "${resolve_manifest}"
        WORKING_DIRECTORY "${resolve_dir}"
        RESULT_VARIABLE cargo_metadata_result
        OUTPUT_VARIABLE cargo_metadata_output
        ERROR_VARIABLE cargo_metadata_error
        OUTPUT_STRIP_TRAILING_WHITESPACE)

    if(NOT cargo_metadata_result EQUAL 0)
        message(FATAL_ERROR
            "Failed to fetch ws2tcp-local-ffi ${WS2TCP_LOCAL_FFI_CRATE_VERSION} from crates.io.\n"
            "${cargo_metadata_error}")
    endif()

    string(JSON package_count LENGTH "${cargo_metadata_output}" packages)
    math(EXPR package_last "${package_count} - 1")
    set(resolved_manifest "")

    foreach(package_index RANGE 0 ${package_last})
        string(JSON package_name GET "${cargo_metadata_output}" packages ${package_index} name)
        string(JSON package_version GET "${cargo_metadata_output}" packages ${package_index} version)
        if(package_name STREQUAL "ws2tcp-local-ffi" AND
                package_version STREQUAL "${WS2TCP_LOCAL_FFI_CRATE_VERSION}")
            string(JSON resolved_manifest GET "${cargo_metadata_output}" packages ${package_index} manifest_path)
            break()
        endif()
    endforeach()

    if(NOT resolved_manifest)
        message(FATAL_ERROR
            "Cargo downloaded metadata, but ws2tcp-local-ffi ${WS2TCP_LOCAL_FFI_CRATE_VERSION} "
            "was not found in the resolved package list.")
    endif()

    get_filename_component(resolved_source_dir "${resolved_manifest}" DIRECTORY)
    set(resolved_include_dir "${resolved_source_dir}/include")

    if(NOT EXISTS "${resolved_include_dir}/ws2tcp_local_ffi.h")
        message(FATAL_ERROR
            "ws2tcp-local-ffi ${WS2TCP_LOCAL_FFI_CRATE_VERSION} was fetched, but its C header "
            "was not found at ${resolved_include_dir}/ws2tcp_local_ffi.h")
    endif()

    set("${out_source_dir}" "${resolved_source_dir}" PARENT_SCOPE)
    set("${out_include_dir}" "${resolved_include_dir}" PARENT_SCOPE)
endfunction()

if(NOT WS2TCP_LOCAL_FFI_LIBRARY)
    if(NOT WS2TCP_LOCAL_FFI_AUTO_BUILD)
        message(FATAL_ERROR "Set WS2TCP_LOCAL_FFI_LIBRARY or enable WS2TCP_LOCAL_FFI_AUTO_BUILD")
    endif()

    find_program(CARGO_EXECUTABLE cargo REQUIRED)

    if(NOT EXISTS "${WS2TCP_LOCAL_FFI_SOURCE_DIR}/Cargo.toml")
        message(STATUS
            "ws2tcp-local-ffi was not found at ${WS2TCP_LOCAL_FFI_SOURCE_DIR}; "
            "fetching ${WS2TCP_LOCAL_FFI_CRATE_VERSION} from crates.io")
        ws2tcp_local_ffi_resolve_cratesio(
            WS2TCP_LOCAL_FFI_RESOLVED_SOURCE_DIR
            WS2TCP_LOCAL_FFI_RESOLVED_INCLUDE_DIR)
        set(WS2TCP_LOCAL_FFI_SOURCE_DIR
            "${WS2TCP_LOCAL_FFI_RESOLVED_SOURCE_DIR}"
            CACHE PATH "Path to the ws2tcp-local-ffi Rust crate" FORCE)
        set(WS2TCP_LOCAL_FFI_INCLUDE_DIR
            "${WS2TCP_LOCAL_FFI_RESOLVED_INCLUDE_DIR}"
            CACHE PATH "Path to ws2tcp-local FFI headers" FORCE)
        set(WS2TCP_LOCAL_FFI_TARGET_DIR
            "${CMAKE_BINARY_DIR}/_deps/ws2tcp-local-ffi-target"
            CACHE PATH "Cargo target directory for ws2tcp-local-ffi auto-build artifacts" FORCE)
    elseif(NOT EXISTS "${WS2TCP_LOCAL_FFI_INCLUDE_DIR}/ws2tcp_local_ffi.h" AND
            EXISTS "${WS2TCP_LOCAL_FFI_SOURCE_DIR}/include/ws2tcp_local_ffi.h")
        set(WS2TCP_LOCAL_FFI_INCLUDE_DIR
            "${WS2TCP_LOCAL_FFI_SOURCE_DIR}/include"
            CACHE PATH "Path to ws2tcp-local FFI headers" FORCE)
    endif()

    if(NOT EXISTS "${WS2TCP_LOCAL_FFI_INCLUDE_DIR}/ws2tcp_local_ffi.h")
        message(FATAL_ERROR
            "ws2tcp-local FFI header was not found. Set WS2TCP_LOCAL_FFI_INCLUDE_DIR.")
    endif()

    set(WS2TCP_LOCAL_FFI_CARGO_ENV
        "--unset=http_proxy"
        "--unset=https_proxy"
        "--unset=all_proxy"
        "--unset=HTTP_PROXY"
        "--unset=HTTPS_PROXY"
        "--unset=ALL_PROXY")
    if(WS2TCP_LOCAL_FFI_TARGET_DIR)
        list(APPEND WS2TCP_LOCAL_FFI_CARGO_ENV
            "CARGO_TARGET_DIR=${WS2TCP_LOCAL_FFI_TARGET_DIR}")
    endif()

    add_custom_target(ws2tcp_local_ffi_cargo_build
        COMMAND
            "${CMAKE_COMMAND}" -E env
                ${WS2TCP_LOCAL_FFI_CARGO_ENV}
                "${CARGO_EXECUTABLE}" build
                "$<$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>,$<CONFIG:MinSizeRel>>:--release>"
        WORKING_DIRECTORY "${WS2TCP_LOCAL_FFI_SOURCE_DIR}"
        COMMENT "Building ws2tcp-local-ffi with Cargo")

    ws2tcp_local_ffi_artifacts(debug WS2TCP_LOCAL_FFI_DEBUG_LIBRARY WS2TCP_LOCAL_FFI_DEBUG_IMPLIB)
    ws2tcp_local_ffi_artifacts(release WS2TCP_LOCAL_FFI_RELEASE_LIBRARY WS2TCP_LOCAL_FFI_RELEASE_IMPLIB)

    if(CMAKE_BUILD_TYPE MATCHES "^(Release|RelWithDebInfo|MinSizeRel)$")
        set(WS2TCP_LOCAL_FFI_LIBRARY "${WS2TCP_LOCAL_FFI_RELEASE_LIBRARY}")
        set(WS2TCP_LOCAL_FFI_IMPLIB "${WS2TCP_LOCAL_FFI_RELEASE_IMPLIB}")
    else()
        set(WS2TCP_LOCAL_FFI_LIBRARY "${WS2TCP_LOCAL_FFI_DEBUG_LIBRARY}")
        set(WS2TCP_LOCAL_FFI_IMPLIB "${WS2TCP_LOCAL_FFI_DEBUG_IMPLIB}")
    endif()
endif()

add_library(ws2tcp_local_ffi SHARED IMPORTED)
set_target_properties(ws2tcp_local_ffi PROPERTIES
    IMPORTED_LOCATION "${WS2TCP_LOCAL_FFI_LIBRARY}"
    IMPORTED_NO_SONAME TRUE
    INTERFACE_INCLUDE_DIRECTORIES "${WS2TCP_LOCAL_FFI_INCLUDE_DIR}")

if(WS2TCP_LOCAL_FFI_IMPLIB)
    set_target_properties(ws2tcp_local_ffi PROPERTIES
        IMPORTED_IMPLIB "${WS2TCP_LOCAL_FFI_IMPLIB}")
endif()

if(TARGET ws2tcp_local_ffi_cargo_build)
    set_target_properties(ws2tcp_local_ffi PROPERTIES
        IMPORTED_CONFIGURATIONS "DEBUG;RELEASE;RELWITHDEBINFO;MINSIZEREL"
        IMPORTED_LOCATION_DEBUG "${WS2TCP_LOCAL_FFI_DEBUG_LIBRARY}"
        IMPORTED_LOCATION_RELEASE "${WS2TCP_LOCAL_FFI_RELEASE_LIBRARY}"
        IMPORTED_LOCATION_RELWITHDEBINFO "${WS2TCP_LOCAL_FFI_RELEASE_LIBRARY}"
        IMPORTED_LOCATION_MINSIZEREL "${WS2TCP_LOCAL_FFI_RELEASE_LIBRARY}")

    if(WS2TCP_LOCAL_FFI_DEBUG_IMPLIB)
        set_target_properties(ws2tcp_local_ffi PROPERTIES
            IMPORTED_IMPLIB_DEBUG "${WS2TCP_LOCAL_FFI_DEBUG_IMPLIB}"
            IMPORTED_IMPLIB_RELEASE "${WS2TCP_LOCAL_FFI_RELEASE_IMPLIB}"
            IMPORTED_IMPLIB_RELWITHDEBINFO "${WS2TCP_LOCAL_FFI_RELEASE_IMPLIB}"
            IMPORTED_IMPLIB_MINSIZEREL "${WS2TCP_LOCAL_FFI_RELEASE_IMPLIB}")
    endif()
endif()

add_executable(ws2tcp-local-qt
    src/main.cpp
    src/MainWindow.cpp
    src/MainWindow.h)

if(WIN32)
    set_target_properties(ws2tcp-local-qt PROPERTIES
        WIN32_EXECUTABLE "$<NOT:$<CONFIG:Debug>>")
endif()

target_link_libraries(ws2tcp-local-qt PRIVATE Qt6::Widgets ws2tcp_local_ffi)

if(TARGET ws2tcp_local_ffi_cargo_build)
    add_dependencies(ws2tcp-local-qt ws2tcp_local_ffi_cargo_build)
endif()

if(WIN32)
    qt_generate_deploy_app_script(
        TARGET ws2tcp-local-qt
        OUTPUT_SCRIPT WS2TCP_LOCAL_QT_DEPLOY_SCRIPT
        INCLUDE_PLUGINS qwindows
        NO_TRANSLATIONS
        NO_UNSUPPORTED_PLATFORM_ERROR)
endif()

install(TARGETS ws2tcp-local-qt
    RUNTIME_DEPENDENCY_SET ws2tcp-local-qt-runtime-deps
    BUNDLE DESTINATION .
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")

install(IMPORTED_RUNTIME_ARTIFACTS ws2tcp_local_ffi
    RUNTIME_DEPENDENCY_SET ws2tcp-local-qt-runtime-deps
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")

install(RUNTIME_DEPENDENCY_SET ws2tcp-local-qt-runtime-deps
    DIRECTORIES
        "$<TARGET_FILE_DIR:Qt6::Core>"
        "$<TARGET_FILE_DIR:Qt6::Gui>"
        "$<TARGET_FILE_DIR:Qt6::Widgets>"
        "$<TARGET_FILE_DIR:ws2tcp_local_ffi>"
    PRE_EXCLUDE_REGEXES
        "api-ms-"
        "ext-ms-"
    POST_EXCLUDE_REGEXES
        ".*[\\\\/]system32[\\\\/].*"
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")

if(WIN32)
    install(CODE "
if(IS_ABSOLUTE \"\${CMAKE_INSTALL_PREFIX}\")
    set(QT_DEPLOY_PREFIX \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}\")
else()
    get_filename_component(QT_DEPLOY_PREFIX \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}\" ABSOLUTE)
endif()
include(\"${WS2TCP_LOCAL_QT_DEPLOY_SCRIPT}\")
")
endif()
