#ifndef POLLER_H
#define POLLER_H

#ifdef _WIN32

#include "io_def.h"
#include "socket.h"

#include <utility>
#include <functional>
#include <thread>
#include <chrono>

namespace coxnet {
    static void WINAPI IOCompletionCallBack(DWORD err_code, DWORD transferred_bytes, LPOVERLAPPED over_lapped) {
        Socket* socket = CONTAINING_RECORD(over_lapped, Socket, wsovl_);
        if (socket == nullptr) {
            return;
        }

        if (err_code == ERROR_SUCCESS) {
            if (transferred_bytes <= 0) {
                socket->err_ = static_cast<int>(SocketErr::kAlreadyDisconnected);
            }
        } else {
            socket->err_ = static_cast<int>(err_code);
        }

        if (socket->err_ != 0) {
            socket->_close_handle();
        }

        socket->io_completed_ = true;
        socket->read_buff_->_add_written_from_overlap(transferred_bytes);
    }

    class Poller {
    public:
        Poller() = default;
        ~Poller() = default;

        Poller(const Poller&) = delete;
        Poller& operator=(const Poller&) = delete;
        Poller(Poller&& other) = delete;
        Poller& operator=(Poller&& other) = delete;

        bool setup() {
            return true;
        }
 
        Socket* connect(const char address[], const uint32_t port, DataCallback on_data, CloseCallback on_close) {
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
                if (int err = Error::get_last_error(); err == WSAEWOULDBLOCK) {
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
                closesocket(sock_handle);
                return nullptr;
            }

            result = ::BindIoCompletionCallback(reinterpret_cast<HANDLE>(sock_handle), IOCompletionCallBack, 0);
            if (!result) {
                closesocket(sock_handle);
                return nullptr;
            }

            auto* socket = new Socket(sock_handle);
            connections_.emplace(socket->native_handle(), socket);
            if (on_data != nullptr) {
                on_data_ = std::move(on_data);
            }

            if (on_close != nullptr) {
                on_close_ = std::move(on_close);
            }

            socket->_set_remote_addr(address, port);

            // IMPORTANT: trigger first io
            socket->io_completed_ = true;
            return socket;
        }

        bool listen(const char address[], const uint32_t port, ConnectionCallback on_connection, DataCallback on_data, CloseCallback on_close) {
            IPType net_type = ip_address_version(std::string(address));
            if (net_type == IPType::kInvalid) {
                return false;
            }

            sockaddr_in local_addr = {};
            local_addr.sin_family       = AF_INET;
            local_addr.sin_port         = htons(port);
            if (inet_pton(AF_INET, address, &local_addr.sin_addr) <= 0) {
                return false;
            }

            SOCKET sock = ::socket(AF_INET , SOCK_STREAM, IPPROTO_IP);
            if (sock == invalid_socket) {
                return false;
            }

            int reuse_addr = 1;
            int result = ::setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<char*>(&reuse_addr), sizeof(reuse_addr));
            if (result == SOCKET_ERROR) {
                return false;
            }

            result = ::bind(sock, reinterpret_cast<sockaddr*>(&local_addr), sizeof(local_addr));
            if (result == SOCKET_ERROR) {
                return false;
            }

            result = ::listen(sock, 8);
            if (result == SOCKET_ERROR) {
                return false;
            }

            if (!Socket::_set_non_blocking(sock)) {
                return false;
            }

            sock_listener_  = new listener(sock);

            if (on_connection != nullptr) {
                on_connection_ = std::move(on_connection);
            }

            if (on_data != nullptr) {
                on_data_ = std::move(on_data);
            }

            if (on_close != nullptr) {
                on_close_ = std::move(on_close);
            }

            return true;
        }

