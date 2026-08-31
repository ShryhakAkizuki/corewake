#ifndef LISTENER_LINUX_HPP
#define LISTENER_LINUX_HPP

#include <string>

#include <httplib.h>

#include "ports/listener_port.h"

class ListenerLinux {
public:

    // token: env var COREWAKE_TOKEN; port == 0 -> default port 8080
    static ListenerLinux* create(listener_receive_t callback_f, void* callback_context, int port = 8080);
    ~ListenerLinux();

    ListenerLinux(const ListenerLinux&) = delete;
    ListenerLinux& operator=(const ListenerLinux&) = delete;
    ListenerLinux(ListenerLinux&&) = delete;
    ListenerLinux& operator=(ListenerLinux&&) = delete;

    bool serve();
    void stop();

    int port() const { return port_; }

private:

    ListenerLinux(listener_receive_t callback_f, void* callback_context, const std::string& token, int port);

    void register_routes();
    void handle_wake(const httplib::Request& req, httplib::Response& res);
    bool verify_token(const httplib::Request& req) const;

    static bool const_time_equals(const std::string& presented, const std::string& expected);

    listener_receive_t callback_f_;
    void* callback_context_;
    std::string token_;
    int  port_;
    httplib::Server svr_;
};

#endif // LISTENER_LINUX_HPP
