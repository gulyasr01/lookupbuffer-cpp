cmake_minimum_required(VERSION 3.20)

project(lookupbuffer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(lookupbuffer
    src/main.cpp
)

target_include_directories(lookupbuffer PRIVATE
    include
)

# === GoogleTest ===
include(FetchContent)

FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.14.0  # recommended stable release
)

# Forces gtest to build as static libs
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(googletest)

enable_testing()

# === Add your test executable ===
add_executable(
    test_lookupbuffer
    tests/test_lookupbuffer.cpp
)

target_include_directories(test_lookupbuffer PRIVATE
    include
)

target_link_libraries(
    test_lookupbuffer
    PRIVATE
        gtest_main
)

# === Register tests with CTest ===
include(GoogleTest)
gtest_discover_tests(test_lookupbuffer)