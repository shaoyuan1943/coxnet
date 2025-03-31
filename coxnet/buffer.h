#ifndef BUFFER_H
#define BUFFER_H

#include "io_def.h"

#include <vector>

namespace coxnet {
    class Poller;
    class SimpleBuffer {
    public:
        friend class Poller;
        SimpleBuffer(size_t initial_capacity = 8192)
                : buff_(initial_capacity), begin_(0), end_(0), write_index_(0) {}

        void clear()            { begin_ = end_ = write_index_ = 0; }
        char* data_from_head()  { return &buff_[write_index_]; }
        char* data_from_tail()  { return &buff_[end_]; }

        // only write in tail
        void write_to_tail(const char* data, size_t size) {
            _ensure_writable_bytes(size);
            memcpy(&buff_[end_], data, size);
            end_ += size;
        }
        
        void seek_written(size_t size)      { write_index_ += size; }
        size_t written_size() const         { return end_ - write_index_; }
        size_t writeable_size() const       { return buff_.size() - end_; }
    private:
#if defined(_WIN32)
        friend void WINAPI IOCompletionCallBack(DWORD, DWORD, LPOVERLAPPED);
#endif // _WIN32
        void _add_written_from_io(size_t size) { end_ += size; }


        void _ensure_writable_bytes(size_t size) {
            size_t writable_bytes = writeable_size();
            if (writable_bytes < size) {
                // 如果总空间不足，扩展缓冲区
                size_t new_capacity = buff_.size() * 2;
                while (new_capacity < buff_.size() + size) {
                    new_capacity *= 2;
                }
                buff_.resize(new_capacity);
            }
        }
    private:
        std::vector<char>   buff_;
        size_t              begin_;
        size_t              end_;
        size_t              write_index_;
    };
}

#endif // BUFFER_H