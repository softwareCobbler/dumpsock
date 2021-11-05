// listens on port 9999 for incoming data, tries to read it all, and dumps it to stdout

#include <winsock2.h>
#include <WS2tcpip.h>
#include <cstdio>
#include <iostream>
#include <string>
#include <variant>
#include <vector>

#pragma comment(lib, "Ws2_32.lib")

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

// this is an experiment with a monad-ish (?? it's kind-of a reader monad?) error handling pattern
// "but it's just exceptions but you had to do more work" yes it's an experiment
// it's funny that this takes us back to the older C style where we have to declare all our variables up front,
// except here we declare them as members of some "environment"
// we have a "Context", which is `Env | Error`
// if Context is Error, any calls to operate on the context are no-ops, kind of like an optional<T>
// if anything, this gives a really clean exposition at the use-site of what the steps are to use a socket
namespace Monadish {
    struct Env {
        WSADATA wsaData;
        SOCKET socket;
        sockaddr_in addr;
        SOCKET incomingDataSocket;
        std::vector<char> received;
    };

    struct Error {
        Error() = delete;
        Error(const char* c) : msg(c) {}
        Error(std::string s) : msg(std::move(s)) {}
        std::string msg;
    };

    struct Nil {};

    using Context = std::variant<Env, Error>;
    using Result = std::variant<Nil, Error>; // Nil first, for the default constructability; note we don't want to accidentally construct Errors with `return {}`

    Context freshContext() {
        return Context(Env{});
    }

    template<typename ... Args>
    auto withContext(Context& ctx, auto callable, Args&& ... args) {
        return callable(ctx, std::forward<Args>(args)...);
    }

    void errorGuard(Context& ctx, auto callable) {
        if (std::holds_alternative<Error>(ctx)) {
            return;
        }
        auto result = callable(std::get<Env>(ctx));
        if (std::holds_alternative<Error>(result)) {
            ctx = std::get<Error>(result);
        }
    }

    void errorGuard(Context& ctx, auto onOk, auto onFailure) {
        if (std::holds_alternative<Error>(ctx)) {
            onFailure(std::get<Error>(ctx));
            return;
        }
        auto result = onOk(std::get<Env>(ctx));
        if (std::holds_alternative<Error>(result)) {
            ctx = std::get<Error>(result);
        }
    }

    void init(Context& ctx) {
        errorGuard(ctx, [](Env& env) -> Result {
            int iResult = WSAStartup(MAKEWORD(2, 2), &env.wsaData);
            if (iResult != 0) {
                return Error{ "WSAStartup failed: " + std::to_string(iResult) };
            }
            return {};
        });
    }

    void setupTcpSocket(Context& ctx) {
        errorGuard(ctx, [](Env& env) -> Result {
            env.socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
            if (env.socket == INVALID_SOCKET) {
                return Error{ "Couldn't create a tcp socket" };
            }
            return {};
        });
    }

    void sockAddrForPort(Context& ctx, int port) {
        errorGuard(ctx, [port](Env& env) -> Result {
            env.addr = ::sockAddrForPort(port);
            return {};
        });
    }

    void bindSocket(Context& ctx) {
        errorGuard(ctx, [](Env& env) -> Result {
            int result = bind(env.socket, (sockaddr*)&env.addr, sizeof(sockaddr_in));
            if (result == SOCKET_ERROR) {
                return Error{ "socket bind error" };
            }
            return {};
        });
    }

    void listenSocket(Context& ctx) {
        errorGuard(ctx, [](Env& env) -> Result {
            int result = listen(env.socket, /*backlog*/1);
            if (result == SOCKET_ERROR) {
                return Error{ "socket listen error" };
            }
            return {};
        });
    }

    void acceptSocket(Context& ctx) {
        errorGuard(ctx, [](Env& env) -> Result {
            env.incomingDataSocket = accept(env.socket, NULL, NULL);
            if (env.incomingDataSocket == INVALID_SOCKET) {
                return Error{ "socket accept error" };
            }
            return {};
        });
    }

    void drainSocket(Context& ctx) {
        errorGuard(ctx, [](Env& env) -> Result {
            constexpr int buffSize = 4096;

            env.received.reserve(1024 * 1024 * 1); // 1MiB

            char buf[buffSize] = { 0 };
            int result = 0;

            while (result = recv(env.incomingDataSocket, buf, buffSize, 0)) {
                if (result == SOCKET_ERROR) {
                    return Error{ "socket error during read" };
                }
                const int readSize = result;
                env.received.insert(env.received.end(), buf, buf + readSize);
                memset(buf, 0, buffSize);
            }

            return {};
        });
    }

    void dump(Context& ctx) {
        auto ok = [](Env& env) -> Result {
            std::fwrite(env.received.data(), sizeof(char), env.received.size(), stdout);
            return {};
        };
        auto fail = [](Error& err) {
            std::cout << err.msg << std::endl;
        };
        errorGuard(ctx, ok, fail);
    }

    int getExitCode(Context& ctx) {
        return std::holds_alternative<Error>(ctx) ? EXIT_FAILURE : EXIT_SUCCESS;
    }
}

// imagine a world, where the type system could validate that these were called in order
int main() {
    namespace M = Monadish;
    auto ctx = M::freshContext();
    M::withContext(ctx, M::init);
    M::withContext(ctx, M::setupTcpSocket);
    M::withContext(ctx, M::sockAddrForPort, 9999);
    M::withContext(ctx, M::bindSocket);
    M::withContext(ctx, M::listenSocket);
    M::withContext(ctx, M::acceptSocket);
    M::withContext(ctx, M::drainSocket);
    M::withContext(ctx, M::dump);
    return M::withContext(ctx, M::getExitCode);
}
