#ifndef POLLER_WINDOWS_H
#define POLLER_WINDOWS_H

#ifdef _WIN32

#include "io_def.h"
#include "poller.h"
#include "socket.h"

#include <chrono>
#include <functional>
#include <thread>
#include <utility>

namespace coxnet {
  static void WINAPI IOCompletionCallBack(DWORD err_code, DWORD transferred_bytes, LPOVERLAPPED over_lapped) {
    RecvContext4Win* context = CONTAINING_RECORD(over_lapped, RecvContext4Win, Overlapped);
    if (context == nullptr || context->Conn == nullptr) {
      return;
    }

    if (err_code == ERROR_SUCCESS) {
      if (transferred_bytes <= 0) {
        err_code = 104; // 104 is ECONNRESET in linux, it's mean of connection reset by peer
      }
    }

    if (err_code != 0) {
      context->Conn->_close_handle(static_cast<int>(err_code));
    }

    context->Conn->io_completed_ = true;
    context->Conn->read_buff_->add_written_from_external_take(transferred_bytes);
  }

  class Poller final : public IPoller {
  public:
    Poller() = default;
    ~Poller() override = default;

    Poller(const Poller&) = delete;
    Poller& operator=(const Poller&) = delete;
    Poller(Poller&& other) = delete;
    Poller& operator=(Poller&& other) = delete;

    Socket* connect(const char address[], const uint16_t port,
                    DataCallback on_data, CloseCallback on_close) override {
      IPType ip_type = ip_address_type(std::string(address));
      if (ip_type == IPType::kInvalid) {
        return nullptr;
      }

      int               af_family           = 0;
      sockaddr_storage  remote_addr_storage = {};
      int               addr_len            = 0;
      memset(&remote_addr_storage, 0, sizeof(remote_addr_storage));

      if (ip_type == IPType::kIPv4) {
        af_family                 = AF_INET;
        sockaddr_in* remote_addr  = reinterpret_cast<sockaddr_in*>(&remote_addr_storage);
        remote_addr->sin_family   = af_family;
        remote_addr->sin_port     = htons(port);
        if (InetPtonA(af_family, address, &remote_addr->sin_addr) != 1) { // Use InetPtonA for char*
          return nullptr;
        }
        addr_len = sizeof(sockaddr_in);
      }

      if (ip_type == IPType::kIPv6) {
        af_family                   = AF_INET6;
        sockaddr_in6* remote_addr6  = reinterpret_cast<sockaddr_in6*>(&remote_addr_storage);
        remote_addr6->sin6_family   = af_family;
        remote_addr6->sin6_port     = htons(port);
        if (InetPtonA(af_family, address, &remote_addr6->sin6_addr) != 1) {
          return nullptr;
        }
        addr_len = sizeof(sockaddr_in6);
      }

      socket_t sock_handle = ::socket(af_family, SOCK_STREAM, IPPROTO_TCP);
      if (sock_handle == invalid_socket) {
        return nullptr;
      }

      if (!Socket::_set_non_blocking(sock_handle)) {
        closesocket(sock_handle);
        return nullptr;
      }

      int result = ::connect(sock_handle, reinterpret_cast<sockaddr*>(&remote_addr_storage), addr_len);
      if (result == SOCKET_ERROR) {
        if (get_last_error() != WSAEWOULDBLOCK) {
          closesocket(sock_handle);
          return nullptr;
        }
        // WSAEWOULDBLOCK is expected, use select to wait for connection.
        fd_set write_set = {};
        FD_ZERO(&write_set);
        FD_SET(sock_handle, &write_set);
        timeval timeout{ 5, 0 }; // 5-second timeout

        result = select(0, nullptr, &write_set, nullptr, &timeout); // First param ignored on Windows for socket fd_sets
        if (result <= 0) { // Timeout or error
          closesocket(sock_handle);
          return nullptr;
        }
        // Check for error on socket
        int optval = 0;
        int optlen = sizeof(optval);
        if (getsockopt(sock_handle, SOL_SOCKET, SO_ERROR,
                      (char*)&optval, &optlen) == SOCKET_ERROR || optval != 0) {
          closesocket(sock_handle);
          return nullptr;
        }
      }

      if (!::BindIoCompletionCallback(reinterpret_cast<HANDLE>(sock_handle), IOCompletionCallBack, 0)) {
        closesocket(sock_handle);
        return nullptr;
      }

      auto conn = new Socket(sock_handle, this->_cleaner());
      conn->_set_remote_addr(address, port);
      conns_.emplace(conn->native_handle(), conn);

      // Set callbacks on IPoller (these are general for the poller instance)
      on_data_  = std::move(on_data);
      on_close_ = std::move(on_close);

      conn->io_completed_ = true; // IMPORTANT: trigger first async read in _poll loop
      return conn;
    }

