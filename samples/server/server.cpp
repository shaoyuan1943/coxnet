#include "coxnet/coxnet.h"
#include <iostream>

int main() {
    coxnet::Poller poller;
    
    bool success = poller.listen("::", 8080, coxnet::ProtocolStack::kDualStack,
        [](coxnet::Socket* conn) {
            std::cout << "[Server] New connection from: " 
                     << conn->remote_addr().first << std::endl;
        },
        [](coxnet::Socket* conn, const char* data, size_t len) {
            std::cout << "[Server] Received " << len << " bytes" << std::endl;
            conn->write(data, len); // Echo back
        },
        [](coxnet::Socket* conn, int err) {
            std::cout << "[Server] Connection closed: " << err << std::endl;
        });
    
    if (!success) {
        std::cerr << "Server startup failed" << std::endl;
        return 1;
    }
    
    std::cout << "Server running on port 8080..." << std::endl;
    
    // 事件循环
    while(true) {
        poller.poll();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    coxnet::cleanup_socket_env();
    return 0;
}