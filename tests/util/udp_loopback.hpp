#pragma once

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <cstddef>
#include <cstdint>

#include <catch2/catch_test_macros.hpp>

#include "core/wol_packet.h"

namespace corewake::test {

struct UdpLoopbackReceiver {
    int      fd   = -1;
    uint16_t port = 0;

    explicit UdpLoopbackReceiver(int timeout_ms = 250) {
        fd = ::socket(AF_INET, SOCK_DGRAM, 0);
        if (fd < 0) return;

        sockaddr_in addr{};
        addr.sin_family      = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port        = 0;

        if (::bind(fd, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) != 0) {
            ::close(fd);
            fd = -1;
            return;
        }

        socklen_t alen = sizeof(addr);
        if (::getsockname(fd, reinterpret_cast<sockaddr*>(&addr), &alen) != 0) {
            ::close(fd);
            fd = -1;
            return;
        }
        port = ntohs(addr.sin_port);

        timeval tv{};
        tv.tv_sec  = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;
        ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    }

    ~UdpLoopbackReceiver() {
        if (fd >= 0) ::close(fd);
    }

    UdpLoopbackReceiver(const UdpLoopbackReceiver&) = delete;
    UdpLoopbackReceiver& operator=(const UdpLoopbackReceiver&) = delete;

    bool valid() const { return fd >= 0; }

    int receive(uint8_t* buf, std::size_t cap) const {
        if (fd < 0) return -1;
        return static_cast<int>(::recvfrom(fd, buf, cap, 0, nullptr, nullptr));
    }
};

inline void require_magic_packet(const uint8_t buf[MAGIC_PACKET_SIZE], const uint8_t mac[MAC_ADDRESS_SIZE]) {
    for (int i = 0; i < BROADCAST_BYTES; ++i)
        REQUIRE(buf[i] == 0xFF);
    for (int i = 0; i < MAC_REPETITIONS; ++i)
        for (int j = 0; j < MAC_ADDRESS_SIZE; ++j)
            REQUIRE(buf[BROADCAST_BYTES + i * MAC_ADDRESS_SIZE + j] == mac[j]);
}

} // namespace corewake::test
