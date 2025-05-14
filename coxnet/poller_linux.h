#ifndef POLLER_LINUX_H
#define POLLER_LINUX_H

#ifdef __linux__

#include "io_def.h"
#include "poller.h"
#include "socket.h"

#include <cassert>
#include <chrono>
#include <set>
#include <thread>


namespace coxnet {
  class Poller : public IPoller {
public:
    Poller() = default;
    ~Poller() = default;

    Poller(const Poller&) = delete;
    Poller& operator=(const Poller&) = delete;
    Poller(Poller&& other) = delete;
    Poller& operator=(Poller&& other) = delete;

    bool setup() {
      epoll_events_ = new epoll_event[max_epoll_event_count];
      epoll_fd_ = epoll_create1(EPOLL_CLOEXEC);
      if (epoll_fd_ == -1) {
        return false;
      }

      return true;
    }

    std::shared_ptr<Socket> connect(const char address[], uint32_t port, DataCallback on_data, 
        CloseCallback on_close) override {
      IPType net_type = ip_address_version(std::string(address));
      if (net_type == IPType::kInvalid) {
        return nullptr;
      }

      socket_t sock_handle = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
      if (sock_handle == invalid_socket) {
        return nullptr;
      }

      sockaddr_in remote_addr = { 0 };
      remote_addr.sin_family  = AF_INET;
      remote_addr.sin_port    = htons(port);
      if (inet_pton(AF_INET, address, &remote_addr.sin_addr) <= 0) {
        return nullptr;
      }

      int result = ::connect(sock_handle, reinterpret_cast<sockaddr*>(&remote_addr), sizeof(sockaddr_in));
      if (result == SOCKET_ERROR) { // TODO: async operation is in progress, ignore this error code
        if (get_last_error() != EINPROGRESS) {
          return nullptr;
        }
      }

      if (!Socket::_set_non_blocking(sock_handle)) {
        return nullptr;
      }

      fd_set write_set;
      FD_ZERO(&write_set);
      FD_SET(sock_handle, &write_set);

      timeval timeout{ 5, 0 };
      // use select to ensure connect operation succeed
      result = select((int)(sock_handle + 1), nullptr, &write_set, nullptr, &timeout);
      if (result != 1) {
        close(sock_handle);
        return nullptr;
      }

      auto conn = std::make_shared<Socket>(sock_handle, _cleaner(), epoll_fd_);
      epoll_event ev{ .events = EPOLLIN | EPOLLET, .data.ptr = conn.get() };
      int result = epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, sock_handle, &ev);
      if (result != 0) {
        close(sock_handle);
        conn.reset();
        return nullptr;
      }

      conn->_set_remote_addr(address, port);
      conns_.emplace(conn->native_handle(), conn);

      on_data_  = std::move(on_data);
      on_close_ = std::move(on_close);

      return conn;
    }

    bool listen(const char address[], uint32_t port, ConnectionCallback on_connection, DataCallback on_data,
        CloseCallback on_close) override {
      IPType net_type = ip_address_version(std::string(address));
      if (net_type == IPType::kInvalid) {
        return false;
      }

      sockaddr_in local_addr  = { 0 };
      local_addr.sin_family   = AF_INET;
      local_addr.sin_port     = htons(port);
      if (inet_pton(AF_INET, address, &local_addr.sin_addr) <= 0) {
        return false;
      }

      SOCKET sock_handle = ::socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
      if (sock_handle == invalid_socket) {
        return false;
      }

      int reuse_addr = 1;
      int result =
          ::setsockopt(sock_handle, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<char*>(&reuse_addr), 
              sizeof(reuse_addr));
      if (result == SOCKET_ERROR) {
        return false;
      }

      result = ::bind(sock_handle, reinterpret_cast<sockaddr*>(&local_addr), sizeof(local_addr));
      if (result == SOCKET_ERROR) {
        return false;
      }

      result = ::listen(sock_handle, 8);
      if (result == SOCKET_ERROR) {
        return false;
      }

      if (!Socket::_set_non_blocking(sock_handle)) {
        return false;
      }

      sock_listener_ = new listener(sock_handle);
      epoll_event ev{ .events = EPOLLIN | EPOLLET, .data.ptr = sock_listener_ };
      result = epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, sock_handle, &ev);
      if (result != 0) {
        close(sock_handle);
        delete sock_listener_;
        sock_listener_ = nullptr;
        return false;
      }

