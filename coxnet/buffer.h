#ifndef BUFFER_H
#define BUFFER_H

#include "io_def.h"

#include <cassert>

namespace coxnet {
  struct SimpleBuffer {
    friend class Poller;
    explicit SimpleBuffer(size_t initial_capacity = 8192)
    : size_(initial_capacity), begin_(0), end_(0), seek_index_(0) {
      data_ = new char[size_];
    }

    SimpleBuffer(const SimpleBuffer&) = delete;
    SimpleBuffer& operator=(const SimpleBuffer&) = delete;

    SimpleBuffer(SimpleBuffer&& other) noexcept
    : data_(other.data_), begin_(other.begin_), end_(other.end_),
      seek_index_(other.seek_index_), size_(other.size_) {
      other.data_       = nullptr;
      other.size_       = 0;
      other.begin_      = 0;
      other.end_        = 0;
      other.seek_index_ = 0;
    }

    SimpleBuffer& operator=(SimpleBuffer&& other) noexcept {
      if (this != &other) {
        delete[] data_;
        data_       = other.data_;
        begin_      = other.begin_;
        end_        = other.end_;
        seek_index_ = other.seek_index_;
        size_       = other.size_;

        other.data_       = nullptr;
        other.size_       = 0;
        other.begin_      = 0;
        other.end_        = 0;
        other.seek_index_ = 0;
      }
      return *this;
    }

    ~SimpleBuffer()                     { delete[] data_; }
    void clear()                        { begin_ = end_ = seek_index_ = 0; }

    size_t writable_size()              { return size_ - end_; }
    size_t written_size()               { return end_ - begin_; }
    size_t written_size_from_seek()     { return end_ - seek_index_; }
    size_t readable_size()              { return written_size(); }
    
    char* take_data()                   { return &data_[begin_]; }
    char* take_data_from_seek()         { return &data_[seek_index_]; }
    void add_written_from_external_write(const size_t size_written) {
      assert(end_ + size_written <= size_);
      end_ += size_written;
    }

    void seek(const size_t offset) {
      if (begin_ + offset <= end_) { // Assuming seek is from begin_ or relative to current seek_index
        seek_index_ = begin_ + offset; // Or seek_index_ += offset; with more checks
      } else {
        seek_index_ = end_; // Or throw an error, or handle as per design
      }

      // Make sure seek_index_ doesn't go before begin_ if that's a constraint
      if (seek_index_ < begin_) { seek_index_ = begin_; }
    }

    void write(const char* data, size_t size_written) {
      ensure_writable_size(size_written);
      memcpy(&data_[end_], data, size_written);
      end_ += size_written;
    }

    void ensure_writable_size(size_t required_size) {
      if (writable_size() >= required_size) {
          return;
      }

      size_t  new_size  = size_ + required_size * 2;
      char*   temp      = new char[new_size];
      memcpy(temp, data_ + begin_, end_);

      char* original  = data_;
      data_           = temp;
      size_           = new_size;
      delete[] original;
    }
#ifdef _WIN32
    friend void WINAPI IOCompletionCallBack(DWORD, DWORD, LPOVERLAPPED);
#endif // _WIN32
  private:
    char* data_         = nullptr;
    size_t begin_       = 0;
    size_t end_         = 0;
    size_t seek_index_  = 0;
    size_t size_        = 0;
  };
} // namespace coxnet

#endif // BUFFER_H
