cmake_minimum_required(VERSION 3.31)

project(ManualEngine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(SDL3 CONFIG REQUIRED)
find_package(bgfx CONFIG REQUIRED)

add_executable(manual_engine
    src/main.cpp
)

target_link_libraries(manual_engine
    PRIVATE
        SDL3::SDL3
        bgfx::bgfx
)
