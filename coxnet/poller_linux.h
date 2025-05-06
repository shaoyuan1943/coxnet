#ifndef POLLER_LINUX_H
#define POLLER_LINUX_H

#ifdef __linux__

#include "io_def.h"
#include "socket.h"
#include "cleaner.h"

#include <cassert>
#include <thread>
#include <chrono>
#include <set>

namespace coxnet {
    class Poller : public Cleaner {
    public:
        Poller() = default;
        ~Poller() { shut(); }

        Poller(const Poller&) = delete;
        Poller& operator=(const Poller&) = delete;
        Poller(Poller&& other) = delete;
        Poller& operator=(Poller&& other) = delete;

        bool setup() {
            epoll_events_   = new epoll_event[max_epoll_event_count];
            epoll_fd_       = epoll_create1(EPOLL_CLOEXEC);
            if (epoll_fd_ == -1) {
                return false;
            }

            return true;
        }

        Socket* connect(const char address[], uint32_t port, DataCallback on_data, CloseCallback on_close) {
            IPType net_type = ip_address_version(std::string(address));
            if (net_type == IPType::kInvalid) {
                return nullptr;
            }

            socket_t sock_handle = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
            if (sock_handle == invalid_socket) {
                return nullptr;
            }

            sockaddr_in remote_addr = {};
            remote_addr.sin_family       = AF_INET;
            remote_addr.sin_port         = htons(port);
            if (inet_pton(AF_INET, address, &remote_addr.sin_addr) <= 0) {
                return nullptr;
            }

            int result = ::connect(sock_handle, reinterpret_cast<sockaddr*>(&remote_addr), sizeof(sockaddr_in));
            if (result == SOCKET_ERROR) {
                if (errno == EINPROGRESS) {
                    // TODO: async operation is in progress, ignore this error code
                } else {
                    return nullptr;
                }
            }

            if (!Socket::_set_non_blocking(sock_handle)) {
                return nullptr;
            }

            fd_set write_set;
            FD_ZERO(&write_set);
            FD_SET(sock_handle, &write_set);

            timeval timeout {5, 0};
            // use select to ensure connect operation succeed
            result = select((int)(sock_handle + 1), nullptr, &write_set, nullptr, &timeout);
            if (result != 1) {
                close(sock_handle);
                return nullptr;
            }
            
            epoll_event ev { .events = EPOLLIN | EPOLLET, .data.ptr = socket };
            int result = epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, sock_handle, &ev);
            if (result != 0) {
                close(sock_handle);
                return nullptr;
            }

            auto socket = new Socket(sock_handle, static_cast<Cleaner*>(this));
            socket->_set_remote_addr(address, port);
            conns_.emplace(socket->native_handle(), socket);
            
            on_data_    = std::move(on_data);
            on_close_   = std::move(on_close);

            return socket;
        }

        bool listen(const char address[], uint32_t port, ConnectionCallback on_connection, DataCallback on_data, CloseCallback on_close) {
            IPType net_type = ip_address_version(std::string(address));
            if (net_type == IPType::kInvalid) {
                return false;
            }

            sockaddr_in local_addr      = { 0 };
            local_addr.sin_family       = AF_INET;
            local_addr.sin_port         = htons(port);
            if (inet_pton(AF_INET, address, &local_addr.sin_addr) <= 0) {
                return false;
            }

            SOCKET sock_fd = ::socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
            if (sock_fd == invalid_socket) {
                return false;
            }

            int reuse_addr = 1;
            int result = ::setsockopt(sock_fd, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<char*>(&reuse_addr), sizeof(reuse_addr));
            if (result == SOCKET_ERROR) {
                return false;
            }

            result = ::bind(sock_fd, reinterpret_cast<sockaddr*>(&local_addr), sizeof(local_addr));
            if (result == SOCKET_ERROR) {
                return false;
            }

            result = ::listen(sock_fd, 8);
            if (result == SOCKET_ERROR) {
                return false;
            }

            if (!Socket::_set_non_blocking(sock_fd)) {
                return false;
            }

            epoll_event ev { .events = EPOLLIN | EPOLLET, .data.ptr = sock_listener_ };
            result = epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, sock_fd, &ev);
            if (result != 0) {
                return false;
            }

            sock_listener_  = new listener(sock_fd);
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
                epoll_event* ev = &epoll_events_[i];
                Socket* socket = static_cast<Socket*>(ev->data.ptr);
                
                // Fatal error
                if (socket == nullptr) {
                    shut();
                    return -1;
                }

                // If enable to write data, do it first whatever this socket has error
                if (ev->events & EPOLLOUT) {
                    socket->_try_write_when_io_event_coming();
                }

                // When EPOLLHUP happend, read data first and then deal this error
                if ((ev->events & EPOLLIN) || (ev->events & EPOLLHUP)) {
                    _try_read(socket);
                    continue;
                }
                
