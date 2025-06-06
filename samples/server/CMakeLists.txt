cmake_minimum_required(VERSION 3.23)
project(server)

# 使用绝对路径确保可靠性
include_directories(
    ${CMAKE_SOURCE_DIR}/
)

file(GLOB SOURCE_FILES "*.cpp" "*.c")

add_executable(server ${SOURCE_FILES})