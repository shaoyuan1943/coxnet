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
    Poller() {
      epoll_events_ = new epoll_event[max_epoll_event_count];
      epoll_fd_     = epoll_create1(EPOLL_CLOEXEC);
      assert(epoll_fd_);
    }

    ~Poller() = default;
    Poller(const Poller&) = delete;
    Poller& operator=(const Poller&) = delete;
    Poller(Poller&& other) = delete;
    Poller& operator=(Poller&& other) = delete;

    Socket* connect(const char address[], const uint16_t port, DataCallback on_data, CloseCallback on_close) override {
      IPType net_type = ip_address_version(std::string(address));
      if (net_type == IPType::kInvalid) {
        return nullptr;
      }

      int               af_family = 0;
      sockaddr_storage  remote_addr_storage;
      socklen_t         addr_len;
      memset(&remote_addr_storage, 0, sizeof(remote_addr_storage));

      if (net_type == IPType::kIPv4) {
        af_family                 = AF_INET;
        sockaddr_in* remote_addr  = reinterpret_cast<sockaddr_in*>(&remote_addr_storage);
        remote_addr->sin_family   = af_family;
        remote_addr->sin_port     = htons(port);
        if (inet_pton(af_family, address, &remote_addr->sin_addr) <= 0) {
          return nullptr; // Invalid address format
        }

        addr_len = sizeof(sockaddr_in);
      }

      if (net_type == IPType::kIPv6) {
        af_family                   = AF_INET6;
        sockaddr_in6* remote_addr6  = reinterpret_cast<sockaddr_in6*>(&remote_addr_storage);
        remote_addr6->sin6_family   = af_family;
        remote_addr6->sin6_port     = htons(port);
        if (inet_pton(af_family, address, &remote_addr6->sin6_addr) <= 0) {
          return nullptr; // Invalid address format
        }
        addr_len = sizeof(sockaddr_in6);
      }

      socket_t sock_handle = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
      if (sock_handle == invalid_socket) {
        return nullptr;
      }

      if (!Socket::_set_non_blocking(sock_handle)) {
        ::close(sock_handle);
        return nullptr;
      }

      int result = ::connect(sock_handle, reinterpret_cast<sockaddr*>(&remote_addr_storage), addr_len);
      if (result == SOCKET_ERROR) { // TODO: async operation is in progress, ignore this error code
        if (get_last_error() != EINPROGRESS) {
          ::close(sock_handle);
          return nullptr;
        }
      }

      if (result == SOCKET_ERROR && get_last_error() == EINPROGRESS) {
        fd_set write_set;
        FD_ZERO(&write_set);
        FD_SET(sock_handle, &write_set);

        timeval timeout{ 5, 0 };
        // use select to ensure connect operation succeed
        result = select((int)(sock_handle + 1), nullptr, &write_set, nullptr, &timeout);
        if (result != 1) {
          ::close(sock_handle);
          return nullptr;
        }
      }

      auto conn = new Socket(sock_handle, _cleaner(), epoll_fd_);
      epoll_event ev{ .events = EPOLLIN | EPOLLET, .data.ptr = conn };
      int result = epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, sock_handle, &ev);
      if (result != 0) {
        ::close(sock_handle);
        delete conn;
        return nullptr;
      }

      conn->_set_remote_addr(address, port);
      conns_.emplace(conn->native_handle(), conn);

      on_data_  = std::move(on_data);
      on_close_ = std::move(on_close);

      return conn;
    }

    bool listen(const char address[], uint16_t port, SocketStack stack, 
                ConnectionCallback on_connection, DataCallback on_data, CloseCallback on_close) override {
      IPType net_type = ip_address_version(std::string(address));
      if (net_type == IPType::kInvalid) {
        return false;
      }

      if (sock_listener_ != nullptr) { // Already listening
        return false;
      }

      int af_family = 0;
      if (strcmp(address, "0.0.0.0") == 0) { // Wildcard IPv4
        if (stack == SocketStack::kOnlyIPv6) { return false; } // Cannot bind 0.0.0.0 if only IPv6
        af_family = AF_INET;
        if (stack == SocketStack::kDualStack) {
          // If dual stack is preferred for "0.0.0.0", we actually want to listen on "::" (IPv6)
          // and set IPV6_V6ONLY = 0. The user should pass "::" for this.
          // Here, "0.0.0.0" with kDualStack means listen on IPv4 AND if "::" is also listened, it's separate.
          // For simplicity, if "0.0.0.0" is given, stick to IPv4 unless explicitly kOnlyIPv6 (which is an error).
        }
      } else if (strcmp(address, "::") == 0) { // Wildcard IPv6
        if (stack == SocketStack::kOnlyIPv4) { return false; } // Cannot bind :: if only IPv4
        af_family = AF_INET6;
      } else if (net_type == IPType::kIPv4) {
        if (stack == SocketStack::kOnlyIPv6) { return false; }
        af_family = AF_INET;
      } else if (net_type == IPType::kIPv6) {
        if (stack == SocketStack::kOnlyIPv4) { return false; }
        af_family = AF_INET6;
      } else { // Invalid address string
        return false;
      }
      
      if (af_family == 0) {
        return false;
      }

      sockaddr_storage  local_addr_storage;
      socklen_t         addr_len;
      memset(&local_addr_storage, 0, sizeof(local_addr_storage));

      if (af_family == AF_INET) {
        sockaddr_in* local_addr   = reinterpret_cast<sockaddr_in*>(&local_addr_storage);
        local_addr->sin_family    = AF_INET;
        local_addr->sin_port      = htons(port);
        if (inet_pton(AF_INET, address, &local_addr->sin_addr) <= 0) {
          return false;
        }
        addr_len = sizeof(sockaddr_in);
      } else { // AF_INET6
        sockaddr_in6* local_addr6 = reinterpret_cast<sockaddr_in6*>(&local_addr_storage);
        local_addr6->sin6_family = AF_INET6;
        local_addr6->sin6_port   = htons(port);
        if (inet_pton(AF_INET6, address, &local_addr6->sin6_addr) <= 0) {
          return false;
        }
        addr_len = sizeof(sockaddr_in6);
      }

      socket_t sock_handle = ::socket(af_family, SOCK_STREAM, IPPROTO_TCP); // Use IPPROTO_TCP for stream
      if (sock_handle == invalid_socket) {
        return false;
      }

      int reuse_addr = 1;
      if (::setsockopt(sock_handle, SOL_SOCKET, SO_REUSEADDR, &reuse_addr, sizeof(reuse_addr)) == SOCKET_ERROR) {
        ::close(get_last_error()); return false;
      }

      // For dual-stack operation on an AF_INET6 socket listening on "::"
      if (af_family == AF_INET6 && 
        (stack == SocketStack::kDualStack) &&
        (strcmp(address, "::") == 0 || (strcmp(address, "0.0.0.0") == 0 && net_type == IPType::kInvalid))) { // "0.0.0.0" could imply "::" for dual stack
        int ipv6_only = 0; // 0 means dual-stack (accept IPv4 as v4-mapped v6)
        if (stack == SocketStack::kOnlyIPv6) { ipv6_only = 1; } 
        if (::setsockopt(sock_handle, IPPROTO_IPV6, IPV6_V6ONLY, &ipv6_only, sizeof(ipv6_only)) == SOCKET_ERROR) {
          // Non-critical on some older systems, but log it. Could be an issue.
          // For now, let's treat as error if dual-stack was intended.
          if (ipv6_only == 0) { // If we tried to enable dual-stack and failed
            ::close(sock_handle); return false;
          }
        }
      }

      if (::bind(sock_handle, reinterpret_cast<sockaddr*>(&local_addr_storage), addr_len) == SOCKET_ERROR) {
        ::close(sock_handle); return false;
      }

      if (::listen(sock_handle, SOMAXCONN) == SOCKET_ERROR) { // Use SOMAXCONN for backlog
        ::close(sock_handle); return false;
      }

      if (!Socket::_set_non_blocking(sock_handle)) {
        ::close(sock_handle); return false;
      }

      // Listener does not need epoll_fd in its Socket object, pass -1 or specific marker.
      // The listener socket itself is added to epoll here.
      sock_listener_ = new listener(sock_handle); 
      // Listener socket is also a Socket object, so it's data.ptr.
      // No EPOLLOUT needed for listener. EPOLLRDHUP for peer close detection (less common for listener).
      epoll_event ev{ .events = EPOLLIN | EPOLLET, .data.ptr = sock_listener_ };
      if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, sock_handle, &ev) != 0) {
        ::close(sock_handle);
        delete sock_listener_;
        return false;
      }

      on_connection_  = std::move(on_connection);
      on_data_        = std::move(on_data);
      on_close_       = std::move(on_close);

      return true;
    }
    
    void poll() override {
      if (epoll_fd_ == -1) { return; }

      _poll(); 
      _cleanup(); 
    }

    void shut() override {
      if (sock_listener_ != nullptr && sock_listener_->native_handle() != invalid_socket) {
        epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, sock_listener_->native_handle(), nullptr);
      }

      IPoller::_close_conns();

      if (epoll_fd_ != -1) {
        close(epoll_fd_);
        epoll_fd_ = -1;
      }

      delete[] epoll_events_;
      epoll_events_ =nullptr;

      delete sock_listener_;
      sock_listener_ = nullptr;
    }
  protected:
    void _poll() {
      if (epoll_fd_ == -1 || epoll_events_ == nullptr) {
        return;
      }

      int count = epoll_wait(epoll_fd_, epoll_events_, max_epoll_event_count, 0);
      for (int i = 0; i < count; i++) {
        epoll_event*  ev    = &epoll_events_[i];
        Socket*       conn  = static_cast<Socket*>(ev->data.ptr);

        // Fatal error if conn is nil
        if (conn == nullptr) {
          assert(false && "epoll_wait returned a null socket ptr");
          continue;
        }

        bool is_listener_event = (sock_listener_ && conn == sock_listener_);
        if (ev->events & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)) {
          if (is_listener_event) {
            int err_code = 0;
            socklen_t err_len = sizeof(err_code);
            getsockopt(conn->native_handle(), SOL_SOCKET, SO_ERROR, &err_code, &err_len);
            if (on_listen_err_) on_listen_err_(err_code ? err_code : EPIPE); // EPIPE for HUP/RDHUP if no specific error
            
            // Need to shut down the listener properly.
                // It will be cleaned up by the _cleanup mechanism.
                // The Poller might need to stop trying to listen.
            if (conn->native_handle() != invalid_socket) {
              epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, conn->native_handle(), nullptr);
              // sock_listener_ still exists, its _close_handle will be called
            }
             
            sock_listener_->_close_handle(err_code ? err_code : EPIPE); // Mark for cleanup
            // sock_listener_ might be set to nullptr in _cleanup later.
          } else {
            int err_code = 0;
            if (ev->events & EPOLLERR) {
              socklen_t err_len = sizeof(err_code);
              getsockopt(conn->native_handle(), SOL_SOCKET, SO_ERROR, &err_code, &err_len);
            }
            // For EPOLLHUP/EPOLLRDHUP, err_code might be 0. We treat it as a clean close by peer.
            // If data is readable (EPOLLIN is also set), read it before closing.
            if ((ev->events & EPOLLIN) || (ev->events & EPOLLHUP)) {
              _try_read(conn);
            }
            conn->_close_handle(err_code ? err_code : 0); // 0 for HUP/RDHUP if no specific socket error
          }

          continue;
        }

        if (is_listener_event && (ev->events & EPOLLIN)) {
          _wait_new_connection(); 
          if (sock_listener_ && sock_listener_->err_ != 0 && on_listen_err_) {
            on_listen_err_(sock_listener_->err_);
          }

          if(sock_listener_->is_valid()) { continue; }
          continue;
        }

        if (ev->events & EPOLLOUT) {
          conn->_try_write_when_io_event_coming();
          if (conn->is_valid()) { continue; }
        }
        
        if (ev->events & EPOLLIN) {
          _try_read(conn);
          if (conn->is_valid()) { continue; }
        }
      }
    }
  private:
    void _wait_new_connection() {
      if (sock_listener_ == nullptr || !sock_listener_->is_valid() || epoll_fd_ == -1) {
        return;
      }

      while (true) { // Edge-triggered, so accept all pending
        sockaddr_storage  remote_addr_storage; // Use storage for IPv4/IPv6
        socklen_t         addr_len    = sizeof(remote_addr_storage);
        memset(&remote_addr_storage, 0, sizeof(remote_addr_storage));

        socket_t handle = ::accept4(sock_listener_->native_handle(), 
                                   reinterpret_cast<sockaddr*>(&remote_addr_storage), 
                                   &addr_len, SOCK_NONBLOCK | SOCK_CLOEXEC);

        if (handle == invalid_socket) {
          int err_code = get_last_error();
          if (err_code == EINTR || err_code == EAGAIN || err_code == EWOULDBLOCK) {
            break; 
          }

          sock_listener_->_close_handle(err_code);
          if (on_listen_err_) on_listen_err_(err_code);
          break;
        }

        // Socket::_set_non_blocking not needed due to accept4 SOCK_NONBLOCK
        // But if not using accept4, it would be:
        // if (!Socket::_set_non_blocking(handle)) { ::close(handle); continue; }
        
        char      client_ip_str[INET6_ADDRSTRLEN] = { 0 };
        uint16_t  client_port                     = 0;

        switch (remote_addr_storage.ss_family) {
        case AF_INET:
          sockaddr_in* sin = reinterpret_cast<sockaddr_in*>(&remote_addr_storage);
          inet_ntop(AF_INET, &sin->sin_addr, client_ip_str, sizeof(client_ip_str));
          client_port = ntohs(sin->sin_port);
          break;
        case AF_INET6:
          sockaddr_in6* sin6 = reinterpret_cast<sockaddr_in6*>(&remote_addr_storage);
          inet_ntop(AF_INET6, &sin6->sin6_addr, client_ip_str, sizeof(client_ip_str));
          client_port = ntohs(sin6->sin6_port);
          break;
        default:
          break;
        }

        auto conn = new Socket(handle, _cleaner(), epoll_fd_);
        conn->_set_remote_addr(client_ip_str, client_port);

        // Add to epoll. EPOLLRDHUP for peer close.
        epoll_event ev{ .events = EPOLLIN | EPOLLET | EPOLLRDHUP, .data.ptr = conn };
        if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, handle, &ev) != 0) {
          // Failed to add to epoll, close and delete this connection
          // conn destructor will call ::close() if handle is valid.
          // Or call conn->_close_handle() explicitly.
          conn->_close_handle(get_last_error());
          delete conn; // Directly delete, won't go through cleaner normally.
          continue;
        }

        conns_.emplace(conn->native_handle(), conn);

        if (on_connection_ != nullptr) {
          on_connection_(conn);
        }
      }
    }

    void _try_read(Socket* conn) {
      auto    conn_fd       = conn->native_handle();
      int     read_n        = -1;
      size_t  readed_total  = 0;

      while (true) {
        if (conn->read_buff_->writable_size() <= 0) { conn->read_buff_->ensure_writable_size(max_size_per_read); }
        
        auto buffer_start = conn->read_buff_->take_data();
        read_n = ::recv(conn->native_handle(), buffer_start, conn->read_buff_->writable_size(), 0);
        if (read_n > 0) {
          readed_total += read_n;
          conn->read_buff_->add_written_from_external_take(read_n);
          if (on_data_ != nullptr) {
            on_data_(conn, conn->read_buff_->take_data(), conn->read_buff_->writable_size());
          }
          conn->read_buff_->clear();
          continue;
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
  private:
    int                 epoll_fd_       = -1;
    epoll_event*        epoll_events_   = nullptr;
  };
} // namespace coxnet

#endif // __linux__

#endif // POLLER_LINUX_H
