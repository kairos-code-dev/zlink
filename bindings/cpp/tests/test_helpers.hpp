#ifndef ZLINK_CPP_TEST_HELPERS_HPP
#define ZLINK_CPP_TEST_HELPERS_HPP

#include <zlink.hpp>

#include <cassert>
#include <chrono>
#include <cstdlib>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

struct transport_case_t
{
    std::string name;
    std::string inproc_base;
};

inline std::vector<transport_case_t> transport_cases()
{
    std::vector<transport_case_t> cases;
    cases.push_back(transport_case_t{"tcp", ""});
    cases.push_back(transport_case_t{"ws", ""});
    cases.push_back(transport_case_t{"inproc", "inproc://cpp-"});
    return cases;
}

inline std::string unique_inproc(const std::string &base, const std::string &suffix)
{
    static int counter = 0;
    std::ostringstream os;
    os << base << suffix << "-" << ++counter;
    return os.str();
}

inline std::string bound_endpoint(zlink::socket_t &socket)
{
    char buf[256];
    size_t size = sizeof(buf);
    const int rc = socket.get(zlink::socket_option::last_endpoint, buf, &size);
    assert(rc == 0);
    return std::string(buf, buf + size);
}

inline std::string endpoint_for(const transport_case_t &tc, const std::string &suffix)
{
    if (tc.name == "inproc") {
        return unique_inproc(tc.inproc_base, suffix);
    }
    std::ostringstream os;
    os << tc.name << "://127.0.0.1:*";
    return os.str();
}

inline int recv_with_timeout(zlink::socket_t &socket, void *buf, size_t len, int timeout_ms)
{
    const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (true) {
        const int rc = socket.recv(buf, len, zlink::recv_flag::dontwait);
        if (rc >= 0)
            return rc;
        if (zlink_errno() != EAGAIN)
            return -1;
        if (std::chrono::steady_clock::now() >= deadline)
            return -1;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

inline int recv_msg_with_timeout(zlink::socket_t &socket, zlink::message_t &msg, int timeout_ms)
{
    const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (true) {
        const int rc = socket.recv(msg, zlink::recv_flag::dontwait);
        if (rc >= 0)
            return rc;
        if (zlink_errno() != EAGAIN)
            return -1;
        if (std::chrono::steady_clock::now() >= deadline)
            return -1;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

inline bool transport_supported(const transport_case_t &tc)
{
    if (tc.name == "ws")
        return zlink::has("ws");
    return true;
}

inline void sleep_ms(int ms)
{
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

#endif
