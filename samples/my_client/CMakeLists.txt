cmake_minimum_required(VERSION 3.23)
project(my_client)

file(GLOB SOURCE_FILES "*.cpp" "*.c")

add_executable(my_client ${SOURCE_FILES})
