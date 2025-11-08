cmake_minimum_required(VERSION 3.10)
project(AdaptiveCLB)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Boost REQUIRED COMPONENTS system)
find_package(CURL REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(yaml-cpp REQUIRED)

include_directories(
    ${Boost_INCLUDE_DIRS}
    ${CURL_INCLUDE_DIRS}
)

add_executable(custom_lb
    src/main.cpp
    src/proxy_server.cpp
    src/shared_state.cpp
    src/docker_monitor.cpp
)

target_link_libraries(custom_lb
    ${Boost_LIBRARIES}
    ${CURL_LIBRARIES}
    nlohmann_json::nlohmann_json
    yaml-cpp
)

