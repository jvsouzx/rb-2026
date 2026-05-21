cmake_minimum_required(VERSION 3.20)

project(fraud_api)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(unofficial-uwebsockets CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

add_executable(fraud_api
    src/main.cpp
    src/vectorize.cpp
    src/vectorSearch.cpp
)

target_link_libraries(fraud_api
    PRIVATE
        unofficial::uwebsockets::uwebsockets
        nlohmann_json::nlohmann_json
)