#ifndef POLLER_H
#define POLLER_H

#include "io_def.h"
#include "socket.h"

#include <utility>
#include <functional>
#include <thread>
#include <chrono>

class ListenNode;
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
            socket->close_handle();
        }

        socket->io_completed_ = true;
        socket->buff_->end += transferred_bytes;
    }

    class Poller {
    public:
        Poller() = default;

        ~Poller() {
            delete sock_listener_;
        }

        Poller(const Poller&) = delete;
        Poller& operator=(const Poller&) = delete;
        Poller(Poller&& other) = delete;
        Poller& operator=(Poller&& other) = delete;

        bool listen(const char address[], uint32_t port, ConnectionCallback on_connection, DataCallback on_data, CloseCallback on_close) {
            SOCKET sock = ::socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
            if (sock == invalid_socket) {
                return false;
            }

            if (address == nullptr || address[0] == '\0') {
                return false;
            }

            in_addr ipv4_addr {};
            if (inet_pton(AF_INET, address, &ipv4_addr) == 0) {
                return false;
            }

            int reuse_addr = 1;
            int result = ::setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<char*>(&reuse_addr), sizeof(reuse_addr));
            if (result == SOCKET_ERROR) {
                return false;
            }

            sockaddr_in local_addr = {};
            local_addr.sin_family       = AF_INET;
            local_addr.sin_addr.s_addr  = ipv4_addr.S_un.S_addr;
            local_addr.sin_port         = htons(port);

            result = ::bind(sock, reinterpret_cast<sockaddr*>(&local_addr), sizeof(local_addr));
            if (result == SOCKET_ERROR) {
                return false;
            }

            result = ::listen(sock, 8);
            if (result == SOCKET_ERROR) {
                return false;
            }

            if (!Socket::set_non_blocking(sock)) {
                return false;
            }

            sock_listener_  = new listener(sock);
            on_connection_  = std::move(on_connection);
            on_data_        = std::move(on_data);
            on_close_       = std::move(on_close);

            return true;
        }

        void poll() {
            static bool need_clean      = false;
            static int  poll_count      = 0;
            static int  clean_interval  = 1000;

            if (try_wait_connections() <= 0) {
                return;
            }
            
            if (++poll_count % clean_interval == 0) {
                if (!connections_.empty()) {
                    need_clean = true;
                }
            }

            auto iter = connections_.begin();
            while (iter != connections_.end()) {
                Socket* socket = iter->second;
                try_read_and_process_written(socket);
                try_write_async(socket);

                ++iter;
                if (!need_clean) {
                    continue;
                }

                if (socket->err_ != 0 || socket->user_closed_) {
                    if (on_close_ != nullptr && socket->io_completed_) {
                        on_close_(socket, socket->user_closed_ ? 0 : socket->err_);

                        Socket::safe_delete(socket);
                    }

                    connections_.erase(socket->native_handle());
                }
            }

            need_clean = false;
        }
        // sync function
        void shut() {
            if (sock_listener_ != nullptr) {
                sock_listener_->close_handle();
            }

            for (auto kv : connections_) {
                kv.second->close_handle();
            }

            // sleep 100ms, wait io
            std::this_thread::sleep_for(std::chrono::milliseconds(100));

            // waiting for io complete
            for (auto& kv : connections_) {
                while (true) {
                    if (kv.second->io_completed_) {
                        Socket::safe_delete(kv.second);
                        break;
                    }
                }
            }

            WSACleanup();
            connections_.clear();
        }
    private:
        int try_wait_connections() {
            SOCKET      socket      = invalid_socket;
            sockaddr_in remote_addr = {};
            int         addr_len    = sizeof(sockaddr_in);
            int         event_count = 0;

            while (sock_listener_->is_valid()) {
                memset(&remote_addr, 0, sizeof(sockaddr_in));

                socket = accept(sock_listener_->native_handle(), reinterpret_cast<sockaddr*>(&remote_addr), &addr_len);
                if (socket == invalid_socket) {
                    break;
                }

                event_count++;

                if (!Socket::set_non_blocking(socket)) {
                    continue;
                }

                auto* conn_socket = new Socket(socket);
                char ipv4_addr_str[16] = { 0 };
                inet_ntop(AF_INET, &remote_addr.sin_addr, ipv4_addr_str, sizeof(ipv4_addr_str));
                conn_socket->set_remote_addr(ipv4_addr_str, ntohs(remote_addr.sin_port));
                if (!BindIoCompletionCallback(
                        reinterpret_cast<HANDLE>(conn_socket->native_handle()), IOCompletionCallBack, 0)) {
                    conn_socket->close_handle();
                    Socket::safe_delete(conn_socket);
                    continue;
                }

                connections_.emplace(conn_socket->native_handle(), conn_socket);
                if (on_connection_ != nullptr) {
                    on_connection_(conn_socket);
                }
            }

            return event_count;
        }
        void try_read_and_process_written(Socket* socket) {
            if (!socket->io_completed_) {
                return;
            }

            if (socket->buff_->been_written_size()) {
                if (on_data_ != nullptr) {
                    on_data_(socket, socket->buff_->data(), socket->buff_->been_written_size());
                }
            }

            socket->io_completed_ = false;
            socket->buff_->clear();

            socket->wsa_buf_.buf = socket->buff_->data();
            socket->wsa_buf_.len = socket->buff_->residual_size();
            memset(&socket->wsovl_, 0, sizeof(socket->wsovl_));

            DWORD   recv_bytes        = 0;
            DWORD   flags             = 0;
            int result = WSARecv(socket->native_handle(), &socket->wsa_buf_, 1, &recv_bytes, &flags, &socket->wsovl_, NULL);
            if (result == SOCKET_ERROR) {
                int err = Error::get_last_error();
                if (err != WSA_IO_PENDING) {
                    socket->err_ = err;
                    socket->close_handle();
                }
            }
        }
        void try_write_async(Socket* socket) {
            if (socket->write_buff_ == nullptr || socket->write_buff_->been_written_size() <= 0) {
                return;
            }

            const char* data = socket->write_buff_->data();
            size_t len = socket->write_buff_->been_written_size();
            int written_size = socket->write(data, len);
            if (written_size <= -1) {   // some error happened
                return;
            }

            if (written_size == len) {
                socket->write_buff_->clear();
            } else {
                socket->write_buff_->end -= written_size;
            }
        }
    private:
        listener*                               sock_listener_  = nullptr;
        std::unordered_map<socket_t, Socket*>   connections_    = {};

        ConnectionCallback                      on_connection_  = nullptr;
        DataCallback                            on_data_        = nullptr;
        CloseCallback                           on_close_       = nullptr;
    };
}
#endif //POLLER_H
