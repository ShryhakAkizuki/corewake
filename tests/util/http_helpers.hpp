#pragma once

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <cstdint>
#include <string>
#include <thread>

#include <catch2/catch_test_macros.hpp>
#include <httplib.h>

namespace corewake::test {

inline int find_free_port(int attempts = 16) {
    for (int i = 0; i < attempts; ++i) {
        const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) return -1;

        sockaddr_in addr{};
        addr.sin_family      = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
        addr.sin_port        = 0;

        if (::bind(fd, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) != 0) {
            ::close(fd);
            continue;
        }

        socklen_t alen = sizeof(addr);
        if (::getsockname(fd, reinterpret_cast<sockaddr*>(&addr), &alen) != 0) {
            ::close(fd);
            continue;
        }

        const int port = ntohs(addr.sin_port);
        ::close(fd);
        return port;
    }
    return -1;
}

inline bool wait_until_ready(int port, int timeout_ms = 3000) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < deadline) {
        const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
        if (fd >= 0) {
            sockaddr_in addr{};
            addr.sin_family      = AF_INET;
            addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            addr.sin_port        = htons(static_cast<uint16_t>(port));

            if (::connect(fd, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) == 0) {
                ::close(fd);
                return true;
            }
            ::close(fd);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
    return false;
}

inline httplib::Client make_client(int port) {
    httplib::Client cli("127.0.0.1", port);
    cli.set_connection_timeout(2, 0);
    cli.set_read_timeout(2, 0);
    return cli;
}

inline httplib::Response post_wake(httplib::Client& cli, const std::string& alias, const std::string& auth_header) {
    httplib::Request req;
    req.method = "POST";
    req.path   = "/wake/" + alias;
    if (!auth_header.empty()) req.headers.emplace("Authorization", auth_header);

    httplib::Response res;
    httplib::Error err = httplib::Error::Success;
    const bool sent = cli.send(req, res, err);
    if (!sent) INFO("client error code: " << static_cast<int>(err));
    return res;
}

inline httplib::Response get_path(httplib::Client& cli, const std::string& path) {
    httplib::Request req;
    req.method = "GET";
    req.path   = path;

    httplib::Response res;
    httplib::Error err = httplib::Error::Success;
    const bool sent = cli.send(req, res, err);
    if (!sent) INFO("client error code: " << static_cast<int>(err));
    return res;
}

} // namespace corewake::test
