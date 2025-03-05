#ifndef SOCKET_H
#define SOCKET_H

#include <tuple>
#include <functional>

#include "io_def.h"

namespace coxnet {

    class Socket;
    using ConnectionCallback    = std::function<void (Socket* conn_socket)>;
    using ErrorCallback         = std::function<void (Socket* conn_socket, int err_code)>;

    class Socket {
    public:
        Socket()
            : sock_(invalid_socket) {}

        explicit Socket(socket_t sock) : sock_(sock) {}

        virtual ~Socket() {
            this->close_handle();
        }

        Socket(const Socket&) = delete;
        Socket& operator=(const Socket&) = delete;

        Socket(Socket&& other) noexcept {
            sock_           = other.sock_;
            other.sock_     = invalid_socket;
        }

        Socket& operator=(Socket&& other) noexcept {
            if (this != &other) {
                close_handle();
                sock_           = other.sock_;
                other.sock_     = invalid_socket;
            }
            return *this;
        }

        socket_t native_handle() const { return sock_; }
        bool is_valid() const { return (sock_ == invalid_socket) || delay_close_; }

        bool is_closed() const { return delay_close_ ? true : (sock_ == invalid_socket ? true : false); }
        void close_handle(const bool is_delay = false) {
            if (is_delay) {
                delay_close_ = true;
            } else {
                if (sock_ == invalid_socket) {
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
        }

        bool set_non_blocking() const {
#ifdef _WIN32
            u_long option = 0;
            int result = ioctlsocket(sock_, FIONBIO, &option);
            return result == 0;
#endif

#ifdef __linux__ || __APPLE__
            int option = fcntl(sock_, F_GETFL, 0);
            int result = fcntl(sock_, F_SETFL, &option | O_NONBLOCK);
            return result == 0;
#endif
        }

        virtual bool is_listener() { return false; }

        void* user_data() { return user_data_; }
        void set_user_data(void* usr_data) {
            user_data_ = usr_data;
        }

        void set_remote_addr(const char* addr, int port) {
            memcpy(remote_addr, addr, 64);
            remote_port = port;
        }

        std::tuple<char*, int> remote_addr() {
            return std::make_tuple(remote_addr, remote_port);
        }
    private:
        socket_t    sock_;
        void*       user_data_          = nullptr;
        bool        delay_close_        = false;
        char        remote_addr[64]     = { 0 };
        int         remote_port         = 0;
    };

    class listener : public Socket {
    public:
        listener(socket_t sock)
            : Socket(sock) {}
        ~listener() override {}

        void set_connection_callback(ConnectionCallback callback) {
            on_connection_ = callback;
        }

        void on_connection(Socket* socket) const {
            if (on_connection_ != nullptr) {
                on_connection_(socket);
            }
        }

        bool is_listener() override { return true; }
    private:
        ConnectionCallback on_connection_ = nullptr;
    };
}

#endif //SOCKET_H
