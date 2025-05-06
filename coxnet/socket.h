#ifndef SOCKET_H
#define SOCKET_H

#include "io_def.h"
#include "cleaner.h"

#include <tuple>
#include <functional>
#include <utility>
#include <algorithm>

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
        explicit Socket(socket_t sock, Cleaner* cleaner = nullptr) {
            sock_ = sock;
            if (!Socket::_is_listener()) {
                read_buff_  = new SimpleBuffer(max_read_buff_size);
                write_buff_ = new SimpleBuffer(max_write_buff_size);
            }

            cleaner_ = cleaner;
        }
        
        virtual ~Socket() {
            if (!Socket::_is_listener()) {
                if (read_buff_ != nullptr) {
                    delete read_buff_;
                    read_buff_ = nullptr;
                }
                
                if (write_buff_ != nullptr) {
                    delete write_buff_;
                    write_buff_ = nullptr;
                }
            }
        }

        Socket(const Socket&) = delete;
        Socket& operator=(const Socket&) = delete;
        Socket(Socket&& other) = delete;
        Socket& operator=(Socket&& other) = delete;

        socket_t native_handle() const { return sock_; }
        bool is_valid() const { return sock_ != invalid_socket && err_ == 0 && !user_closed_; }

        void user_close() {
            user_closed_ = true;
            _close_handle(0);
        }

        std::tuple<char*, int> remote_addr() { return std::make_tuple(remote_addr_, remote_port_); }

        int write(const char* data, size_t len) {
            int result = -1;
            if (!is_valid() || user_closed_ || err_ != 0) {
                return result;
            }

            if (write_buff_->written_size_from_seeker() > 0) {
                write_buff_->write(data, len);
                return (int)len;
            }

            size_t total_sent   = 0;
            size_t data_len     = len;

            while (total_sent < data_len) {
                int sent_n = ::send(native_handle(), data + total_sent, data_len - total_sent, 0);
                if (sent_n > 0) {
                    total_sent += sent_n;
                } else {
                    int err_code = get_last_error();
                    ErrorOption state = adjust_io_error_option(err_code);
                    if (state == ErrorOption::kNext) {
                        write_buff_->write(data + total_sent, data_len - total_sent);
#if defined(__linux__)
                        epoll_event ev { .events = EPOLLIN | EPOLLOUT | EPOLLET, .data.ptr = this };
                        epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, sock_, &ev);
#endif // __linux__
                        break;
                    } else if (state == ErrorOption::kContinue) {
                        continue;
                    } else {
                        _close_handle(err_code);
                        return -1;
                    }
                }
            }

            return total_sent;
        }
    private:
        size_t _try_write_when_io_event_coming() {
            if (write_buff_->written_size_from_seeker() <= 0) {
                return 0;
            }
            
            size_t total_sent   = 0;
            size_t data_len     = write_buff_->written_size_from_seeker();
            while (total_sent < data_len) {
                int sent_n = ::send(native_handle(), write_buff_->data_from_last_seek(), write_buff_->written_size_from_seeker(), 0);
                if (sent_n > 0) {
                    total_sent += sent_n;
                    write_buff_->seek(sent_n);
                } else {
                    int err_code = get_last_error();
                    ErrorOption state = adjust_io_error_option(err_code);
                    if (state == ErrorOption::kNext) {
#if defined(__linux__)
                        epoll_event ev { .events = EPOLLIN | EPOLLOUT | EPOLLET, .data.ptr = this };
                        epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, sock_, &ev);
#endif // __linux__
                        break;
                    } else if (state == ErrorOption::kContinue) {
                        continue;
                    } else {
                        _close_handle(err_code);
                        return -1;
                    }
                }
            }

            if (total_sent >= data_len) {
                write_buff_->clear();
#if defined(__linux__)
                epoll_event ev { .events = EPOLLIN | EPOLLET, .data.ptr = this };
                epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, sock_, &ev);
#endif // __linux__
            }

            return total_sent;
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

            if (cleaner_ != nullptr) {
                cleaner_->push_handle(sock_);
            }
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
        Cleaner*        cleaner_            = nullptr;
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
