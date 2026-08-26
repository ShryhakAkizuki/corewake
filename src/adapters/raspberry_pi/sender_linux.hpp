#ifndef SENDER_LINUX_HPP
#define SENDER_LINUX_HPP

#include <cstddef>
#include <cstdint>
#include <mutex>

#include "ports/sender_port.h"

// Error codes
typedef enum sender_linux_err {
    SENDER_LINUX_OK               =  0,  // success
    SENDER_LINUX_ERR_INVALID_PORT = -1,  // port out of range (1..65535)
    SENDER_LINUX_ERR_INVALID_IP   = -2,  // invalid destination IP
    SENDER_LINUX_ERR_SOCKET       = -3,  // socket() failed
    SENDER_LINUX_ERR_SETSOCKOPT   = -4   // setsockopt(SO_BROADCAST) failed
} sender_linux_err_t;

// dest_ip == nullptr -> INADDR_BROADCAST
// port <= 0          -> 9
class SenderLinux {
public:

    static SenderLinux* create(const char* dest_ip = nullptr, int port = 0);
    ~SenderLinux();

    SenderLinux(const SenderLinux&) = delete;
    SenderLinux& operator=(const SenderLinux&) = delete;
    SenderLinux(SenderLinux&&) = delete;
    SenderLinux& operator=(SenderLinux&&) = delete;

    sender_port_t* vtable() { return &vtable_; }

private:

    SenderLinux();

    int init(const char* dest_ip, int port);

    static int c_send(const uint8_t* packet, std::size_t len, sender_port_t* self);

    int send_packet(const uint8_t* packet, std::size_t len);

    sender_port_t vtable_;

    int id_socket_udp_;
    int port_;
    std::uint32_t dest_addr_;
    std::mutex mutex_;
};

#endif // SENDER_LINUX_HPP
