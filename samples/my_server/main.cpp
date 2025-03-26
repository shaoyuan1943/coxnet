#include <iostream>
#include <signal.h>
#include <atomic>
#include <thread>
#include <chrono>

#include "coxnet/coxnet.h"

std::atomic<bool> g_exit_flag(false);

// 递归终止
template<typename T>
void log_impl(std::ostream& os, T&& t) {
    os << std::forward<T>(t);
}

// 递归展开
template<typename T, typename... Args>
void log_impl(std::ostream& os, T&& t, Args&&... args) {
    os << std::forward<T>(t) << " ";
    log_impl(os, std::forward<Args>(args)...);
}

// 对外接口
template<typename... Args>
void log(Args&&... args) {
    log_impl(std::cout, std::forward<Args>(args)...);
    std::cout << std::endl;
}

void signal_handler(int sign) {
    if (sign == SIGINT) {
        g_exit_flag = true;
    }
}

class Server {
public:
    bool start() {
        auto on_new_connection = [this](coxnet::Socket* conn_socket) {
            log("on_new_connection", conn_socket, std::get<0>(conn_socket->remote_addr()));

        };

        auto on_close = [this](coxnet::Socket* conn_socket, int err) {
            log("on_close: err: ", err);
        };

        auto on_data = [this](coxnet::Socket* conn_socket, const char* data, size_t size) {
            log("on_data: ", (int)conn_socket->native_handle(), std::string(data));
            const char* welcome = "welcome";
            conn_socket->write(welcome, strlen(welcome));
        };

        return poller_.listen("0.0.0.0", 6890, on_new_connection, on_data, on_close);
    }

    coxnet::Poller poller_;
};

int main() {
    signal(SIGINT, signal_handler);
    log("server start...");

    coxnet::init_socket_env();

    Server server;
    if (!server.start()) {
        log("poller listen failed");
        return 0;
    }

    while (!g_exit_flag) {
        server.poller_.poll();

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    log("server exit.");
    server.poller_.shut();
}