

#include <iostream>
#include <cstdint>

#include "catch_amalgamated.hpp"
#include "coxnet.h"

TEST_CASE("Buffer basic functions", "[Buffer]") {
    SECTION("default constructor") {
        coxnet::Buffer buffer;
        REQUIRE(buffer.readable_bytes() == 0);
        REQUIRE(buffer.writable_bytes() > 0);
    }

    SECTION("write and read integer") {
        coxnet::Buffer buffer;
        int writeData = 42;
        buffer.write(&writeData, sizeof(writeData));

        REQUIRE(buffer.readable_bytes() == sizeof(writeData));

        int readData = 0;
        buffer.read(&readData, sizeof(readData));

        REQUIRE(readData == 42);
        REQUIRE(buffer.readable_bytes() == 0);
    }

    SECTION("mult times write and read") {
        coxnet::Buffer buffer;
        int data[] = {1, 2, 3, 4, 5};
        buffer.write(data, sizeof(data));

        REQUIRE(buffer.readable_bytes() == sizeof(data));

        int readData[5] = {0};
        buffer.read(readData, sizeof(readData));

        for (int i = 0; i < 5; ++i) {
            REQUIRE(readData[i] == data[i]);
        }
    }

    SECTION("peek data") {
        coxnet::Buffer buffer;
        int writeData = 100;
        buffer.write(&writeData, sizeof(writeData));

        int peekData = 0;
        buffer.peek(&peekData, sizeof(peekData));

        REQUIRE(peekData == 100);
        REQUIRE(buffer.readable_bytes() == sizeof(writeData)); // 确保peek不改变可读字节数
    }

    SECTION("auto expand store space") {
        coxnet::Buffer buffer(10); // 初始容量很小
        std::vector<int> largeData(100, 42);

        buffer.write(largeData.data(), largeData.size() * sizeof(int));

        REQUIRE(buffer.readable_bytes() == largeData.size() * sizeof(int));

        std::vector<int> readData(100, 0);
        buffer.read(readData.data(), readData.size() * sizeof(int));

        REQUIRE(std::equal(largeData.begin(), largeData.end(), readData.begin()));
    }

    SECTION("clear and compact") {
        coxnet::Buffer buffer;
        int data1 = 10, data2 = 20;

        buffer.write(&data1, sizeof(data1));
        buffer.read(&data1, sizeof(data1)); // 读取后留下一些已读数据

        buffer.write(&data2, sizeof(data2));

        buffer.compact(); // 压缩内存

        int readData = 0;
        buffer.read(&readData, sizeof(readData));
        REQUIRE(readData == 20);
    }

    SECTION("reserve store space") {
        coxnet::Buffer buffer(10);
        size_t originalCapacity = buffer.writable_bytes();

        buffer.reserve(originalCapacity * 2);

        REQUIRE(buffer.writable_bytes() >= originalCapacity * 2);
    }
}

TEST_CASE("buffer marginal test", "[Buffer]") {
    SECTION("many small data write") {
        coxnet::Buffer buffer;
        for (int i = 0; i < 1000; ++i) {
            int data = i;
            buffer.write(&data, sizeof(data));
        }

        REQUIRE(buffer.readable_bytes() == 1000 * sizeof(int));

        for (int i = 0; i < 1000; ++i) {
            int readData = 0;
            buffer.read(&readData, sizeof(readData));
            REQUIRE(readData == i);
        }
    }

    SECTION("huge data") {
        coxnet::Buffer buffer;
        std::vector<char> largeData(1024 * 1024, 'A'); // 1MB数据

        buffer.write(largeData.data(), largeData.size());

        REQUIRE(buffer.readable_bytes() == largeData.size());

        std::vector<char> readData(largeData.size(), 0);
        buffer.read(readData.data(), readData.size());

        REQUIRE(readData == largeData);
    }
}