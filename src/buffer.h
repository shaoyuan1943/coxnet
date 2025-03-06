#ifndef BUFFER_H
#define BUFFER_H

#include <cstddef>
#include <cstring>

namespace coxnet {
    class Buffer {
    public:
        // 默认构造函数，可以指定初始容量
        explicit Buffer(size_t initial_capacity = 8192)
            : data_(initial_capacity), read_index_(0), write_index_(0) {}

        // 从已有数据构造
        Buffer(const char* data, size_t size)
            : data_(size), read_index_(0), write_index_(size) {
            std::memcpy(data_.data(), data, size);
        }

        Buffer(const Buffer&) = delete;
        Buffer& operator=(const Buffer&) = delete;

        Buffer(Buffer&& other) noexcept {
            if (this == &other) {
                return;
            }

            data_.swap(other.data_);
            read_index_     = other.read_index_;
            write_index_    = other.write_index_;
        }

        Buffer& operator=(Buffer&& other) noexcept {
            if (this == &other) {
                return *this;
            }

            data_.swap(other.data_);
            read_index_     = other.read_index_;
            write_index_    = other.write_index_;
            return *this;
        }

        // 写入数据
        void write(const void* data, size_t size) {
            ensure_writable_bytes(size);
            std::memcpy(data_.data() + write_index_, data, size);
            write_index_ += size;
        }

        // 读取数据
        int read(void* dest, size_t size) {
            if (readable_bytes() < size) {
                return -1;
            }
            std::memcpy(dest, data_.data() + read_index_, size);
            read_index_ += size;
            return static_cast<int>(size);
        }

        // 预览数据，不移动读指针
        int peek(void* dest, size_t size) const {
            if (readable_bytes() < size) {
                return -1;
            }
            std::memcpy(dest, data_.data() + read_index_, size);
            return static_cast<int>(size);
        }

        // 清理已读数据，压缩内存
        void compact() {
            if (read_index_ > 0) {
                size_t remaining = readable_bytes();
                std::memmove(data_.data(), data_.data() + read_index_, remaining);
                read_index_ = 0;
                write_index_ = remaining;
            }
        }

        // 直接访问底层缓冲区
        char* data() { return data_.data() + write_index_; }
        const char* data() const { return data_.data() + write_index_; }

        // 可读字节数
        size_t readable_bytes() const { return write_index_ - read_index_; }

        // 可写字节数
        size_t writable_bytes() const { return data_.size() - write_index_; }

        // 预留空间
        void reserve(size_t new_capacity) {
            if (new_capacity > data_.size()) {
                data_.resize(new_capacity);
            }
        }

        // 清空缓冲区
        void clear() {
            read_index_ = 0;
            write_index_ = 0;
        }

    private:
        // 确保有足够的可写空间
        void ensure_writable_bytes(size_t size) {
            if (writable_bytes() < size) {
                // 如果总空间不足，扩展缓冲区
                size_t new_capacity = data_.size() * 2;
                while (new_capacity < data_.size() + size) {
                    new_capacity *= 2;
                }
                data_.resize(new_capacity);
            }
        }

        std::vector<char>   data_;        // 底层存储
        size_t              read_index_;    // 读取索引
        size_t              write_index_;   // 写入索引
    };
}

#endif //BUFFER_H
