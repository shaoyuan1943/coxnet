cmake_minimum_required(VERSION 3.23)
project(coxnet)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(${CMAEKE_SOURCE_DIR}/coxnet)

add_subdirectory(samples/my_client)
add_subdirectory(samples/my_server)