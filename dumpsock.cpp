// listens on port 9999 for incoming data, tries to read it all, and dumps it to stdout

#include <chrono>
#include <cstdio>
#include <iostream>
#include <optional>
#include <string>
#include <variant>
#include <vector>

// windows stuff
#include <fcntl.h>
#include <io.h>
#include <winsock2.h>
#include <WS2tcpip.h>

#undef min
#undef max

#pragma comment(lib, "Ws2_32.lib")

class SocketDumper {
private:
    WSADATA wsaData_;
    SOCKET socket_;
    sockaddr_in addr_;
    SOCKET incomingDataSocket_;
    std::vector<char> received_;

    std::optional<std::string> error_;

    // create an ipv4 address in the win32 format that all the socket functions expect
    sockaddr_in sockAddrForPort(uint16_t port) {
        return {
            .sin_family = AF_INET,
            .sin_port = htons(port),
            .sin_addr = {
                .S_un = {
                    .S_un_b = {
                        .s_b1 = 0,
                        .s_b2 = 0,
                        .s_b3 = 0,
                       .s_b4 = 0
                   }
                }
            }
        };
    }

    void setError(std::string msg) {
        error_ = std::move(msg);
    }

    boolean hasError() const {
        return error_.has_value();
    }
public:
    SocketDumper() {}

    void initWsa() {
        int iResult = WSAStartup(MAKEWORD(2, 2), &wsaData_);
        if (iResult != 0) {
            setError("WSAStartup failed: " + std::to_string(iResult));
        }
    }

    void initTcpSocket(uint16_t port) {
        if (hasError()) return;

        socket_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (socket_ == INVALID_SOCKET) {
            setError("Couldn't create a tcp socket");
            return;
        }
        addr_ = sockAddrForPort(port);
    }

    void bindSocket() {
        if (hasError()) return;

        int result = bind(socket_, (sockaddr*)&addr_, sizeof(sockaddr_in));
        if (result == SOCKET_ERROR) {
            setError("socket bind error");
            return;
        }
    }

    void listenSocket() {
        if (hasError()) return;

        int result = listen(socket_, /*backlog*/1);
        if (result == SOCKET_ERROR) {
            setError("socket listen error");
            return;
        }
    }

    void acceptSocket() {
        if (hasError()) return;

        incomingDataSocket_ = accept(socket_, NULL, NULL);
        if (incomingDataSocket_ == INVALID_SOCKET) {
            setError("socket accept error");
        }
    }

    void drainSocket() {
        if (hasError()) return;

        constexpr int buffSize = 4096;

        received_.reserve(1024 * 1024 * 1); // 1MiB

        char buf[buffSize];
        int result = 0;

        const auto recv_start = std::chrono::high_resolution_clock::now();

        while (result = recv(incomingDataSocket_, buf, buffSize, 0)) {
            if (result == SOCKET_ERROR) {
                setError("socket error during read");
                return;
            }

            const int readSize = result;
            received_.insert(received_.end(), buf, buf + readSize);
        }

        const auto recv_end = std::chrono::high_resolution_clock::now();

        // bytes / ms * 1000 = bytes / s
        const double Bps = static_cast<double>(received_.size()) / std::chrono::duration_cast<std::chrono::milliseconds>(recv_end - recv_start).count() * 1000.0;
        const double KiBps = Bps / 1024;
        const double MiBps = KiBps / 1024;

        const double seconds = std::chrono::duration_cast<std::chrono::milliseconds>(recv_end - recv_start).count() / 1000.0;

        std::cerr << received_.size() << " bytes in " << seconds << "s" << " for " << MiBps << " MiB/s" << std::endl;
    }

    void dump() {
        if (hasError()) {
            std::cerr << *error_ << std::endl;
        }
        else {
            std::fwrite(received_.data(), sizeof(char), received_.size(), stdout);
        }
    }

    int getExitCode() {
        return hasError() ? EXIT_FAILURE : EXIT_SUCCESS;
    }
};

void UNUSED(const auto& v) {
    v;
}

int main() {
    int v = _setmode(_fileno(stdout), O_BINARY); // write to stdout in binary mode, not character mode; otherwise windows adds an 0x0D byte for every 0x0A byte
    UNUSED(v);

    SocketDumper socketDumper{};
    socketDumper.initWsa();
    socketDumper.initTcpSocket(9999);
    socketDumper.bindSocket();
    socketDumper.listenSocket();
    socketDumper.acceptSocket();
    socketDumper.drainSocket();
    socketDumper.dump();
    return socketDumper.getExitCode();
}
