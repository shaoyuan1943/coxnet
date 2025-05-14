#ifndef POLLER_H
#define POLLER_H

#include "io_def.h"
#include "socket.h"

#include <functional>
#include <memory>
#include <thread>
#include <chrono>
#include <ranges>
#include <set>
#include <unordered_map>
#include <cassert>

namespace coxnet {
  class IPoller {
  public:
    IPoller() {
#ifdef __linux__
      epoll_events_ = new epoll_event[max_epoll_event_count];
      epoll_fd_     = epoll_create1(EPOLL_CLOEXEC);
      assert(epoll_fd_);
#endif // __linux__
    }

    virtual ~IPoller() { _shut(); };
    void shut() { _shut(); }
    void poll() { _poll(); _cleanup(); }
    virtual Socket* connect(
        const char address[], const uint32_t port, DataCallback on_data, CloseCallback on_close) = 0;
    virtual bool listen(const char address[], const uint32_t port, ConnectionCallback on_connection,
        DataCallback on_data, CloseCallback on_close) = 0;
  protected:
    void _shut() {
      for(const auto& [handle, conn] : conns_) {
        conn->_close_handle();
      }

      if (sock_listener_ != nullptr) {
        sock_listener_->_close_handle();
      }

      // sleep 100ms, wait io event
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      
      for(auto& [handle, conn] : conns_) {
        delete conn;
      }
      conns_.clear();
      cleaner_.clear();

#ifdef __linux__
      if (epoll_fd_ != -1) {
        close(epoll_fd_);
        epoll_fd_ = -1;
      }

      if (epoll_events_ != nullptr) {
        delete[] epoll_events_;
        epoll_events_ = nullptr;
      }
#endif
      if (sock_listener_ != nullptr) {
        delete sock_listener_;
        sock_listener_ = nullptr;
      }

      on_connection_  = nullptr;
      on_data_        = nullptr;
      on_close_       = nullptr;
    }

    void _cleanup() {
      cleaner_.traverse([this](const socket_t handle) {
        auto finder = conns_.find(handle);
        if (finder != conns_.end()) {
          if (on_close_ != nullptr) {
            on_close_(finder->second, finder->second->user_closed_ ? 0 : finder->second->err_);
          }
          
          delete finder->second;
          conns_.erase(finder);
        }
      });
    }

    Cleaner* _cleaner() { return &cleaner_; }
    virtual void _poll() = 0;
  protected:
    ConnectionCallback  on_connection_  = nullptr;
    DataCallback        on_data_        = nullptr;
    CloseCallback       on_close_       = nullptr;
    ListenErrorCallback on_listen_err_  = nullptr;
#ifdef __linux__
    int                 epoll_fd_       = -1;
    epoll_event*        epoll_events_   = nullptr;
#endif // __linux__
    Cleaner                                 cleaner_;
    std::unordered_map<socket_t, Socket*>   conns_;
    listener*                               sock_listener_;
  };
} // namespace coxnet

#endif // POLLER_H
