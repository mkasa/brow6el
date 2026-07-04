cmake_minimum_required(VERSION 3.10)
project(brow6el C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# RPATH configuration for AppImage and portable builds (Linux only).
# $ORIGIN is relative to the executable location. On macOS the CEF framework is
# loaded dynamically via CefScopedLibraryLoader, so no $ORIGIN rpath is needed.
if(NOT APPLE)
    set(CMAKE_BUILD_RPATH ".")
    set(CMAKE_INSTALL_RPATH "$ORIGIN:$ORIGIN/../lib:$ORIGIN/..")
    set(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)
    set(CMAKE_INSTALL_RPATH_USE_LINK_PATH FALSE)
endif()

# Get version from git
execute_process(
    COMMAND git describe --tags --always --dirty
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE GIT_VERSION
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)

# Fallback version if git is not available
if(NOT GIT_VERSION)
    set(GIT_VERSION "0.0.999-dev")
endif()

# Clean up version string (remove 'v' prefix if present)
string(REGEX REPLACE "^v" "" BROW6EL_VERSION "${GIT_VERSION}")

# Extract Chromium version from CEF directory name
file(GLOB CEF_DIRS "${CMAKE_SOURCE_DIR}/cef_binary*chromium*")
if(CEF_DIRS)
    list(GET CEF_DIRS 0 CEF_DIR)
    string(REGEX MATCH "chromium-([0-9]+\\.[0-9]+\\.[0-9]+\\.[0-9]+)" CHROMIUM_MATCH "${CEF_DIR}")
    if(CMAKE_MATCH_1)
        set(CHROMIUM_VERSION "${CMAKE_MATCH_1}")
    else()
        set(CHROMIUM_VERSION "143.0.0.0")
    endif()
else()
    set(CHROMIUM_VERSION "143.0.0.0")
endif()

# Detect system architecture
if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|amd64|AMD64")
    set(SYSTEM_ARCH "x86_64")
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64|ARM64")
    set(SYSTEM_ARCH "aarch64")
else()
    set(SYSTEM_ARCH "${CMAKE_SYSTEM_PROCESSOR}")
endif()

message(STATUS "Brow6el version: ${BROW6EL_VERSION}")
message(STATUS "Chromium version: ${CHROMIUM_VERSION}")
message(STATUS "System architecture: ${SYSTEM_ARCH}")

# Generate version header
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/src/version.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/version.h
    @ONLY
)

# Set CMake policy for <PackageName>_ROOT variables
if(POLICY CMP0074)
    cmake_policy(SET CMP0074 NEW)
endif()

# CEF configuration
set(CEF_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/cef_binary" CACHE PATH "Root directory of CEF binary distribution")
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CEF_ROOT}/cmake")

