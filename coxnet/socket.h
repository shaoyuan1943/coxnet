#ifndef SOCKET_H
#define SOCKET_H

#include "poller.h"
#include "buffer.h"
#include "io_def.h"

#include <cassert>
#include <memory>
#include <tuple>
#include <utility>
#include <set>

namespace coxnet {
  class Cleaner {
  public:
    void push_handle(socket_t handle) { clean_handles_.emplace(handle); }
    void traverse(std::function<void(socket_t)>&& func) const {
      for (const socket_t handle: clean_handles_) {
        func(handle);
      }
    }

    void clear() {
      if (!clean_handles_.empty()) {
        clean_handles_.clear();
      }
    }
  private:
    std::set<socket_t> clean_handles_;
  };

#ifdef _WIN32
  class RecvContext4Win {
    friend class Socket;
    friend class Poller;
    friend void WINAPI IOCompletionCallBack(DWORD, DWORD, LPOVERLAPPED);

    WSAOVERLAPPED   Overlapped  = {};
    WSABUF          Buf         = {};
    Socket*         Conn        = nullptr;
  };
#endif

  class Socket {
public:
    friend class Poller;
    friend class IPoller;
    explicit Socket(socket_t native_handle, Cleaner* cleaner = nullptr, int epoll_fd = -1) {
      handle_ = native_handle;
      cleaner_ = cleaner;

#ifdef __linux__
      epoll_fd_ = epoll_fd;
#endif

      if (!Socket::_is_listener()) {
        read_buff_ = new SimpleBuffer(max_read_buff_size);
        write_buff_ = new SimpleBuffer(max_write_buff_size);
      }
    }

    virtual ~Socket() {
      if (read_buff_ != nullptr) {
        delete read_buff_;
        read_buff_ = nullptr;
      }

      if (write_buff_ != nullptr) {
        delete write_buff_;
        write_buff_ = nullptr;
      }
    }

    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;
    Socket(Socket&& other) = delete;
    Socket& operator=(Socket&& other) = delete;

    socket_t native_handle() const { return handle_; }
    bool is_valid() const { return handle_ != invalid_socket && err_ == 0 && !user_closed_; }

    void user_close() {
      user_closed_ = true;
      _close_handle(0);
    }

    std::tuple<char*, int> remote_addr() { return std::make_tuple(remote_addr_, remote_port_); }

    int write(const char* data, size_t len) {
      if (!is_valid() || user_closed_ || err_ != 0) {
        return -1;
      }

      if (write_buff_->written_size_from_seeker() > 0) {
        write_buff_->write(data, len);
        return static_cast<int>(len);
      }

      size_t  total_sent  = 0;
      size_t  data_len    = len;

      while (total_sent < data_len) {
        int sent_n = ::send(native_handle(), data + total_sent, data_len - total_sent, 0);
        if (sent_n > 0) {
          total_sent += sent_n;
          continue;
        }  
        
        int err_code = get_last_error();
        if (adjust_io_error_option(err_code) == ErrorOption::kNext) {
          write_buff_->write(data + total_sent, data_len - total_sent);
#ifdef __linux__
          epoll_event ev{ .events = EPOLLIN | EPOLLOUT | EPOLLET, .data.ptr = this };
          epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, handle_, &ev);
#endif // __linux__
          break;
        }

        if (adjust_io_error_option(err_code) == ErrorOption::kContinue) {
          continue;
        }

        _close_handle(err_code);
        return -1;
      }

      return static_cast<int>(total_sent);
    }

private:
    size_t _try_write_when_io_event_coming() {
      if (write_buff_->written_size_from_seeker() <= 0) {
        return 0;
      }

      size_t total_sent = 0;
      size_t data_len   = write_buff_->written_size_from_seeker();
      while (total_sent < data_len) {
        int sent_n =
            ::send(native_handle(), write_buff_->data_from_last_seek(), write_buff_->written_size_from_seeker(), 0);
        if (sent_n > 0) {
          total_sent += sent_n;
          write_buff_->seek(sent_n);
          continue;
        } 
          
        const int err_code = get_last_error();
        if (adjust_io_error_option(err_code) == ErrorOption::kNext) {
#ifdef __linux__
          epoll_event ev{ .events = EPOLLIN | EPOLLOUT | EPOLLET, .data.ptr = this };
          epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, handle_, &ev);
#endif // __linux__
          break;
        }

        if (adjust_io_error_option(err_code) == ErrorOption::kContinue) {
          continue;
        }

        _close_handle(err_code);
        return -1;
      }

      if (total_sent >= data_len) {
        write_buff_->clear();
#ifdef __linux__
        epoll_event ev{ .events = EPOLLIN | EPOLLET, .data.ptr = this };
        epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, handle_, &ev);
#endif // __linux__
      }

      return total_sent;
    }

    void _close_handle(int err = 0) {
      if (!is_valid()) {
        return;
      }

#ifdef _WIN32
      closesocket(handle_);
#endif

#ifdef __linux__
      epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, handle_, nullptr);
      close(handle_);
#endif

      handle_ = invalid_socket;
      err_    = err;

      if (cleaner_ != nullptr) {
        cleaner_->push_handle(handle_);
      }
    }

    virtual bool _is_listener() { return false; }

    static bool _set_non_blocking(socket_t handle) {
#ifdef _WIN32
      u_long  option = 1;
      return ioctlsocket(handle, FIONBIO, &option) == 0;
#endif

#if defined (__linux__) || (__APPLE__)
      int option = fcntl(handle, F_GETFL, 0);
      return fcntl(handle, F_SETFL, option | O_NONBLOCK) == 0;
#endif
    }

    void _set_remote_addr(const char* addr, uint32_t port) {
      memcpy(remote_addr_, addr, 16);
      remote_port_ = port;
    }
#ifdef _WIN32
    void _overlapped() {
      memset(&recv_context_for_win_.Overlapped, 0, sizeof(recv_context_for_win_.Overlapped));
      recv_context_for_win_.Buf.buf = this->read_buff_->data();
      recv_context_for_win_.Buf.len = this->read_buff_->writable_size();
      recv_context_for_win_.Conn    = this;
    }
    friend void WINAPI IOCompletionCallBack(DWORD, DWORD, LPOVERLAPPED);
#endif //_WIN32
  private:
    socket_t          handle_           = invalid_socket;

    char              remote_addr_[16]  = { 0 };
    uint32_t          remote_port_      = 0;

    SimpleBuffer*     read_buff_        = nullptr;
    SimpleBuffer*     write_buff_       = nullptr;
    bool              io_completed_     = false;

    int               err_              = 0;
    bool              user_closed_      = false;
    Cleaner*          cleaner_          = nullptr;

#ifdef __linux__
    int               epoll_fd_         = -1;
#endif

#ifdef _WIN32
    RecvContext4Win recv_context_for_win_;
#endif
  };

  class listener final : public Socket {
  public:
    explicit listener(socket_t sock) : Socket(sock) {}
  private:
    bool _is_listener() override { return true; }
  };

  static void init_socket_env() {
#ifdef _WIN32
    WSAData wsa_data = {};
    if (::WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
      assert(false);
    }
#endif
  }

  static void shut_socket_env() {
#ifdef _WIN32
    ::WSACleanup();
#endif // _WIN32
  }
} // namespace coxnet

#endif // SOCKET_H
