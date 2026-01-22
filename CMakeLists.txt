cmake_minimum_required(VERSION 3.10)
project(brow6el C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# RPATH configuration for AppImage and portable builds
# $ORIGIN is relative to the executable location
set(CMAKE_BUILD_RPATH ".")
set(CMAKE_INSTALL_RPATH "$ORIGIN:$ORIGIN/../lib:$ORIGIN/..")
set(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)
set(CMAKE_INSTALL_RPATH_USE_LINK_PATH FALSE)

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
)

# Create executable
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
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/src/select_detector.js ${CMAKE_CURRENT_BINARY_DIR}/select_detector.js COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/src/hint_mode.js ${CMAKE_CURRENT_BINARY_DIR}/hint_mode.js COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/src/mouse_emu.js ${CMAKE_CURRENT_BINARY_DIR}/mouse_emu.js COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/src/inspect_mode.js ${CMAKE_CURRENT_BINARY_DIR}/inspect_mode.js COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/src/visual_mode.js ${CMAKE_CURRENT_BINARY_DIR}/visual_mode.js COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/src/tutorial.html ${CMAKE_CURRENT_BINARY_DIR}/tutorial.html COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/src/run_brow6el.sh ${CMAKE_CURRENT_BINARY_DIR}/run_brow6el.sh COPYONLY)

# Copy bundled scripts directory
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/src/scripts DESTINATION ${CMAKE_CURRENT_BINARY_DIR})

# Make run script executable
file(CHMOD ${CMAKE_CURRENT_BINARY_DIR}/run_brow6el.sh
    PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