# Find packages
find_package(CEF REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(SIXEL REQUIRED libsixel)

# Include directories
include_directories(
    ${CEF_INCLUDE_PATH}
    ${SIXEL_INCLUDE_DIRS}
    ${TERMKEY_INCLUDE_DIRS}
    ${CMAKE_CURRENT_BINARY_DIR}
)

# Source files
set(SOURCES
    src/main.cpp
    src/browser_client.cpp
    src/image_renderer.cpp
    src/sixel_renderer.cpp
    src/kitty_renderer.cpp
    src/terminal_detector.cpp
    src/input_handler.cpp
    src/status_bar.cpp
    src/bookmarks.cpp
    src/user_scripts.cpp
    src/profile_config.cpp
    src/clipboard.cpp
)

set(HEADERS
    src/browser_app.h
    src/browser_client.h
    src/image_renderer.h
    src/sixel_renderer.h
    src/kitty_renderer.h
    src/terminal_detector.h
    src/input_handler.h
    src/status_bar.h
    src/bookmarks.h
    src/user_scripts.h
    src/profile_config.h
    src/clipboard.h
    src/platform_paths.h
)

# Bundled application resources (injected JS + tutorial page). Shared list used
# by both the Linux path (copy next to the binary) and the macOS path (copy into
# the .app bundle's Contents/Resources).
set(BROW6EL_RESOURCE_FILES
    select_detector.js
    hint_mode.js
    mouse_emu.js
    inspect_mode.js
    visual_mode.js
    tutorial.html
)

if(OS_MAC)
    # =======================================================================
    # macOS: build brow6el.app plus the CEF Helper .app bundles.
    #
    # The CEF framework is loaded dynamically at runtime via
    # CefScopedLibraryLoader, so it is NOT linked here — only the wrapper static
    # library and the system frameworks in CEF_STANDARD_LIBS. Subprocesses are
    # the separate Helper bundles (built below), not this executable.
    # =======================================================================
    set(CEF_APP "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.app")

    # Generate the main app Info.plist.
    set(BROW6EL_PRODUCT_NAME "${PROJECT_NAME}")
    set(BROW6EL_BUNDLE_ID "org.brow6el.brow6el")
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/mac/Info.plist.in"
        "${CMAKE_CURRENT_BINARY_DIR}/Info.plist"
        @ONLY)

    # Homebrew's libsixel lives outside the default macOS linker search path.
    link_directories(${SIXEL_LIBRARY_DIRS})

    add_executable(${PROJECT_NAME} MACOSX_BUNDLE ${SOURCES} ${HEADERS})
    set_target_properties(${PROJECT_NAME} PROPERTIES
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_BINARY_DIR}/Info.plist"
        OUTPUT_NAME "${PROJECT_NAME}")
    # pthread is part of libSystem on macOS (linking it explicitly only warns).
    target_link_libraries(${PROJECT_NAME}
        ${CEF_ROOT}/build/libcef_dll_wrapper/libcef_dll_wrapper.a
        ${CEF_STANDARD_LIBS}
        ${SIXEL_LIBRARIES}
        z)

    # Copy the CEF framework into brow6el.app/Contents/Frameworks.
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CEF_APP}/Contents/Frameworks"
        VERBATIM)
    COPY_MAC_FRAMEWORK(${PROJECT_NAME} "${CEF_ROOT}/Release" "${CEF_APP}")

    # Copy bundled app resources into brow6el.app/Contents/Resources.
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CEF_APP}/Contents/Resources"
        VERBATIM)
    foreach(_res ${BROW6EL_RESOURCE_FILES})
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy
                    "${CMAKE_CURRENT_SOURCE_DIR}/src/${_res}"
                    "${CEF_APP}/Contents/Resources/${_res}"
            VERBATIM)
    endforeach()
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_CURRENT_SOURCE_DIR}/src/scripts"
                "${CEF_APP}/Contents/Resources/scripts"
        VERBATIM)

    # Build the CEF Helper .app bundles — one per suffix (default, Alerts, GPU,
    # Plugin, Renderer). They share one tiny entry point and differ only by
    # name/identifier in their generated Info.plist.
    foreach(_suffix_list ${CEF_HELPER_APP_SUFFIXES})
        # Format is "<name suffix>:<target suffix>:<plist suffix>".
        string(REPLACE ":" ";" _suffix_list ${_suffix_list})
        list(GET _suffix_list 0 _name_suffix)
        list(GET _suffix_list 1 _target_suffix)
        list(GET _suffix_list 2 _plist_suffix)

        set(_helper_target "${PROJECT_NAME}_Helper${_target_suffix}")
        set(_helper_output_name "${PROJECT_NAME} Helper${_name_suffix}")

        # Generate this helper's Info.plist.
        set(BROW6EL_HELPER_PRODUCT_NAME "${_helper_output_name}")
        set(BROW6EL_HELPER_BUNDLE_ID "org.brow6el.brow6el.helper${_plist_suffix}")
        set(_helper_info_plist "${CMAKE_CURRENT_BINARY_DIR}/helper-Info${_plist_suffix}.plist")
        configure_file(
            "${CMAKE_CURRENT_SOURCE_DIR}/mac/helper-Info.plist.in"
            "${_helper_info_plist}"
            @ONLY)

        add_executable(${_helper_target} MACOSX_BUNDLE src/process_helper_mac.mm)
        set_target_properties(${_helper_target} PROPERTIES
            MACOSX_BUNDLE_INFO_PLIST "${_helper_info_plist}"
            OUTPUT_NAME "${_helper_output_name}")
        target_link_libraries(${_helper_target}
            ${CEF_ROOT}/build/libcef_dll_wrapper/libcef_dll_wrapper.a
            ${CEF_STANDARD_LIBS})

        # Copy the finished helper bundle into the main app's Frameworks dir.
        add_dependencies(${PROJECT_NAME} ${_helper_target})
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                    "${CMAKE_CURRENT_BINARY_DIR}/${_helper_output_name}.app"
                    "${CEF_APP}/Contents/Frameworks/${_helper_output_name}.app"
            VERBATIM)
    endforeach()

    # Generate the terminal launcher (build/run_brow6el.sh).
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/mac/run_brow6el_mac.sh.in"
        "${CMAKE_CURRENT_BINARY_DIR}/run_brow6el.sh"
        @ONLY)
    file(CHMOD ${CMAKE_CURRENT_BINARY_DIR}/run_brow6el.sh
        PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)

else()
    # =======================================================================
    # Linux: original behavior (link libcef.so, copy files next to the binary).
    # =======================================================================
    add_executable(${PROJECT_NAME} ${SOURCES} ${HEADERS})

    # Link CEF
    ADD_LOGICAL_TARGET("libcef_lib" "${CEF_LIB_DEBUG}" "${CEF_LIB_RELEASE}")
    SET_CEF_TARGET_OUT_DIR()

    target_link_libraries(${PROJECT_NAME}
        libcef_lib
        ${CEF_ROOT}/build/libcef_dll_wrapper/libcef_dll_wrapper.a
        ${CEF_STANDARD_LIBS}
        ${SIXEL_LIBRARIES}
        pthread
        z
    )

    # Copy CEF binary files
    COPY_FILES("${PROJECT_NAME}" "${CEF_BINARY_FILES}" "${CEF_BINARY_DIR}" "${CMAKE_CURRENT_BINARY_DIR}")
    COPY_FILES("${PROJECT_NAME}" "${CEF_RESOURCE_FILES}" "${CEF_RESOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}")

    # Copy JS files and run script
    foreach(_res ${BROW6EL_RESOURCE_FILES})
        configure_file(${CMAKE_CURRENT_SOURCE_DIR}/src/${_res} ${CMAKE_CURRENT_BINARY_DIR}/${_res} COPYONLY)
    endforeach()
    configure_file(${CMAKE_CURRENT_SOURCE_DIR}/src/run_brow6el.sh ${CMAKE_CURRENT_BINARY_DIR}/run_brow6el.sh COPYONLY)

    # Copy bundled scripts directory
    file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/src/scripts DESTINATION ${CMAKE_CURRENT_BINARY_DIR})

    # Make run script executable
    file(CHMOD ${CMAKE_CURRENT_BINARY_DIR}/run_brow6el.sh
        PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
endif()