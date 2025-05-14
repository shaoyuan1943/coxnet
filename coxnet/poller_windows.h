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
    if (context->Conn == nullptr) {
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
    context->Conn->read_buff_->_add_written_from_io(transferred_bytes);
  }

  class Poller final : public IPoller {
  public:
    Poller() = default;
    ~Poller() override = default;

    Poller(const Poller&) = delete;
    Poller& operator=(const Poller&) = delete;
    Poller(Poller&& other) = delete;
    Poller& operator=(Poller&& other) = delete;

    Socket* connect(const char address[], const uint32_t port,
      DataCallback on_data, CloseCallback on_close) override {
      IPType net_type = ip_address_version(std::string(address));
      if (net_type == IPType::kInvalid) {
        return nullptr;
      }

      socket_t sock_handle = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
      if (sock_handle == invalid_socket) {
        return nullptr;
      }

      sockaddr_in remote_addr = {};
      remote_addr.sin_family  = AF_INET;
      remote_addr.sin_port    = htons(port);
      if (inet_pton(AF_INET, address, &remote_addr.sin_addr) <= 0) {
        return nullptr;
      }

      int result = ::connect(sock_handle, reinterpret_cast<sockaddr*>(&remote_addr), sizeof(sockaddr_in));
      if (result == SOCKET_ERROR) { // TODO: async operation is in progress, ignore this error code
        if (const int err_code = get_last_error(); (err_code != WSAEWOULDBLOCK && err_code != WSA_IO_PENDING)) {
          return nullptr;
        }
      }

      if (!Socket::_set_non_blocking(sock_handle)) {
        return nullptr;
      }

      fd_set write_set;
      FD_ZERO(&write_set);
      FD_SET(sock_handle, &write_set);

      constexpr timeval timeout{ 5, 0 };
      // use select to ensure connect operation succeed
      result = select(static_cast<int>(sock_handle + 1), nullptr, &write_set, nullptr, &timeout);
      if (result != 1) {
        closesocket(sock_handle);
        return nullptr;
      }

      result = ::BindIoCompletionCallback(reinterpret_cast<HANDLE>(sock_handle), IOCompletionCallBack, 0);
      if (!result) {
        closesocket(sock_handle);
        return nullptr;
      }

      auto conn = new Socket(sock_handle, this->_cleaner());
      conn->_set_remote_addr(address, port);
      conns_.emplace(conn->native_handle(), conn);

      on_data_  = std::move(on_data);
      on_close_ = std::move(on_close);

      // IMPORTANT: trigger first io
      conn->io_completed_ = true;
      return conn;
    }

    bool listen(const char address[], const uint32_t port,
      ConnectionCallback on_connection, DataCallback on_data,
        CloseCallback on_close) override {
      const IPType net_type = ip_address_version(std::string(address));
      if (net_type == IPType::kInvalid) {
        return false;
      }

      sockaddr_in local_addr = {};
      local_addr.sin_family = AF_INET;
      local_addr.sin_port = htons(port);
      if (inet_pton(AF_INET, address, &local_addr.sin_addr) <= 0) {
        return false;
      }

      SOCKET native_handle = ::socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
      if (native_handle == invalid_socket) {
        return false;
      }

      int reuse_addr = 1;
      int result =
          ::setsockopt(native_handle, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<char*>(&reuse_addr), sizeof(reuse_addr));
      if (result == SOCKET_ERROR) {
        return false;
      }

      result = ::bind(native_handle, reinterpret_cast<sockaddr*>(&local_addr), sizeof(local_addr));
      if (result == SOCKET_ERROR) {
        return false;
      }

      result = ::listen(native_handle, 8);
      if (result == SOCKET_ERROR) {
        return false;
      }

      if (!Socket::_set_non_blocking(native_handle)) {
        return false;
      }

      sock_listener_  = new listener(native_handle);
      on_connection_  = std::move(on_connection);
      on_data_        = std::move(on_data);
      on_close_       = std::move(on_close);

      return true;
    }

    void poll() override { _poll(); _cleanup(); }
  protected:
    void _poll() {
      if (sock_listener_ == nullptr || !sock_listener_->is_valid()) {
        return;
      }

      _wait_new_connection();

      for (auto& [handle, conn] : conns_) {
        _try_read(conn);
        _try_write_async(conn);
      }
    }
  private:
    void _wait_new_connection() {
      socket_t    handle        = invalid_socket;
      sockaddr_in remote_addr   = {};
      int         addr_len      = sizeof(sockaddr_in);
      int         event_count   = 0;

      while (sock_listener_ != nullptr && sock_listener_->is_valid()) {
        memset(&remote_addr, 0, sizeof(sockaddr_in));

        handle = ::accept(sock_listener_->native_handle(), reinterpret_cast<sockaddr*>(&remote_addr), &addr_len);
        if (handle == invalid_socket) {
          break;
        }

        event_count++;

        if (!Socket::_set_non_blocking(handle)) {
          continue;
        }

        if (!::BindIoCompletionCallback(reinterpret_cast<HANDLE>(handle), IOCompletionCallBack, 0)) {
          closesocket(handle);
          continue;
        }

        auto conn = new Socket(handle, this->_cleaner());
        char ipv4_addr_str[16] = { 0 };
        inet_ntop(AF_INET, &remote_addr.sin_addr, ipv4_addr_str, sizeof(ipv4_addr_str));
        conn->_set_remote_addr(ipv4_addr_str, ntohs(remote_addr.sin_port));

        conns_.emplace(conn->native_handle(), conn);
        if (on_connection_ != nullptr) {
          on_connection_(conn);
        }

        // IMPORTANT: trigger first io
        conn->io_completed_ = true;
      }
    }

    void _try_read(Socket* conn) {
      if (!conn->io_completed_) {
        return;
      }

      if (conn->read_buff_->written_size() > 0 && on_data_ != nullptr) {
        on_data_(conn, conn->read_buff_->data(), conn->read_buff_->written_size());
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
