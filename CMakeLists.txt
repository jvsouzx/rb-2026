cmake_minimum_required(VERSION 3.20)

project(rb_2026 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(unofficial-uwebsockets CONFIG REQUIRED)

add_executable(rb_2026 src/main.cpp)
target_link_libraries(rb_2026 PRIVATE unofficial::uwebsockets::uwebsockets)
