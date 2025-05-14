#ifndef POLLER_H
#define POLLER_H

#include "io_def.h"
#include "socket.h"

#include <functional>
#include <thread>
#include <chrono>
#include <ranges>
#include <unordered_map>

namespace coxnet {
  class IPoller {
  public:
    IPoller() {
      cleaner_ = new Cleaner([this](const socket_t handle) {
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

    virtual ~IPoller() { if (cleaner_ != nullptr) delete cleaner_; };
    virtual void shut() {};
    virtual void poll() = 0;
    virtual Socket* connect(const char address[], const uint32_t port,
      DataCallback on_data, CloseCallback on_close) = 0;
    virtual bool listen(const char address[], const uint32_t port,
      ConnectionCallback on_connection, DataCallback on_data, CloseCallback on_close) = 0;
  protected:
    void _close_conns() {
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
      cleaner_->clear();

      on_connection_  = nullptr;
      on_data_        = nullptr;
      on_close_       = nullptr;
    }
    void _cleanup() const { cleaner_->traverse(); }
    Cleaner* _cleaner() const { return cleaner_; }
  protected:
    ConnectionCallback  on_connection_  = nullptr;
    DataCallback        on_data_        = nullptr;
    CloseCallback       on_close_       = nullptr;
    ListenErrorCallback on_listen_err_  = nullptr;
    Cleaner*                                cleaner_      = nullptr;
    std::unordered_map<socket_t, Socket*>   conns_;
    listener*                               sock_listener_ = nullptr;
  };
} // namespace coxnet

#endif // POLLER_H
