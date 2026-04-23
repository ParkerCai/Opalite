cmake_minimum_required(VERSION 3.22.1)
project(opaliteedge LANGUAGES CXX)

# NDK ships with cmake 3.22.1 and clang; we only need C++17 (the desktop
# build uses C++23 for std::print, which the shared .cpp files don't
# actually use on NDK).
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Shared source tree root. This CMakeLists.txt lives at
#   <repo>/android/opalite-edge/app/src/main/cpp/CMakeLists.txt
# so the repo root is six levels up.
set(OPALITE_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../..)

include(FetchContent)

# cpp-httplib for the Brain client's POST to Ollama. Plain HTTP to the
# host PC on the LAN, so TLS/OpenSSL stays disabled.
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

# miniaudio is header-only and sonar.cpp defines MINIAUDIO_IMPLEMENTATION
# in exactly one TU, so the include directory is all we need here.
FetchContent_Declare(miniaudio
    GIT_REPOSITORY https://github.com/mackron/miniaudio.git
    GIT_TAG 0.11.22
)
FetchContent_MakeAvailable(miniaudio)

# Shared Opalite headers + NDK-portable sources from the desktop build.
# Compiled here without OPALITE_USE_OPENCV, so the cv::Mat overloads in
# free_space.cpp and brain_client.cpp are excluded by the preprocessor.
include_directories(${OPALITE_ROOT}/include)
include_directories(${miniaudio_SOURCE_DIR})

add_library(opaliteedge SHARED
    jni_bridge.cpp
    ${OPALITE_ROOT}/src/free_space.cpp
    ${OPALITE_ROOT}/src/sonar.cpp
    ${OPALITE_ROOT}/src/brain_client.cpp
)

target_link_libraries(opaliteedge
    PRIVATE
    httplib::httplib
    nlohmann_json::nlohmann_json
    # NDK system libs: android/log for __android_log_print,
    # OpenSLES and aaudio for miniaudio's playback backends.
    log
    android
    OpenSLES
    aaudio
)
