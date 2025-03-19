
#include "coxnet.h"

#include <iostream>
#include <map>
#include <signal.h>
#include <atomic>

std::atomic<bool> g_exit_flag(false);

void log_line(const char* content) {
    std::cout << content << std::endl;
}

void log(const char* content) {
    std::cout << content;
}
void log(coxnet::socket_t content) {
    std::cout << content;
}
void log(int content) {
    std::cout << content;
}
void log_end() {
    std::cout << std::endl;
}

class Server {
public:
    Server() {
        poller_ = new coxnet::Poller();
    }

    void start(const char address[], uint32_t port) {

        if (!poller_->listen(address, port,
            std::bind(&Server::on_connection, this, std::placeholders::_1),
            std::bind(&Server::on_data, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3),
            std::bind(&Server::on_close, this, std::placeholders::_1, std::placeholders::_2))) {

            auto err = coxnet::Error::get_last_error();
            log("listen err: ");
            log(err);
            log_end();
            return;
        }

        while (!g_exit_flag) {
            poller_->poll();
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }

    void shut() {
        if (poller_ != nullptr) {
            poller_->shut();
            delete poller_;
        }
    }

    void on_connection(coxnet::Socket* conn) {
        log("new connection: ");
        log(conn->native_handle());
        log_end();

        conns_.emplace(conn->native_handle(), conn);
    }

    void on_close(coxnet::Socket* conn, int err) {
        log("connection closed: ");
        log(err);
        log_end();

        conns_.erase(conn->native_handle());
    }

    void on_data(coxnet::Socket* conn, const char* data, size_t len) {
        std::string content(data, len);
        log("on_data: socket handle: ");
        log(conn->native_handle());
        log(", content: ");
        log(content.c_str());
        log_end();
    }

private:
    coxnet::Poller* poller_;
    std::map<coxnet::socket_t, coxnet::Socket*> conns_;
};

void signal_handler(int sign) {
    if (sign == SIGINT) {
        g_exit_flag = true;
    }
}

int main() {
    signal(SIGINT, signal_handler);

    Server server;
    server.start("0.0.0.0", 6980);

    log_line("exit...");
}