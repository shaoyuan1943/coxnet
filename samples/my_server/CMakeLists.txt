cmake_minimum_required(VERSION 3.23)
project(my_server)

file(GLOB SOURCE_FILES "*.cpp" "*.c")

add_executable(my_server ${SOURCE_FILES})
