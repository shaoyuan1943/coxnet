#ifndef BUFFER_H
#define BUFFER_H

#include <cstddef>
#include <cstring>
#include <string>

namespace coxnet {
    class Buffer {
    public:
        explicit Buffer(std::size_t initial_size = 8192)
            : data_(new char[initial_size]), capacity_(initial_size), size_(0) {}

        ~Buffer() {
            if (data_ != nullptr) {
                delete[] data_;
            }
        }

        Buffer(const Buffer&) = delete;
        Buffer& operator=(const Buffer&) = delete;

        Buffer(Buffer&& other) noexcept
            : data_(other.data_), capacity_(other.capacity_), size_(other.size_) {
            other.data_     = nullptr;
            other.capacity_ = 0;
            other.size_     = 0;
        }

        Buffer& operator=(Buffer&& other) noexcept {
            if (this != &other) {
                if (data_ != nullptr) {
                    delete[] data_;
                }

                data_           = other.data_;
                capacity_       = other.capacity_;
                size_           = other.size_;
                other.data_     = nullptr;
                other.capacity_ = 0;
                other.size_     = 0;
            }

            return *this;
        }

        char* data() { return data_; }
        const char* data() const { return data_; }

        std::size_t size() const { return size_; }
        std::size_t capacity() const { return capacity_; }

        void resize(std::size_t new_size) {
            if (new_size > capacity_) {
                reserve(new_size);
            }

            size_ = new_size;
        }

        void reserve(std::size_t new_capacity) {
            if (new_capacity <= capacity_) {
                return;
            }

            char* new_data = new char[new_capacity];
            if (size_ > 0) {
                std::memcpy(new_data, data_, size_);
            }

            delete[] data_;
            data_       = new_data;
            capacity_   = new_capacity;
        }

        void append(const char* data, std::size_t len) {
            if (size_ + len > capacity_) {
                reserve((size_ + len) * 2);
            }

            memcpy(data_ + size_, data, len);
            size_ += len;
        }

        void consume(std::size_t len) {
            if (len >= size_) {
                size_ = 0;
            } else {
                std::memmove(data_, data_ + len, size_ - len);
                size_ -= len;
            }
        }

        std::string_view view() const {
            return std::string_view(data_, size_);
        }

    private:
        char*       data_;
        std::size_t capacity_;
        std::size_t size_;
    };
}

#endif //BUFFER_H
