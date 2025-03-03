

#include <iostream>
#include <cstdint>

#include "catch_amalgamated.hpp"
#include "coxnet.h"

TEST_CASE("Buffer construction and basic properties", "[buffer]") {
    SECTION("Default construction") {
        coxnet::Buffer buf;
        REQUIRE(buf.size() == 0);
        REQUIRE(buf.capacity() == 8192);
        REQUIRE(buf.data() != nullptr);
    }

    SECTION("Custom initial size") {
        const std::size_t custom_size = 16384;
        coxnet::Buffer buf(custom_size);
        REQUIRE(buf.size() == 0);
        REQUIRE(buf.capacity() == custom_size);
    }
}

TEST_CASE("Buffer resize operation", "[buffer]") {
    coxnet::Buffer buf(100);

    SECTION("Resize within capacity") {
        buf.resize(50);
        REQUIRE(buf.size() == 50);
        REQUIRE(buf.capacity() == 100);
    }

    SECTION("Resize beyond capacity") {
        buf.resize(200);
        REQUIRE(buf.size() == 200);
        REQUIRE(buf.capacity() >= 200);
    }

    SECTION("Resize to zero") {
        buf.resize(50);  // First set some size
        buf.resize(0);
        REQUIRE(buf.size() == 0);
        REQUIRE(buf.capacity() == 100);  // Capacity should remain unchanged
    }
}

TEST_CASE("Buffer reserve operation", "[buffer]") {
    coxnet::Buffer buf(100);

    SECTION("Reserve more capacity") {
        buf.reserve(200);
        REQUIRE(buf.capacity() >= 200);
        REQUIRE(buf.size() == 0);  // Size should remain unchanged
    }

    SECTION("Reserve less capacity") {
        buf.reserve(50);
        REQUIRE(buf.capacity() == 100);  // Should keep original capacity
    }

    SECTION("Reserve same capacity") {
        buf.reserve(100);
        REQUIRE(buf.capacity() == 100);  // Should keep original capacity
    }
}

TEST_CASE("Buffer append operation", "[buffer]") {
    coxnet::Buffer buf(100);

    SECTION("Append within capacity") {
        const char* test_data = "Hello, world!";
        std::size_t len = 13;

        buf.append(test_data, len);
        REQUIRE(buf.size() == len);
        REQUIRE(std::memcmp(buf.data(), test_data, len) == 0);
    }

    SECTION("Append beyond capacity") {
        char test_data[150];
        std::memset(test_data, 'A', 150);

        buf.append(test_data, 150);
        REQUIRE(buf.size() == 150);
        REQUIRE(buf.capacity() >= 150);
        REQUIRE(std::memcmp(buf.data(), test_data, 150) == 0);
    }

    SECTION("Multiple appends") {
        const char* data1 = "Hello, ";
        const char* data2 = "world!";
        const char* expected = "Hello, world!";

        buf.append(data1, 7);
        buf.append(data2, 6);

        REQUIRE(buf.size() == 13);
        REQUIRE(std::memcmp(buf.data(), expected, 13) == 0);
    }
}

TEST_CASE("Buffer consume operation", "[buffer]") {
    coxnet::Buffer buf(100);
    const char* test_data = "Hello, world!";
    buf.append(test_data, 13);

    SECTION("Consume partial data") {
        buf.consume(7);  // Consume "Hello, "

        REQUIRE(buf.size() == 6);
        REQUIRE(std::memcmp(buf.data(), "world!", 6) == 0);
    }

    SECTION("Consume exact size") {
        buf.consume(13);

        REQUIRE(buf.size() == 0);
    }

    SECTION("Consume more than size") {
        buf.consume(100);

        REQUIRE(buf.size() == 0);
    }

    SECTION("Consume zero bytes") {
        buf.consume(0);

        REQUIRE(buf.size() == 13);
        REQUIRE(std::memcmp(buf.data(), test_data, 13) == 0);
    }
}

TEST_CASE("Buffer move semantics", "[buffer]") {
    SECTION("Move construction") {
        coxnet::Buffer buf1(100);
        const char* test_data = "Test data";
        buf1.append(test_data, 9);

        coxnet::Buffer buf2(std::move(buf1));

        // buf1 should be empty now
        REQUIRE(buf1.data() == nullptr);
        REQUIRE(buf1.size() == 0);
        REQUIRE(buf1.capacity() == 0);

        // buf2 should have the data
        REQUIRE(buf2.size() == 9);
        REQUIRE(std::memcmp(buf2.data(), test_data, 9) == 0);
    }

    SECTION("Move assignment") {
        coxnet::Buffer buf1(100);
        coxnet::Buffer buf2(100);

        const char* test_data = "Test data";
        buf1.append(test_data, 9);

        buf2 = std::move(buf1);

        // buf1 should be empty now
        REQUIRE(buf1.data() == nullptr);
        REQUIRE(buf1.size() == 0);
        REQUIRE(buf1.capacity() == 0);

        // buf2 should have the data
        REQUIRE(buf2.size() == 9);
        REQUIRE(std::memcmp(buf2.data(), test_data, 9) == 0);
    }

    SECTION("Self move assignment") {
        coxnet::Buffer buf(100);
        const char* test_data = "Test data";
        buf.append(test_data, 9);

        buf = std::move(buf);  // This should be a no-op

        REQUIRE(buf.size() == 9);
        REQUIRE(std::memcmp(buf.data(), test_data, 9) == 0);
    }
}

TEST_CASE("Buffer string_view operation", "[buffer]") {
    coxnet::Buffer buf(100);
    const char* test_data = "Hello, world!";
    buf.append(test_data, 13);

    std::string_view view = buf.view();

    REQUIRE(view.size() == 13);
    REQUIRE(view == "Hello, world!");
}

TEST_CASE("Buffer edge cases", "[buffer]") {
    SECTION("Zero-length append") {
        coxnet::Buffer buf(100);
        const char* test_data = "Test";

        buf.append(test_data, 0);
        REQUIRE(buf.size() == 0);
    }

    SECTION("Append after consume") {
        coxnet::Buffer buf(100);
        buf.append("Hello", 5);
        buf.consume(3);  // Now contains "lo"
        buf.append(", world!", 8);  // Should contain "lo, world!"

        REQUIRE(buf.size() == 10);
        REQUIRE(buf.view() == "lo, world!");
    }

    SECTION("Large buffer handling") {
        const std::size_t large_size = 1024 * 1024;  // 1MB
        coxnet::Buffer buf(10);  // Start small

        std::vector<char> large_data(large_size, 'X');
        buf.append(large_data.data(), large_size);

        REQUIRE(buf.size() == large_size);
        REQUIRE(buf.capacity() >= large_size);

        // Check first and last bytes
        REQUIRE(buf.data()[0] == 'X');
        REQUIRE(buf.data()[large_size - 1] == 'X');
    }
}