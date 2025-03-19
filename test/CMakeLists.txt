# 开启测试支持
enable_testing()

# 添加 Catch2 头文件路径
#include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../3rdparty/catch2)

# 获取所有测试子目录
file(GLOB TEST_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/test_*)

# 定义一个变量存储所有测试目标
set(ALL_TESTS "")

# 遍历每个测试目录
foreach(TEST_DIR ${TEST_DIRS})
    # 获取目录名称
    get_filename_component(TEST_NAME ${TEST_DIR} NAME)

    # 查找测试源文件
    file(GLOB TEST_SOURCES "${TEST_DIR}/*.cpp")

    # 如果找到源文件，创建测试可执行文件
    if(TEST_SOURCES)
        add_executable(${TEST_NAME} ${TEST_SOURCES})
        target_link_libraries(${TEST_NAME} PRIVATE coxnet)

        # 添加到测试列表
        list(APPEND ALL_TESTS ${TEST_NAME})

        # 添加为CTest测试
        add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
    endif()
endforeach()

# 创建一个编译所有测试的目标
add_custom_target(build_all_tests
        DEPENDS ${ALL_TESTS}
        COMMENT "Building all tests"
)