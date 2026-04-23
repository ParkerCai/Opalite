cmake_minimum_required(VERSION 3.24)
project(opalite LANGUAGES CXX)

# Produces build/<preset>/compile_commands.json so VSCode / clangd can
# resolve RealSense + ImGui + GLFW include paths without duplication.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# C++23 for std::println / std::format, consistent with Project 3 & Project 4.
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# OpenCV: relies on OpenCV_DIR env var pointing at the install prefix that
# contains OpenCVConfig.cmake (see README for the expected value on this PC).
find_package(OpenCV REQUIRED)

find_package(OpenGL REQUIRED)

# Intel RealSense SDK 2.0
# The Windows installer ships realsense2.lib / .dll / headers but no CMake
# config module, so wire it up by hand as an INTERFACE imported target.
set(REALSENSE_SDK_DIR "C:/Program Files/RealSense SDK 2.0"
    CACHE PATH "Intel RealSense SDK 2.0 install root")
if(NOT EXISTS "${REALSENSE_SDK_DIR}/include/librealsense2")
    message(FATAL_ERROR
        "RealSense SDK not found at ${REALSENSE_SDK_DIR}. "
        "Install from https://github.com/IntelRealSense/librealsense/releases "
        "or set -DREALSENSE_SDK_DIR=<path> at configure time.")
endif()
add_library(realsense2 INTERFACE IMPORTED)
target_include_directories(realsense2 INTERFACE "${REALSENSE_SDK_DIR}/include")
target_link_libraries(realsense2 INTERFACE "${REALSENSE_SDK_DIR}/lib/x64/realsense2.lib")

# GLFW + Dear ImGui via FetchContent (pattern lifted from Project 3).
# ImGui is populated here so the sources exist under the build tree; actual
# ImGui .cpp files are compiled into the executable starting in step 3.
include(FetchContent)

set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG 3.3.8
)
FetchContent_MakeAvailable(glfw)

FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.90.1
)
FetchContent_MakeAvailable(imgui)

# Phase 2B brain client: cpp-httplib (single-header HTTP) + nlohmann/json
# for talking to local Ollama at http://localhost:11434.
FetchContent_Declare(cpp_httplib
    GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
    GIT_TAG v0.18.0
)
FetchContent_MakeAvailable(cpp_httplib)

FetchContent_Declare(nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
)
FetchContent_MakeAvailable(nlohmann_json)

# miniaudio (single-header, Windows / Linux / Android / macOS, MIT-0).
# Used by the Phase 2A sonar layer. Populate only - no CMakeLists in the
# repo root - and include the source dir directly. The
# MINIAUDIO_IMPLEMENTATION macro is defined in exactly one .cpp.
FetchContent_Declare(miniaudio
    GIT_REPOSITORY https://github.com/mackron/miniaudio.git
    GIT_TAG 0.11.22
)
FetchContent_MakeAvailable(miniaudio)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../include)
include_directories(${imgui_SOURCE_DIR})
include_directories(${imgui_SOURCE_DIR}/backends)
include_directories(${miniaudio_SOURCE_DIR})

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../bin)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_CURRENT_SOURCE_DIR}/../bin)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_CURRENT_SOURCE_DIR}/../bin)

# Opalite sources. Grows with each step.
set(OPALITE_SOURCES
    main.cpp
    realsense_stream.cpp
    gl_textures.cpp
    topdown.cpp
    free_space.cpp
    sonar.cpp
    brain_client.cpp
)

# Dear ImGui sources. Compiled directly into the executable - ImGui ships no
# CMake target of its own. Uses the GLFW + OpenGL3 backends.
set(IMGUI_SOURCES
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)

add_executable(opalite ${OPALITE_SOURCES} ${IMGUI_SOURCES})
target_link_libraries(opalite
    PRIVATE
    ${OpenCV_LIBS}
    realsense2
    glfw
    OpenGL::GL
    dwmapi
    httplib::httplib
    nlohmann_json::nlohmann_json
)

# Large FetchContent projects overflow the default PDB; disable to avoid
# MSVC linker limit (same rationale as Project 4).
if(MSVC)
    target_link_options(opalite PRIVATE /DEBUG:NONE)
endif()

# Copy realsense2.dll next to the exe so it resolves without a PATH edit.
add_custom_command(TARGET opalite POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${REALSENSE_SDK_DIR}/bin/x64/realsense2.dll"
        $<TARGET_FILE_DIR:opalite>
)
