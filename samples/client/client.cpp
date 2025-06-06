#include "coxnet/coxnet.h"
#include <iostream>
#include <thread>

int main() {
    coxnet::initialize_socket_env();

    coxnet::Poller poller;
    auto* client = poller.connect("127.0.0.1", 8080,
        [](coxnet::Socket* conn, const char* data, size_t len) {
            std::cout << "[Client] Received: " << std::string(data, len) << std::endl;
        },
        [](coxnet::Socket* conn, int err) {
            std::cout << "[Client] Connection closed: " << err << std::endl;
        });
    
    if (!client) {
        std::cerr << "Connection failed" << std::endl;
        return 1;
    }
    
    // 测试消息发送
    const char* test_msg = "Hello server!";
    client->write(test_msg, strlen(test_msg));
    
    // 事件循环
    while(true) {
        poller.poll();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    coxnet::cleanup_socket_env();
    return 0;
}