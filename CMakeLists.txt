cmake_minimum_required(VERSION 3.23)
project(coxnet)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 添加头文件
include_directories(${CMAKE_CURRENT_SOURCE_DIR})
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/3rdparty)

# 创建库目标（头文件库）
add_library(coxnet INTERFACE)
target_include_directories(coxnet INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# 明确列出头文件（不影响CMAKE生成，为了兼容CLion IDE）
file(GLOB HEADER_FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h"
)
target_sources(coxnet INTERFACE ${HEADER_FILES})

# 添加测试目录
add_subdirectory(test)