cmake_minimum_required(VERSION 3.23)
project(client)

include_directories(
    ${CMAKE_SOURCE_DIR}/
)

file(GLOB SOURCE_FILES "*.cpp" "*.c")

add_executable(client ${SOURCE_FILES})