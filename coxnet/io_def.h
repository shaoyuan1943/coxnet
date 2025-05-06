#ifndef IO_DEF_H
#define IO_DEF_H

#ifdef _WIN32
    #include <WinSock2.h>
    #include <WS2tcpip.h>
    #include <MSWSock.h>
    #include <Windows.h>
    #pragma comment(lib, "ws2_32.lib")
    #pragma comment(lib, "Mswsock.lib")
#else
    #include <sys/socket.h>
    #include <sys/types.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <netdb.h>
    #include <unistd.h>
    #include <fcntl.h>
    #include <string.h>

    #ifdef __APPLE__
        #include <sys/event.h>
    #else
        #include <sys/epoll.h>
    #endif
#endif

#include <string>
#include <regex>

namespace coxnet {
    enum class SocketErr {
        kSucceed,
        kAlreadyDisconnected,
        kTimeout
    };

#ifdef _WIN32
    using socket_t = SOCKET;
    static constexpr socket_t invalid_socket = INVALID_SOCKET;
#else
    using socket_t = int;
    using SOCKET = socket_t;
    static constexpr socket_t invalid_socket = -1;
    static constexpr int SOCKET_ERROR = -1;
#endif // _WIN32

    int get_last_error() {
#if defined(__linux__)
        return errno;
#endif // __linux__

#if defined(_WIN32)
        return (int)::WSAGetLastError();
#endif // _WIN32
        
        return 0;
    }

    enum class ErrorOption {
        kNext,
        kContinue,
        kClose
    };

    ErrorOption adjust_io_error_option(int err) {
#if defined(_WIN32)
        switch(err) {
        case WSAEWOULDBLOCK:
            return ErrorOption::kNext;
        case WSAEINTR:
            return ErrorOption::kContinue;
        default:
            return ErrorOption::kClose;
        }
#endif // _WIN32

#if defined(__linux__)
        switch(err) {
        case EAGAIN: // EAGIN == EWOULDBLOCK
            return ErrorOption::kNext;
        case EINTR:
            return ErrorOption::kContinue;
        default:
            return ErrorOption::kClose;
        }
#endif // __linux__
    }

    // socket read and write buffer size
    static constexpr size_t max_read_buff_size      = (size_t)(1024 * 4);
    static constexpr size_t max_write_buff_size     = (size_t)(1024 * 4);

    // every io operation size
    static constexpr size_t max_size_per_write      = (size_t)(1024 * 2);
    static constexpr size_t max_size_per_read       = (size_t)(1024 * 2);

    static constexpr size_t max_epoll_event_count   = 32;

    enum class IPType { kInvalid, kIPv4, kIPv6 };

    inline IPType ip_address_version(const std::string& address) {
        if (address.empty()) {
            return IPType::kInvalid;
        }

        static std::regex v4_regex(R"((\d{1,3}\.){3}\d{1,3})");
        static std::regex v6_regex(R"((([0-9a-fA-F]{0,4}:){1,7}[0-9a-fA-F]{0,4}))");

        if (std::regex_match(address, v6_regex)) {
            return IPType::kIPv6;
        } else if (std::regex_match(address, v4_regex)) {
            return IPType::kIPv4;
        }

        return IPType::kInvalid;
    }

    enum class SocketStackMode {
        kOnlyIPv4,
        kOnlyIPv6,
        kIPv4IPv6,
    };
} // namespace coxcp

#endif //IO_DEF_H