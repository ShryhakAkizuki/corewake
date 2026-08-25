#ifndef SENDER_LINUX_HPP
#define SENDER_LINUX_HPP

#include <cstddef>
#include <cstdint>
#include <mutex>

#include "ports/sender_port.h"

// dest_ip == nullptr -> INADDR_BROADCAST
// port <= 0          -> 9 
class SenderLinux {
public:

    SenderLinux(const char* dest_ip = nullptr, int port = 0);
    ~SenderLinux();

    SenderLinux(const SenderLinux&) = delete;
    SenderLinux& operator=(const SenderLinux&) = delete;
    SenderLinux(SenderLinux&&) = delete;
    SenderLinux& operator=(SenderLinux&&) = delete;

    sender_port_t* vtable() { return &vtable_; }

private:

    static int c_send(const uint8_t* packet, std::size_t len, sender_port_t* self);

    int send_to(const uint8_t* packet, std::size_t len);

    sender_port_t vtable_;

    int fd_;
    int port_;
    std::uint32_t dest_addr_;
    std::mutex mutex_;
};

#endif // SENDER_LINUX_HPP