    // Only IPv4: address is IPv4, stack is kOnlyIPv4
    // Only IPv6: address is IPv6, stack is kOnlyIPv6
    // Dual: address is IPv6, stack is kDual
    bool listen(const char address[], const uint16_t port, ProtocolStack stack,
                ConnectionCallback on_connection, DataCallback on_data, CloseCallback on_close) override {
      const IPType ip_type = ip_address_type(std::string(address));
      if (ip_type == IPType::kInvalid) {
        return false;
      }

      if (sock_listener_ != nullptr) {
        return false;
      }

      int af_family = 0;
      int dual_mode = 0;
      if (ip_type == IPType::kIPv4 && stack == ProtocolStack::kOnlyIPv4) {
        af_family = AF_INET;
      }

      if (ip_type == IPType::kIPv6 && stack == ProtocolStack::kOnlyIPv6) {
        af_family = AF_INET6;
      }

      // for dual stack: supports both IPv4 and IPv6 simultaneously
      if (ip_type == IPType::kIPv6 && stack == ProtocolStack::kDualStack) {
        af_family = AF_INET6;
        dual_mode = 1;
      }

      if (af_family == 0) { return false; }

      sockaddr_storage  local_addr_storage  = {};
      int               addr_len            = 0; // bind takes int for addr_len
      memset(&local_addr_storage, 0, sizeof(local_addr_storage));

      if (af_family == AF_INET) {
        sockaddr_in* local_addr = reinterpret_cast<sockaddr_in*>(&local_addr_storage);
        local_addr->sin_family  = AF_INET;
        local_addr->sin_port    = htons(port);
        if (InetPtonA(AF_INET, address, &local_addr->sin_addr) != 1) {
          return false;
        }
        addr_len = sizeof(sockaddr_in);
      }

      if (af_family == AF_INET6) { // AF_INET6
        sockaddr_in6* local_addr6 = reinterpret_cast<sockaddr_in6*>(&local_addr_storage);
        local_addr6->sin6_family  = AF_INET6;
        local_addr6->sin6_port    = htons(port);
        if (InetPtonA(AF_INET6, address, &local_addr6->sin6_addr) != 1) {
          return false;
        }
        addr_len = sizeof(sockaddr_in6);
      }

      socket_t sock_handle = ::socket(af_family, SOCK_STREAM, IPPROTO_TCP);
      if (sock_handle == invalid_socket) {
        return false;
      }

      int reuse_addr = 1;
      if (::setsockopt(sock_handle, SOL_SOCKET, SO_REUSEADDR,
          reinterpret_cast<char*>(&reuse_addr), sizeof(reuse_addr)) == SOCKET_ERROR) {
        closesocket(sock_handle);
        return false;
      }

      // for dual protocol stack
      if (af_family == AF_INET6 && dual_mode == 1) {
        DWORD ipv6_only = 0;
        if (::setsockopt(sock_handle, IPPROTO_IPV6, IPV6_V6ONLY,
            reinterpret_cast<char*>(&ipv6_only), sizeof(ipv6_only)) == SOCKET_ERROR) {
          // On modern Windows, this should succeed and is important for dual-stack.
          if (ipv6_only == 0) {
            closesocket(sock_handle);
            return false;
          }
        }
      }

      if (::bind(sock_handle, reinterpret_cast<sockaddr*>(&local_addr_storage), addr_len) == SOCKET_ERROR) {
        closesocket(sock_handle);
        return false;
      }

      if (::listen(sock_handle, SOMAXCONN) == SOCKET_ERROR) { // Use SOMAXCONN
        closesocket(sock_handle);
        return false;
      }

      // Listener socket should be non-blocking for accept loop
      if (!Socket::_set_non_blocking(sock_handle)) {
        closesocket(sock_handle);
        return false;
      }

      // Listener itself doesn't use IOCP callbacks for accept, it uses a polling accept.
      sock_listener_  = new listener(sock_handle);
      on_connection_  = std::move(on_connection);
      on_data_        = std::move(on_data);
      on_close_       = std::move(on_close);

      return true;
    }