      on_connection_  = std::move(on_connection);
      on_data_        = std::move(on_data);
      on_close_       = std::move(on_close);

      return true;
    }

    int poll() {
      if (epoll_fd_ == -1 || sock_listener_ == nullptr || !sock_listener_->is_valid()) {
        return -1;
      }

      int count = epoll_wait(epoll_fd_, epoll_events_, max_epoll_event_count, 0);
      for (int i = 0; i < count; i++) {
        epoll_event*  ev    = &epoll_events_[i];
        Socket*       conn  = static_cast<Socket*>(ev->data.ptr);

        // Fatal error
        if (conn == nullptr) {
          shut();
          return -1;
        }

        // If enable to write data, do it first whatever this socket has error
        if (ev->events & EPOLLOUT) {
          conn->_try_write_when_io_event_coming();
        }

        // When EPOLLHUP happend, read data first and then deal this error
        if ((ev->events & EPOLLIN) || (ev->events & EPOLLHUP)) {
          _try_read(conn);
          continue;
        }

        // Global errno is valid only system call failed.
        // But in poll funciton, system call epoll_wait is succeed.
        // So EPOLLERR and EPOLLHUP must use getsockopt(SO_ERROR optional) to get error code.
        if (ev->events & EPOLLERR) {
          int       err_code  = 0;
          socklen_t err_len   = sizeof(err_code);
          getsockopt(conn->native_handle(), SOL_SOCKET, SO_ERROR, &err_code, &err_len);
          conn->_close_handle(err_code);
          continue;
        }

        if ((ev->events & EPOLLHUP)) {
          conn->_close_handle();
          continue;
        }

        if (conn->user_closed_) {
          continue;
        }

        if (conn->_is_listener()) {
          _wait_new_connection();
          if (sock_listener_->err_ != 0 && on_listen_err_ != nullptr) {
            on_listen_err_(sock_listener_->err_);
          }
          continue;
        }
      }

      return 0;
    }
  private:
    void _wait_new_connection() {
      if (sock_listener_ == nullptr || !sock_listener_->is_valid()) {
        return;
      }

      sockaddr_in remote_addr = { 0 };
      socklen_t   addr_len    = sizeof(remote_addr);

      int handle = accept(sock_listener_->native_handle(), (struct sockaddr*)(&remote_addr), &addr_len);
      if (handle == -1) {
        if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) {
          return ;
        }

        // TODO: error
        sock_listener_->_close_handle(errno);
        return;
      }

      if (!Socket::_set_non_blocking(handle)) {
        return;
      }

      auto conn = new Socket(handle, _cleaner(), epoll_fd_);
      char ipv4_addr_str[16] = { 0 };
      inet_ntop(AF_INET, &remote_addr.sin_addr, ipv4_addr_str, sizeof(ipv4_addr_str));
      conn->_set_remote_addr(ipv4_addr_str, ntohs(remote_addr.sin_port));

      epoll_event ev{ .events = EPOLLIN | EPOLLET, .data.ptr = conn };
      epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, handle, &ev);

      conns_.emplace(conn->native_handle(), conn);

      if (on_connection_ != nullptr) {
        on_connection_(conn);
      }
    }

    void _try_read(Socket* conn) {
      auto    conn_fd       = conn->native_handle();
      int     read_n        = -1;
      size_t  readed_total  = 0;

      while (true) {
        auto data = conn->read_buff_->data();
        read_n = recv(conn->native_handle(), data, conn->read_buff_->writable_size(), 0);
        if (read_n > 0) {
          readed_total += read_n;
          conn->read_buff_->_add_written_from_io(read_n);
          if (on_data_ != nullptr) {
            on_data_(conn, conn->read_buff_->data(), conn->read_buff_->writable_size());
          }
          conn->read_buff_->clear();
          continue;
        } 
        
        if (read_n == 0) {
          conn->_close_handle(get_last_error());
          break;
        } 
        
        int err_code = get_last_error();
        if (adjust_io_error_option(err_code) == ErrorOption::kNext) {
          break;
        } 
        
        if (adjust_io_error_option(err_code) == ErrorOption::kContinue) {
          continue;
        } 
          
        conn->_close_handle(err_code);
        break;
      }
    }
  };
} // namespace coxnet

#endif // __linux__

#endif // POLLER_LINUX_H
