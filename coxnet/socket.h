#ifndef SOCKET_H
#define SOCKET_H

#include <tuple>
#include <functional>
#include <utility>
#include <algorithm>

#include "io_def.h"

namespace coxnet {
    class Poller;
    class SimpleBuffer;
    class Socket;
    using ConnectionCallback    = std::function<void (Socket* conn_socket)>;
    using CloseCallback         = std::function<void (Socket* conn_socket, int err)>;
    using DataCallback          = std::function<void (Socket* conn_socket, const char* data, size_t size)>;
    using ListenErrorCallback   = std::function<void(int err)>;

    class Socket {
    public:
        friend class Poller;
        explicit Socket(socket_t sock) {
            sock_ = sock;
            if (!Socket::_is_listener()) {
                read_buff_  = new SimpleBuffer(max_read_buff_size);
                write_buff_ = new SimpleBuffer(max_write_buff_size);
            }
        }

        virtual ~Socket() {}

        Socket(const Socket&) = delete;
        Socket& operator=(const Socket&) = delete;
        Socket(Socket&& other) = delete;
        Socket& operator=(Socket&& other) = delete;

        socket_t native_handle() const { return sock_; }
        bool is_valid() const { return sock_ != invalid_socket && err_ == 0 && !user_closed_; }

        void user_close() {
            user_closed_ = true;
            _close_handle();
        }

        std::tuple<char*, int> remote_addr() { return std::make_tuple(remote_addr_, remote_port_); }

        int write(const char* data, size_t len) {
            int result = -1;
            if (!is_valid() || user_closed_ || err_ != 0) {
                return result;
            }

            if (write_buff_->written_size() > 0) {
                write_buff_->write_to_tail(data, len);
                return len;
            }

            result = 0;
            const char* write_data  = data;
            size_t      write_size  = len;
            size_t      target_size = len;
            while (write_size > 0) {
                size_t current_try_write_size = std::min<size_t>(max_size_per_write, write_size);
                int written_size = ::send(native_handle(), write_data, current_try_write_size, 0);
                if (written_size == -1) {
                    const int err = Error::get_last_error();
#if defined(_WIN32)
                    if (err == WSAEINTR || err == WSAEWOULDBLOCK) {
                        write_buff_->write_to_tail(write_data, target_size - result);
                        break;
                    }
#endif // _WIN32
                        
#if defined(__linux__)
                    if (err == EINTR || err == EAGAIN || err == EWOULDBLOCK) {
                        write_buff_->write_to_tail(write_data, target_size - result);
                        epoll_event ev { .events = EPOLLIN | EPOLLOUT | EPOLLET, .data.ptr = this };
                        epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, sock_, &ev);
                        break;
                    }
#endif // __linux__
                    _close_handle(err);
                    return -1;
                }

                write_data  += written_size;
                write_size  -= written_size;
                result      += written_size;
            }

            return result;
        }
    private:
        size_t _try_write_when_io_event_coming() {
            if (write_buff_->written_size() <= 0) {
                return -1;
            }
            
            int     result      = 0;
            char*   write_data  = write_buff_->data_from_head();
            size_t  write_size  = write_buff_->written_size();
            size_t  target_size = write_size;

            while (write_size > 0) {
                size_t current_try_write_size = std::min<size_t>(max_size_per_write, write_size);
                int written_size = ::send(native_handle(), write_data, current_try_write_size, 0);
                if (written_size == -1) {
                    const int err = Error::get_last_error();
#if defined(_WIN32)
                    if (err == WSAEINTR || err == WSAEWOULDBLOCK) {
                        write_buff_->seek_written(result);
                        break;
                    }
#endif // _WIN32

#if defined(__linux__)
                    if (err == EINTR || err == EAGAIN || err == EWOULDBLOCK) {
                        write_buff_->seek_written(result);
                        epoll_event ev { .events = EPOLLIN | EPOLLOUT | EPOLLET, .data.ptr = this };
                        epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, sock_, &ev);
                        break;
                    }
#endif // __linux__

                    _close_handle(err);
                    return -1;
                }

                write_data  += written_size;
                write_size  -= written_size;
                result      += written_size;
            }
            
            // all data written
            if (result >= target_size) {
                write_buff_->clear();
#if defined(__linux__)
            epoll_event ev { .events = EPOLLIN | EPOLLET, .data.ptr = this };
            epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, sock_, &ev);
#endif // __linux__
            }

            return result;
        }
        
        void _close_handle(int err = 0) {
            if (!is_valid()) {
                return;
            }

#ifdef _WIN32
            closesocket(sock_);
#endif

#if defined(__linux__) || defined(__APPLE__)
            close(sock_);
#endif

            sock_   = invalid_socket;
            err_    = err;
        }

        virtual bool _is_listener() { return false; }

        static bool _set_non_blocking(socket_t sock) {
#ifdef _WIN32
            u_long option = 1;
            int result = ioctlsocket(sock, FIONBIO, &option);
            return result == 0;
#endif

#ifdef __linux__ || __APPLE__
            int option = fcntl(sock, F_GETFL, 0);
            int result = fcntl(sock, F_SETFL, option | O_NONBLOCK);
            return result == 0;
#endif
        }

        void _set_remote_addr(const char* addr, int port) {
            memcpy(remote_addr_, addr, 16);
            remote_port_ = port;
        }

        static void _safe_delete(Socket* socket) {
            if (socket != nullptr) {
                delete socket->read_buff_;
                delete socket->write_buff_;
                delete socket;
            }
        }

#ifdef _WIN32
        friend void WINAPI IOCompletionCallBack(DWORD, DWORD, LPOVERLAPPED);
#endif //_WIN32

    private:
        socket_t        sock_               = invalid_socket;
        void*           user_data_          = nullptr;

        char            remote_addr_[16]    = { 0 };
        int             remote_port_        = 0;

        SimpleBuffer*   read_buff_          = nullptr;
        SimpleBuffer*   write_buff_         = nullptr;
        bool            io_completed_       = false;

        int             err_                = 0;
        bool            user_closed_        = false;
#ifdef _WIN32
        WSABUF          wsa_buf_            = {};
        WSAOVERLAPPED   wsovl_              = {};
#endif // _WIN32

#if defined(__linux__)
        int             epoll_fd_           = 0;
#endif // __linux__
    };

    class listener : public Socket {
    public:
        explicit listener(socket_t sock)
            : Socket(sock) {}

        ~listener() override = default;
    private:
        bool _is_listener() override { return true; }
    };

    static void init_socket_env() {
#ifdef _WIN32
        WSAData wsa_data;
        int result = ::WSAStartup(MAKEWORD(2, 2), &wsa_data);
        if (result != 0) {
            // error happened
        }
#endif
    }

    static void shut_socket_env() {
#ifdef _WIN32
        ::WSACleanup();
#endif // _WIN32
    }
}

#endif //SOCKET_H
