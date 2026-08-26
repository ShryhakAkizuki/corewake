#include "sender_linux.hpp"

#include <arpa/inet.h>
#include <new>
#include <sys/socket.h>
#include <unistd.h>

namespace {
constexpr int kDefaultPort = 9;
}

SenderLinux::SenderLinux()
    :   vtable_{nullptr},
        id_socket_udp_(-1),
        port_(kDefaultPort),
        dest_addr_(0) {
}

SenderLinux* SenderLinux::create(const char* dest_ip, int port) {
    SenderLinux* self = new (std::nothrow) SenderLinux();

    if (self == nullptr) return nullptr;

    if (self->init(dest_ip, port) != SENDER_LINUX_OK) {
        delete self;
        return nullptr;
    }

    return self;
}

int SenderLinux::init(const char* dest_ip, int port) {
    if (port <= 0)
        port_ = kDefaultPort;
    else if (port > 65535)
        return SENDER_LINUX_ERR_INVALID_PORT;
    else
        port_ = port;

    in_addr dest{};
    if (dest_ip == nullptr || dest_ip[0] == '\0')
        dest.s_addr = htonl(INADDR_BROADCAST);
    else if (::inet_pton(AF_INET, dest_ip, &dest) != 1)
        return SENDER_LINUX_ERR_INVALID_IP;
    dest_addr_ = dest.s_addr;

    id_socket_udp_ = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (id_socket_udp_ < 0)
        return SENDER_LINUX_ERR_SOCKET;

    int opt = 1;
    if (::setsockopt(id_socket_udp_, SOL_SOCKET, SO_BROADCAST, &opt, sizeof(opt)) != 0) {
        ::close(id_socket_udp_);
        id_socket_udp_ = -1;
        return SENDER_LINUX_ERR_SETSOCKOPT;
    }

    vtable_.send = &SenderLinux::c_send;
    return SENDER_LINUX_OK;
}

SenderLinux::~SenderLinux() {
    if (id_socket_udp_ >= 0) {
        ::close(id_socket_udp_);
        id_socket_udp_ = -1;
    }
}

int SenderLinux::c_send(const uint8_t* packet, std::size_t len, sender_port_t* self) {
    if (packet == nullptr || len == 0 || self == nullptr) return -1;

    SenderLinux* sl = reinterpret_cast<SenderLinux*>(self);

    return sl->send_packet(packet, len);
}

int SenderLinux::send_packet(const uint8_t* packet, std::size_t len) {
    std::lock_guard<std::mutex> lock(mutex_);

    sockaddr_in dest{};
    dest.sin_family      = AF_INET;
    dest.sin_port        = htons(static_cast<std::uint16_t>(port_));
    dest.sin_addr.s_addr = dest_addr_;

    return (::sendto(id_socket_udp_, packet, len, 0,
                        reinterpret_cast<const sockaddr*>(&dest), sizeof(dest)) < 0) ? -1 : 0;
}

