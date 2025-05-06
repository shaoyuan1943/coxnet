#ifndef POLLER_WINDOWS_H
#define POLLER_WINDOWS_H

#ifdef _WIN32

#include "io_def.h"
#include "socket.h"
#include "cleaner.h"

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
                // 104 is ECONNRESET in linux, it's mean of connection reset by peer
                err_code = 104;
            }
        }

        if (err_code != 0) {
            socket->_close_handle((int)err_code);
        }

        socket->io_completed_ = true;
        socket->read_buff_->_add_written_from_io(transferred_bytes);
    }

    class Poller : public Cleaner {
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
                if (get_last_error() == WSAEWOULDBLOCK) {
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

            auto* socket = new Socket(sock_handle, static_cast<Cleaner*>(this));
            socket->_set_remote_addr(address, port);
            conns_.emplace(socket->native_handle(), socket);

            on_data_ = std::move(on_data);
            on_close_ = std::move(on_close);

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
            on_connection_  = std::move(on_connection);
            on_data_        = std::move(on_data);
            on_close_       = std::move(on_close);

            return true;
        }

        void poll() {
            if (sock_listener_ == nullptr || !sock_listener_->is_valid()) {
                return;
            }

            _wait_new_connection();

            for (auto kv : conns_) {
                _try_read(kv.second);
                _try_write_async(kv.second);
            }
        }

        // sync function
        void shut() {
            for (auto kv : conns_) {
                kv.second->_close_handle();
            }

            if (sock_listener_ != nullptr) {
                sock_listener_->_close_handle();
            }
            // sleep 100ms, wait io
            std::this_thread::sleep_for(std::chrono::milliseconds(100));

            // waiting for io complete
            for (auto kv : conns_) {
                delete kv.second;
            }
            conns_.clear();
            Cleaner::clean_handles_.clear();

            if (sock_listener_ != nullptr) {
                delete sock_listener_;
                sock_listener_ = nullptr;
            }

            on_connection_  = nullptr;
            on_data_        = nullptr;
            on_close_       = nullptr;
            
        }
    private:
        int _wait_new_connection() {
            socket_t        handle      = invalid_socket;
            sockaddr_in     remote_addr = {};
            int             addr_len    = sizeof(sockaddr_in);
            int             event_count = 0;

            while (sock_listener_ != nullptr && sock_listener_->is_valid()) {
                memset(&remote_addr, 0, sizeof(sockaddr_in));

                handle = ::accept(sock_listener_->native_handle(), reinterpret_cast<sockaddr*>(&remote_addr), &addr_len);
                if (handle == invalid_socket) {
                    break;
                }

                event_count++;

                if (!Socket::_set_non_blocking(handle)) {
                    continue;
                }

                if (!::BindIoCompletionCallback(reinterpret_cast<HANDLE>(handle), IOCompletionCallBack, 0)) {
                    closesocket(handle);
                    continue;
                }

                auto* socket = new Socket(handle, static_cast<Cleaner*>(this));
                char ipv4_addr_str[16] = { 0 };
                inet_ntop(AF_INET, &remote_addr.sin_addr, ipv4_addr_str, sizeof(ipv4_addr_str));
                socket->_set_remote_addr(ipv4_addr_str, ntohs(remote_addr.sin_port));

                conns_.emplace(socket->native_handle(), socket);
                if (on_connection_ != nullptr) {
                    on_connection_(socket);
                }

                // IMPORTANT: trigger first io
                socket->io_completed_ = true;
            }

            return event_count;
        }

        void _try_read(Socket* socket) {
            if (!socket->io_completed_) {
                return;
            }

            if (socket->read_buff_->written_size() > 0 && on_data_ != nullptr) {
                on_data_(socket, socket->read_buff_->data(), socket->read_buff_->written_size());
            }

            socket->io_completed_ = false;
            socket->read_buff_->clear();

            socket->wsa_buf_.buf = socket->read_buff_->data();
            socket->wsa_buf_.len = socket->read_buff_->writable_size();
            memset(&socket->wsovl_, 0, sizeof(socket->wsovl_));

            DWORD   recv_bytes        = 0;
            DWORD   flags             = 0;
            int result =::WSARecv(socket->native_handle(), &socket->wsa_buf_, 1, &recv_bytes, &flags, &socket->wsovl_, NULL);
            if (result == SOCKET_ERROR) {
                if (int err = get_last_error(); err != WSA_IO_PENDING) {
                    socket->_close_handle(err);
                }
            }
        }

        void _try_write_async(Socket* socket) {
            socket->_try_write_when_io_event_coming();
        }
    private:
        listener*                               sock_listener_  = nullptr;
        std::unordered_map<socket_t, Socket*>   conns_    = {};

        ConnectionCallback                      on_connection_  = nullptr;
        DataCallback                            on_data_        = nullptr;
        CloseCallback                           on_close_       = nullptr;
        ListenErrorCallback                     on_listen_err_      = nullptr;
    };
}

#endif //_WIN32

#endif //POLLER_H
