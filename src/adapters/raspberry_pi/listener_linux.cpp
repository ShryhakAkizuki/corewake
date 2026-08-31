#include "listener_linux.hpp"

#include <cstddef>
#include <cstdlib>
#include <new>

namespace {

constexpr int         kDefaultPort     = 8080;
constexpr const char* kTokenEnvVar     = "COREWAKE_TOKEN";
constexpr const char* kBearerPrefix    = "Bearer ";
constexpr std::size_t kBearerPrefixLen = 7;

} // namespace

ListenerLinux::ListenerLinux(listener_receive_t callback_f, void* callback_context, const std::string& token, int port)
    :   callback_f_(callback_f),
        callback_context_(callback_context),
        token_(token),
        port_(port == 0 ? kDefaultPort : port),
        svr_() {
    register_routes();
}

ListenerLinux* ListenerLinux::create(listener_receive_t callback_f, void* callback_context, int port) {
    if (callback_f == nullptr || callback_context == nullptr) return nullptr;

    const char* token = std::getenv(kTokenEnvVar);
    if (token == nullptr || token[0] == '\0') return nullptr;

    ListenerLinux* self = new (std::nothrow) ListenerLinux(callback_f, callback_context, token, port);
    if (self == nullptr) return nullptr;

    return self;
}

ListenerLinux::~ListenerLinux() {
    stop();
}

bool ListenerLinux::serve() {
    return svr_.listen("0.0.0.0", port_);
}

void ListenerLinux::stop() {
    svr_.stop();
}

void ListenerLinux::register_routes() {
    svr_.Post(R"(/wake/([A-Za-z0-9_]{1,31}))", [this](const httplib::Request& req, httplib::Response& res) {
        handle_wake(req, res);
    });
}

void ListenerLinux::handle_wake(const httplib::Request& req, httplib::Response& res) {
    if (!verify_token(req)) {
        res.status = 401;
        res.set_content("unauthorized", "text/plain; charset=utf-8");
        return;
    }

    const std::string& alias = req.matches[1].str();

    const int rc = callback_f_(alias.c_str(), callback_context_);

    if (rc == 0) {
        res.status = 200;
        res.set_content("wake ok", "text/plain; charset=utf-8");
    } else if (rc == -3) {
        res.status = 404;
        res.set_content("alias not found", "text/plain; charset=utf-8");
    } else {
        res.status = 500;
        res.set_content("internal error", "text/plain; charset=utf-8");
    }
}

bool ListenerLinux::verify_token(const httplib::Request& req) const {
    const std::string header = req.get_header_value("Authorization");
    if (header.size() <= kBearerPrefixLen) return false;
    if (header.compare(0, kBearerPrefixLen, kBearerPrefix) != 0) return false;

    return const_time_equals(header.substr(kBearerPrefixLen), token_);
}

bool ListenerLinux::const_time_equals(const std::string& presented, const std::string& expected) {
    if (presented.size() != expected.size()) return false;

    unsigned char diff = 0;
    for (std::size_t i = 0; i < presented.size(); ++i)
        diff = static_cast<unsigned char>(diff |
                    (static_cast<unsigned char>(presented[i]) ^
                    static_cast<unsigned char>(expected[i])));

    return diff == 0;
}
