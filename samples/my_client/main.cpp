#include <iostream>
#include <csignal>

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

int main() {
    signal(SIGINT, signal_handler);

    coxnet::init_socket_env();

    auto on_data = [](coxnet::Socket* conn_socket, const char* data, size_t size) {
        log("on_data: ", conn_socket, std::string(data));
    };

    auto on_close = [](coxnet::Socket* conn_socket, int err) {
        log("on_close: ", conn_socket, err);
    };
    log("client start...");

    coxnet::Poller poller;
    auto client_socket = poller.connect("10.11.152.134", 6890, on_data, on_close);
    if (!client_socket) {
        log("connect failed.");
        return 0;
    }

    log("connect success, conn socket", client_socket);
    const char* hello = "hello world";
    int result = client_socket->write(hello, strlen(hello));
    log("write result", result);

    while (!g_exit_flag) {
        poller.poll();

        std::this_thread::sleep_for((std::chrono::milliseconds)10);
    }

    log("client exit.");
    poller.shut();
    return 1;
}