#ifndef SOCKET_H
#define SOCKET_H

#include "io_def.h"

namespace coxnet {
    class Socket {
    public:
        Socket()
            : sock_(invalid_socket) {}

        explicit Socket(socket_t sock)
            : sock_(sock) {}

        ~Socket() {
            this->close();
        }

        Socket(const Socket&) = delete;
        Socket& operator=(const Socket&) = delete;

        Socket(Socket&& other) noexcept {
            sock_           = other.sock_;
            other.sock_     = invalid_socket;
        }

        Socket& operator=(Socket&& other) noexcept {
            if (this != &other) {
                close();
                sock_           = other.sock_;
                other.sock_     = invalid_socket;
            }
            return *this;
        }

        socket_t native_handle() const { return sock_; }
        bool is_valid() const { return sock_ == invalid_socket; }

        void close() {
            if (sock_ == invalid_socket) {
                return;
            }

#ifdef _WIN32
            closesocket(sock_);
#endif

#ifdef __linux__ || __APPLE__
            close(socket_);
#endif

            socket_ = invalid_socket;
        }

        bool set_non_blocking() {
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
    private:
        socket_t sock_;
    };
}

#endif //SOCKET_H
