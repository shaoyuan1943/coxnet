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
    static constexpr socket_t invalid_socket = -1;
    static constexpr int SOCKET_ERROR = -1;
#endif // _WIN32

    class Error {
    public:
        static std::string get_last_error_string() {
#ifdef _WIN32
            const DWORD err = WSAGetLastError();
            char* buffer = nullptr;

            FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                NULL, err, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPSTR)&buffer, 0, NULL);

            std::string message(buffer);
            LocalFree(buffer);
            return message;
#else
            return std::string(strerror(errno));
#endif
        }

        static int get_last_error() {
#ifdef _WIN32
            return WSAGetLastError();
#else
            return errno;
#endif
        }
    };

    struct IOResult {
        std::size_t bytes_transferred   = 0;
        int         error_code          = 0;
        void*       context             = nullptr;
    };

    // socket read and write buffer size
    static constexpr size_t max_read_buff_size  = (size_t)(1024 * 4);
    static constexpr size_t max_write_buff_size = (size_t)(1024 * 4);

    // every io operation size
    static constexpr size_t max_size_per_write  = (size_t)(1024 * 2);
    static constexpr size_t max_size_per_read   = (size_t)(1024 * 2);

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