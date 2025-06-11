#ifndef POLLER_H
#define POLLER_H

#include "io_def.h"
#include "socket.h"

#include <functional>
#include <thread>
#include <chrono>
#include <ranges>
#include <unordered_map>
#include <atomic>

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

    virtual ~IPoller() { 
      _cleanup();

      delete cleaner_;
      cleaner_ = nullptr;  
    };

    virtual void shut() = 0;
    virtual void poll() = 0;
    virtual Socket* connect(const char address[], const uint16_t port,
                            DataCallback on_data, CloseCallback on_close) = 0;
    virtual bool listen(const char address[], const uint16_t port, ProtocolStack stack, 
                        ConnectionCallback on_connection, DataCallback on_data, CloseCallback on_close) = 0;
    
    void request_shutdown() { shutdown_requested_.store(true); }
    bool is_shutdown_requested() const { return shutdown_requested_.load(); }
  protected:
    void _close_conns_internal() {
      for(const auto& [handle, conn] : conns_) {
        conn->_close_handle();
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
    using Conns = std::unordered_map<socket_t, Socket*>;

    ConnectionCallback  on_connection_      = nullptr;
    DataCallback        on_data_            = nullptr;
    CloseCallback       on_close_           = nullptr;
    ListenErrorCallback on_listen_err_      = nullptr;

    Cleaner*            cleaner_            = nullptr;
    Conns               conns_;
    listener*           sock_listener_      = nullptr;
    std::atomic<bool>   shutdown_requested_ = { false };
  };
} // namespace coxnet

#endif // POLLER_H