    void poll() override { _poll_once(); _cleanup(); }
    void shut() override {
      if (sock_listener_ != nullptr && sock_listener_->native_handle() != invalid_socket) {
        sock_listener_->_close_handle(0);
      }

      IPoller::_close_conns_internal();

      delete sock_listener_;
      sock_listener_ = nullptr;
    }
  protected:
    void _poll_once() {
      if (sock_listener_ == nullptr || !sock_listener_->is_valid()) {
        return;
      }

      _wait_new_connection();
      if (sock_listener_ && sock_listener_->err_ != 0 && on_listen_err_) {
        on_listen_err_(sock_listener_->err_);
        return;
      }

      for (auto& [handle, conn] : conns_) {
        if (!conn || !conn->is_valid()) { continue; }

        _try_read(conn);
        if (!conn->is_valid()) { continue; }

        _try_write_async(conn);
      }
    }
  private:
    void _wait_new_connection() {
      while (sock_listener_ != nullptr && sock_listener_->is_valid()) {
        sockaddr_storage  remote_addr_storage = {}; // For IPv4/IPv6
        int               addr_len            = sizeof(remote_addr_storage);
        memset(&remote_addr_storage, 0, sizeof(remote_addr_storage));

        socket_t handle = ::accept(sock_listener_->native_handle(), reinterpret_cast<sockaddr*>(&remote_addr_storage), &addr_len);
        if (handle == invalid_socket) {
          int err_code = get_last_error();
          if (err_code == WSAEWOULDBLOCK) {
            break;
          }

          sock_listener_->_close_handle(err_code);
          break;
        }

        if (!Socket::_set_non_blocking(handle)) {
          closesocket(handle);
          continue;
        }

        if (!::BindIoCompletionCallback(reinterpret_cast<HANDLE>(handle), IOCompletionCallBack, 0)) {
          closesocket(handle);
          continue;
        }

        char      client_ip_str[INET6_ADDRSTRLEN] = { 0 };
        uint16_t  client_port                     = 0;
        switch (remote_addr_storage.ss_family) {
        case AF_INET:
          sockaddr_in* sin = reinterpret_cast<sockaddr_in*>(&remote_addr_storage);
          InetNtopA(AF_INET, &sin->sin_addr, client_ip_str, sizeof(client_ip_str));
          client_port = ntohs(sin->sin_port);
          break;
        case AF_INET6:
          sockaddr_in6* sin6 = reinterpret_cast<sockaddr_in6*>(&remote_addr_storage);
          InetNtopA(AF_INET6, &sin6->sin6_addr, client_ip_str, sizeof(client_ip_str));
          client_port = ntohs(sin6->sin6_port);
          break;
        default:
          assert(false, "Unsupport net family");
          break;
        }

        auto conn = new Socket(handle, this->_cleaner());
        conn->_set_remote_addr(client_ip_str, client_port);
        conns_.emplace(conn->native_handle(), conn);
        if (on_connection_ != nullptr) {
          on_connection_(conn);
        }

        conn->io_completed_ = true; // Trigger first async read for this new connection
      }
    }

    void _try_read(Socket* conn) {
      if (!conn || !conn->is_valid() || !conn->read_buff_ || !conn->io_completed_) return;

      if (conn->read_buff_->written_size() > 0 && on_data_ != nullptr) {
        on_data_(conn, conn->read_buff_->take_data(), conn->read_buff_->written_size());
      }

      conn->io_completed_ = false;
      conn->read_buff_->clear();

      conn->_overlapped();

      DWORD recv_bytes  = 0;
      DWORD flags       = 0;
      int result = ::WSARecv(conn->native_handle(),
        &conn->recv_context_for_win_.Buf, 1, &recv_bytes, &flags,
        &conn->recv_context_for_win_.Overlapped, nullptr);
      if (result == SOCKET_ERROR) {
        if (const int err = get_last_error(); adjust_io_error_option(err) == ErrorOption::kClose) {
          conn->_close_handle(err);
          return;
        }
      }
    }

    void _try_write_async(Socket* conn) { conn->_try_write_when_io_event_coming(); }
  };
} // namespace coxnet

#endif //_WIN32

#endif // POLLER_H
