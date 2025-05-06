#ifndef BUFFER_H
#define BUFFER_H

#include "io_def.h"

#include <vector>

namespace coxnet {
    class Poller;
    struct SimpleBuffer {
    public:
        friend class Poller;
        SimpleBuffer(size_t initial_capacity = 8192)
            : size_(initial_capacity), begin_(0), end_(0), seek_index_(0) {
            data_ = new char[size_];        
        }

        ~SimpleBuffer()                     { delete[] data_; }
        void clear()                        { begin_ = end_ = seek_index_ = 0; }
        void seek(size_t size)              { seek_index_ += size; }
        char* data()                        { return data_; }
        char* data_from_last_seek()         { return &data_[seek_index_]; }
        size_t written_size_from_seeker()   { return end_ - seek_index_; }
        size_t writable_size()              { return size_ - end_; }
        size_t written_size()               { return end_ - begin_; }

        void write(const char* data, size_t size) {
            if (end_ - size <= 0) {
                size_ *= 2;
                char* temp = new char[size_];
                memcpy(temp, data_, end_);
                
                char* original = data_;
                data_ = temp;
                delete[] original;
            }

            memcpy(&data_[end_], data, size);
            end_ += size;
        }
#if defined(_WIN32)
        friend void WINAPI IOCompletionCallBack(DWORD, DWORD, LPOVERLAPPED);
#endif // _WIN32
    private:
        void _add_written_from_io(size_t size) { end_ += size; }
    private:
        char*   data_       = nullptr;
        size_t  begin_      = 0;
        size_t  end_        = 0;
        size_t  seek_index_ = 0;
        size_t  size_       = 0;    
    };
}

#endif // BUFFER_H