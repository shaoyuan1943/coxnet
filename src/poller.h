#ifndef POLLER_H
#define POLLER_H

#include "io_def.h"
#include "socket.h"

#include <vector>
#include <functional>

class ListenNode;
namespace coxnet {
    static void WINAPI IOCompletionCallBack(DWORD dwErrorCode, DWORD dwTransferedBytes, LPOVERLAPPED lpOverlapped)
    {

    }

    class Poller {
    public:
        Poller(uint32_t max_listen = 1) {
            if (max_listen > 0) {
                listen_nodes_.reserve(max_listen);
            }
        }

        ~Poller() {
            for (auto i = 0; i < listen_nodes_.size(); i++) {
                listen_nodes_[i]->close_handle();
                delete listen_nodes_[i];
            }
        }

        Poller(const Poller&) = delete;
        Poller& operator=(const Poller&) = delete;
        Poller(Poller&& other) = delete;
        Poller& operator=(Poller&& other) = delete;

        bool listen(const char address[], uint32_t port) {

            SOCKET sock = ::socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
            if (sock == invalid_socket) {
                return false;
            }

            unsigned long addr = INADDR_ANY;
            if (address[0] != '\0') {
                addr = inet_addr(address);
                if (addr == INADDR_NONE) {
                    addr = INADDR_ANY;
                }
            }

            int one = 1;
            int result = ::setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<char*>(&one), sizeof(one));
            if (result == SOCKET_ERROR) {
                return false;
            }

            // TODO: set recv buffer size ?

            sockaddr_in local_addr = {};
            local_addr.sin_family       = AF_INET;
            local_addr.sin_addr.s_addr  = addr;
            local_addr.sin_port         = htons(port);

            result = ::bind(sock, reinterpret_cast<sockaddr*>(&local_addr), sizeof(local_addr));
            if (result == SOCKET_ERROR) {
                return false;
            }

            result = ::listen(sock, 8);
            if (result == SOCKET_ERROR) {
                return false;
            }

            coxnet::listener* listen_socket = new listener(sock);
            if (!listen_socket->set_non_blocking()) {
                listen_socket->close_handle();
                delete listen_socket;
                return false;
            }

            listen_nodes_.push_back(listen_socket);
            return true;
        }

        int poll() {
            int io_count = 0;
            for (auto i = 0; i < listen_nodes_.size(); i++) {
                if (wait_connections(listen_nodes_[i]) > 0) {
                    io_count++;
                }
            }

            return io_count;
        }

        int wait_connections(const listener* listen_socket) {
            SOCKET      socket      = invalid_socket;
            sockaddr_in remote_addr;
            int         addr_len    = sizeof(sockaddr_in);
            int         event_count = 0;

            while (!listen_socket->is_closed()) {
                memset(&remote_addr, 0, sizeof(sockaddr_in));

                socket = accept(listen_socket->native_handle(), (sockaddr*)(&remote_addr), &addr_len);
                if (socket == invalid_socket) {
                    break;
                }

                event_count++;

                Socket* conn_socket = new Socket(socket);
                if (!conn_socket->set_non_blocking()) {
                    conn_socket->close_handle();
                    delete conn_socket;
                    continue;
                }

                conn_socket->set_remote_addr(inet_addr(remote_addr.sin_addr), ntohs(remote_addr.sin_port));
                if (!BindIoCompletionCallback((HANDLE)(conn_socket->native_handle()), IOCompletionCallBack, 0)) {
                    conn_socket->close_handle();
                    delete conn_socket;
                    continue;
                }

                listen_socket->on_connection(conn_socket);
            }

            return event_count;
        }
    private:
        std::vector<coxnet::listener*> listen_nodes_;
    };
}
#endif //POLLER_H
