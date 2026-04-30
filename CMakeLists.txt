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