        void poll() {
            if (sock_listener_ == nullptr || !sock_listener_->is_valid()) {
                return;
            }

            static bool need_clean      = false;
            static int  poll_count      = 0;
            static int  clean_interval  = 1000;

            _wait_new_connection();
            
            if (++poll_count % clean_interval == 0) {
                if (!connections_.empty()) {
                    need_clean = true;
                }
            }

            auto iter = connections_.begin();
            while (iter != connections_.end()) {
                Socket* socket = iter->second;
                _read_and_recv(socket);
                _write_async(socket);

                ++iter;
                if (!need_clean) {
                    continue;
                }

                if (socket->err_ != 0 || socket->user_closed_) {
                    if (on_close_ != nullptr && socket->io_completed_) {
                        on_close_(socket, socket->user_closed_ ? 0 : socket->err_);
                        Socket::_safe_delete(socket);
                    }

                    connections_.erase(socket->native_handle());
                }
            }

            need_clean = false;
        }
        // sync function
        void shut() {
            if (sock_listener_ != nullptr) {
                sock_listener_->_close_handle();
            }

            for (auto kv : connections_) {
                kv.second->_close_handle();
            }

            // sleep 100ms, wait io
            std::this_thread::sleep_for(std::chrono::milliseconds(100));

            // waiting for io complete
            for (auto& kv : connections_) {
                while (true) {
                    if (kv.second->io_completed_) {
                        Socket::_safe_delete(kv.second);
                        break;
                    }
                }
            }

            connections_.clear();

            delete sock_listener_;
            sock_listener_ = nullptr;

            on_connection_  = nullptr;
            on_data_        = nullptr;
            on_close_       = nullptr;
            
        }
    private:
        int _wait_new_connection() {
            socket_t        socket      = invalid_socket;
            sockaddr_in     remote_addr = {};
            int             addr_len    = sizeof(sockaddr_in);
            int             event_count = 0;

            while (sock_listener_ != nullptr && sock_listener_->is_valid()) {
                memset(&remote_addr, 0, sizeof(sockaddr_in));

                socket = ::accept(sock_listener_->native_handle(), reinterpret_cast<sockaddr*>(&remote_addr), &addr_len);
                if (socket == invalid_socket) {
                    break;
                }

                event_count++;

                if (!Socket::_set_non_blocking(socket)) {
                    continue;
                }

                if (!::BindIoCompletionCallback(reinterpret_cast<HANDLE>(socket), IOCompletionCallBack, 0)) {
                    closesocket(socket);
                    continue;
                }

                auto* conn = new Socket(socket);
                char ipv4_addr_str[16] = { 0 };
                inet_ntop(AF_INET, &remote_addr.sin_addr, ipv4_addr_str, sizeof(ipv4_addr_str));
                conn->_set_remote_addr(ipv4_addr_str, ntohs(remote_addr.sin_port));

                connections_.emplace(conn->native_handle(), conn);
                if (on_connection_ != nullptr) {
                    on_connection_(conn);
                }

                // IMPORTANT: trigger first io
                conn->io_completed_ = true;
            }

            return event_count;
        }

        void _read_and_recv(Socket* conn) {
            if (!conn->io_completed_) {
                return;
            }

            if (conn->read_buff_->written_size() > 0 && on_data_ != nullptr) {
                on_data_(conn, conn->read_buff_->data_from_head(), conn->read_buff_->written_size());
            }

            conn->io_completed_ = false;
            conn->read_buff_->clear();

            conn->wsa_buf_.buf = conn->read_buff_->data_from_tail();
            conn->wsa_buf_.len = conn->read_buff_->residual_size();
            memset(&conn->wsovl_, 0, sizeof(conn->wsovl_));

            DWORD   recv_bytes        = 0;
            DWORD   flags             = 0;
            int result =::WSARecv(conn->native_handle(), &conn->wsa_buf_, 1, &recv_bytes, &flags, &conn->wsovl_, NULL);
            if (result == SOCKET_ERROR) {
                if (int err = Error::get_last_error(); err != WSA_IO_PENDING) {
                    conn->err_ = err;
                    conn->_close_handle();
                }
            }
        }

        void _write_async(Socket* socket) {
            socket->_try_write_when_io_event_coming();
        }
    private:
        listener*                               sock_listener_  = nullptr;
        std::unordered_map<socket_t, Socket*>   connections_    = {};

        ConnectionCallback                      on_connection_  = nullptr;
        DataCallback                            on_data_        = nullptr;
        CloseCallback                           on_close_       = nullptr;
    };
}

#endif //_WIN32

#endif //POLLER_H
