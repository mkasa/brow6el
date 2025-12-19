cmake_minimum_required(VERSION 3.10)
project(brow6el C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# CEF configuration
set(CEF_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/cef_binary" CACHE PATH "Root directory of CEF binary distribution")
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CEF_ROOT}/cmake")

# Find packages
find_package(CEF REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(SIXEL REQUIRED libsixel)
pkg_check_modules(TERMKEY REQUIRED termkey)

# Include directories
include_directories(
    ${CEF_INCLUDE_PATH}
    ${SIXEL_INCLUDE_DIRS}
    ${TERMKEY_INCLUDE_DIRS}
)

# Source files
set(SOURCES
    src/main.cpp
    src/browser_client.cpp
    src/sixel_renderer.cpp
    src/terminal_detector.cpp
    src/input_handler.cpp
    src/status_bar.cpp
)

set(HEADERS
    src/browser_app.h
    src/browser_client.h
    src/sixel_renderer.h
    src/terminal_detector.h
    src/input_handler.h
    src/status_bar.h
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
    ${TERMKEY_LIBRARIES}
    pthread
)

# Copy CEF binary files
COPY_FILES("${PROJECT_NAME}" "${CEF_BINARY_FILES}" "${CEF_BINARY_DIR}" "${CMAKE_CURRENT_BINARY_DIR}")
COPY_FILES("${PROJECT_NAME}" "${CEF_RESOURCE_FILES}" "${CEF_RESOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}")

# Copy JS files and run script
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/src/select_detector.js ${CMAKE_CURRENT_BINARY_DIR}/select_detector.js COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/src/run_brow6el.sh ${CMAKE_CURRENT_BINARY_DIR}/run_brow6el.sh COPYONLY)

# Make run script executable
file(CHMOD ${CMAKE_CURRENT_BINARY_DIR}/run_brow6el.sh
    PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
