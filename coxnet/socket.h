#ifndef SOCKET_H
#define SOCKET_H

#include <tuple>
#include <functional>
#include <utility>
#include <algorithm>

#include "io_def.h"

namespace coxnet {
    class Poller;
    class simple_buffer;
    class Socket;
    using ConnectionCallback    = std::function<void (Socket* conn_socket)>;
    using CloseCallback         = std::function<void (Socket* conn_socket, int err)>;
    using DataCallback          = std::function<void (Socket* conn_socket, const char* data, size_t size)>;

    class Socket {
    public:
        friend class Poller;
        explicit Socket(socket_t sock) {
            sock_ = sock;
            if (!Socket::_is_listener()) {
                read_buff_ = new simple_buffer(max_read_buff_size);
            }
        }

        virtual ~Socket() { this->_close_handle(); }

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

            result = 0;
            const char* write_data = data;
            size_t      write_size = len;
            while (write_size > 0) {
                size_t current_try_write_size = std::min<size_t>(max_size_per_write, write_size);
                int written_size = ::send(native_handle(), write_data, current_try_write_size, 0);
                if (written_size == -1) {
                    const int err = Error::get_last_error();
                    if (err == EINTR) {
                        continue;
                    }

                    if (err == EAGAIN) {
                        continue;
                    }

                    err_ = err;
                    _close_handle();
                    return -1;
                }

                write_data  += written_size;
                write_size  -= written_size;
                result      += written_size;
            }

            return result;
        }

        // result -1: some error happened
        // result 0: write succeed, but not ensure io operation is succeed
        int write_async(const char* data, size_t len) {
            int result = -1;
            if (!is_valid() || user_closed_ || err_ != 0) {
                return result;
            }

            if (write_buff_ == nullptr) {
                write_buff_ = new simple_buffer(max_write_buff_size);
            }

            write_buff_->write(data, len);
            result = 0;
            return result;
        }
    private:
        void _close_handle() {
            if (!is_valid()) {
                return;
            }

#ifdef _WIN32
            closesocket(sock_);
#endif

#ifdef __linux__ || __APPLE__
            close(socket_);
#endif

            sock_ = invalid_socket;
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
            int result = fcntl(sock, F_SETFL, &option | O_NONBLOCK);
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

        friend void WINAPI IOCompletionCallBack(DWORD, DWORD, LPOVERLAPPED);
    private:
        struct simple_buffer {
            friend class Poller;
            std::vector<char>   buff;
            size_t              begin;
            size_t              end;

            simple_buffer(size_t initial_capacity = 8192)
                : buff(initial_capacity), begin(0), end(0) {}

            void clear() { begin = end = 0; }

            char* data() { return buff.data(); }

            // already written data size
            size_t been_written_size() const { return end - begin; }

            size_t residual_size() const { return buff.size() - end; }

            void write(const char* data, size_t len) {
                ensure_writable_bytes(len);
                memcpy(buff.data(), data, len);
                end += len;
            }

            void ensure_writable_bytes(size_t size) {
                size_t writable_bytes = buff.size() - end;
                if (writable_bytes < size) {
                    // 如果总空间不足，扩展缓冲区
                    size_t new_capacity = buff.size() * 2;
                    while (new_capacity < buff.size() + size) {
                        new_capacity *= 2;
                    }
                    buff.resize(new_capacity);
                }
            }
        };
    private:
        socket_t        sock_               = invalid_socket;
        void*           user_data_          = nullptr;

        char            remote_addr_[16]    = { 0 };
        int             remote_port_        = 0;

        simple_buffer*  read_buff_          = nullptr;
        simple_buffer*  write_buff_         = nullptr;
        bool            io_completed_       = false;

        int             err_                = 0;
        bool            user_closed_        = false;

        WSABUF          wsa_buf_            = {};
        WSAOVERLAPPED   wsovl_              = {};
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
}

#endif //SOCKET_H
