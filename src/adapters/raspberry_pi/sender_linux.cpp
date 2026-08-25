#include "sender_linux.hpp"

#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <string>
#include <sys/socket.h>
#include <unistd.h>

namespace {
constexpr int kDefaultPort = 9;  
}

SenderLinux::SenderLinux(const char* dest_ip, int port)
    :   vtable_{nullptr},
        fd_(-1),
        port_(kDefaultPort),
        dest_addr_(0) {

    if (port <= 0) 
        port_ = kDefaultPort;
    else if (port > 65535)
        throw std::invalid_argument("SenderLinux: port out of range (1..65535)");
    else 
        port_ = port;

    in_addr dest{};
    if (dest_ip == nullptr || dest_ip[0] == '\0') 
        dest.s_addr = htonl(INADDR_BROADCAST);
    else if (::inet_pton(AF_INET, dest_ip, &dest) != 1) {
        throw std::invalid_argument(
            std::string("SenderLinux: Invalid IP '") + dest_ip + "'");
    }
    dest_addr_ = dest.s_addr;

    fd_ = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (fd_ < 0) {
        throw std::runtime_error(std::string("socket(): ") + std::strerror(errno));
    }

    int opt = 1;  
    if (::setsockopt(fd_, SOL_SOCKET, SO_BROADCAST, &opt, sizeof(opt)) != 0) {
        throw std::runtime_error(std::string("setsockopt(SO_BROADCAST): ") + std::strerror(errno));
    }

    vtable_.send = &SenderLinux::c_send;
}

SenderLinux::~SenderLinux() {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}

int SenderLinux::c_send(const uint8_t* packet, std::size_t len, sender_port_t* self) {
    if (packet == nullptr || len == 0 || self == nullptr) return -1;

    SenderLinux* sl = reinterpret_cast<SenderLinux*>(self);

    try {
        return sl->send_to(packet, len);
    } catch (const std::system_error&) {
        return -1;
    }
}

int SenderLinux::send_to(const uint8_t* packet, std::size_t len) {
    std::lock_guard<std::mutex> lock(mutex_);

    sockaddr_in dest{};
    dest.sin_family      = AF_INET;
    dest.sin_port        = htons(static_cast<std::uint16_t>(port_));
    dest.sin_addr.s_addr = dest_addr_;

    return (::send_to(fd_, packet, len, 0,
                        reinterpret_cast<const sockaddr*>(&dest), sizeof(dest)) < 0) ? -1 : 0;
}