                // Global errno is valid only system call failed.
                // But in poll funciton, system call epoll_wait is succeed.
                // So EPOLLERR and EPOLLHUP must use getsockopt(SO_ERROR optional) to get error code. 
                if (ev->events & EPOLLERR) {
                    int         err_code    = 0;
                    socklen_t   err_len     = sizeof(err_code);
                    getsockopt(socket->native_handle(), SOL_SOCKET, SO_ERROR, &err_code, &err_len);
                    socket->_close_handle(err_code);
                    continue;
                }
                
                if ((ev->events & EPOLLHUP)) {
                    socket->_close_handle();
                    continue;
                }

                if (socket->user_closed_) {
                    continue;
                }

                if (socket->_is_listener()) {
                    if (_wait_new_connection() != 0) {
                        if (on_listen_err_ != nullptr) {
                            on_listen_err_(sock_listener_->err_);
                        }
                        break;
                    }
                    continue;
                }
            }

            _cleanup();
            return 0;
        }

        void shut() {
            for (auto kv : conns_) {
                kv.second->_close_handle();
            }

            if (sock_listener_ != nullptr) {
                sock_listener_->_close_handle();
            }

            // sleep 100ms, wait io event
            std::this_thread::sleep_for(std::chrono::milliseconds(100));

            for (auto kv : conns_) {
                delete kv.second;
            }
            conns_.clear();
            Cleaner::clean_handles_.clear();
            
            if (epoll_fd_ != -1) {
                close(epoll_fd_);
                epoll_fd_ = -1;
            }

            if (epoll_events_ != nullptr) {
                delete[] epoll_events_;
                epoll_events_ = nullptr;
            }

            if (sock_listener_ != nullptr) {
                delete sock_listener_;
                sock_listener_ = nullptr;
            }
            
            on_connection_  = nullptr;
            on_data_        = nullptr;
            on_close_       = nullptr;
        }
    private:
        void _cleanup() {
            for (socket_t handle : Cleaner::clean_handles_) {
                auto finder = conns_.find(handle);
                if (finder != conns_.end()) {
                    auto socket = finder->second;
                    if (on_close_ != nullptr) {
                        on_close_(socket, socket->user_closed_ ? 0 : socket->err_);
                    }

                    conns_.erase(finder);
                    delete socket;
                }
            }

            if (Cleaner::clean_handles_.size() != 0) {
                Cleaner::clean_handles_.clear();
            }
        }

        int _wait_new_connection() {
            int result = -1;
            if (sock_listener_ == nullptr || !sock_listener_->is_valid()) {
                return result;
            }

            sockaddr_in remote_addr     = { 0 };
            socklen_t   addr_len        = sizeof(remote_addr);
            
            int handle = accept(sock_listener_->native_handle(), (struct sockaddr*)(&remote_addr), &addr_len);
            if (handle == -1) {
                if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) {
                    return 0;
                }

                // TODO: error
                sock_listener_->_close_handle(errno);
                return -1;
            }

            if(!Socket::_set_non_blocking(handle)) {
                return get_last_error();
            }

            auto conn = new Socket(handle, static_cast<Cleaner*>(this));
            char ipv4_addr_str[16] = { 0 };
            inet_ntop(AF_INET, &remote_addr.sin_addr, ipv4_addr_str, sizeof(ipv4_addr_str));
            conn->_set_remote_addr(ipv4_addr_str, ntohs(remote_addr.sin_port));

            epoll_event ev { .events = EPOLLIN | EPOLLET, .data.ptr = conn };
            epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, handle, &ev);
            
            conns_.emplace(conn->native_handle(), conn);
            
            if (on_connection_ != nullptr) {
                on_connection_(conn);
            }

            return 0;
        }

        void _try_read(Socket* conn) {
            auto    conn_fd         = conn->native_handle();
            int     read_n          = -1;
            size_t  readed_total    = 0;

            while (true) {
                auto data           = conn->read_buff_->data();
                read_n = recv(conn->native_handle(), data, conn->read_buff_->writable_size(), 0);
                if (read_n > 0) {
                    readed_total += read_n;
                    conn->read_buff_->_add_written_from_io(read_n);
                    if (on_data_ != nullptr) {
                        on_data_(conn, conn->read_buff_->data(), conn->read_buff_->writable_size());
                    }
                    conn->read_buff_->clear();
                    continue;
                } else if (read_n == 0) {
                    conn->_close_handle(get_last_error());
                    break;
                } else {
                    int err_code = get_last_error();
                    if (adjust_io_error_option(err_code) == ErrorOption::kNext) {
                        break;
                    } else if (adjust_io_error_option(err_code) == ErrorOption::kContinue) {
                        continue;
                    } else {
                        conn->_close_handle(err_code);
                        break;
                    }
                }  
            }
        }
    private:
        listener*                               sock_listener_      = nullptr;

        int                                     epoll_fd_           = -1;
        epoll_event*                            epoll_events_       = nullptr;

        std::unordered_map<socket_t, Socket*>   conns_              = {};

        ConnectionCallback                      on_connection_      = nullptr;
        DataCallback                            on_data_            = nullptr;
        CloseCallback                           on_close_           = nullptr;
        ListenErrorCallback                     on_listen_err_      = nullptr;
    };
}

#endif // __linux__

#endif //POLLER_LINUX_H
