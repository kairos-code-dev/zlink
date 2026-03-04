#ifndef ZLINK_CPP_TEST_HELPERS_HPP
#define ZLINK_CPP_TEST_HELPERS_HPP

#include <zlink.hpp>

#include <cassert>
#include <chrono>
#include <cerrno>
#include <cstring>
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
    std::string endpoint;
    const int rc = socket.get(zlink::socket_option::last_endpoint, endpoint);
    assert(rc == 0);
    assert(!endpoint.empty());
    return endpoint;
}

inline std::string endpoint_for(const transport_case_t &tc, const std::string &suffix)
{
    if (tc.name == "inproc") {
        return unique_inproc(tc.inproc_base, suffix);
    }
    // Keep generated test ports outside Linux's default ephemeral range.
    static int port_seed = 15500;
    const int port = ++port_seed;
    std::ostringstream os;
    os << tc.name << "://127.0.0.1:" << port;
    return os.str();
}

inline int recv_with_timeout(zlink::socket_t &socket, void *buf, size_t len, int timeout_ms)
{
    int old_timeout = -1;
    const int have_old_timeout =
      socket.get (zlink::socket_option::rcvtimeo, &old_timeout);
    assert (socket.set (zlink::socket_option::rcvtimeo, timeout_ms) == 0);
    const int rc = socket.recv (buf, len);
    if (have_old_timeout == 0)
        assert (socket.set (zlink::socket_option::rcvtimeo, old_timeout) == 0);
    return rc;
}

inline int recv_msg_with_timeout(zlink::socket_t &socket, zlink::message_t &msg, int timeout_ms)
{
    int old_timeout = -1;
    const int have_old_timeout =
      socket.get (zlink::socket_option::rcvtimeo, &old_timeout);
    assert (socket.set (zlink::socket_option::rcvtimeo, timeout_ms) == 0);
    const int rc = socket.recv (msg);
    if (have_old_timeout == 0)
        assert (socket.set (zlink::socket_option::rcvtimeo, old_timeout) == 0);
    return rc;
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

inline void send_string_expect_success(zlink::socket_t &socket,
                                       const char *data,
                                       zlink::send_flag flags = zlink::send_flag::none)
{
    assert(data);
    const size_t len = std::strlen(data);
    assert(socket.send(data, len, flags) == static_cast<int>(len));
}

inline void recv_string_expect_success(zlink::socket_t &socket,
                                       const char *expected,
                                       zlink::recv_flag flags = zlink::recv_flag::none)
{
    assert(expected);
    char buffer[256];
    const int rc = socket.recv(buffer, sizeof(buffer), flags);
    assert(rc >= 0);
    const size_t len = std::strlen(expected);
    assert(rc == static_cast<int>(len));
    assert(std::memcmp(buffer, expected, len) == 0);
}

inline int get_rcvmore(zlink::socket_t &socket)
{
    int more = 0;
    assert(socket.get(zlink::socket_option::rcvmore, &more) == 0);
    return more;
}

inline void bounce(zlink::socket_t &server, zlink::socket_t &client)
{
    static const char *kBounce = "12345678ABCDEFGH12345678abcdefgh";
    const size_t bounce_len = std::strlen(kBounce);

    assert(client.send(kBounce, bounce_len, zlink::send_flag::sndmore)
           == static_cast<int>(bounce_len));
    assert(client.send(kBounce, bounce_len) == static_cast<int>(bounce_len));

    recv_string_expect_success(server, kBounce);
    assert(get_rcvmore(server) == 1);
    recv_string_expect_success(server, kBounce);
    assert(get_rcvmore(server) == 0);

    assert(server.send(kBounce, bounce_len, zlink::send_flag::sndmore)
           == static_cast<int>(bounce_len));
    assert(server.send(kBounce, bounce_len) == static_cast<int>(bounce_len));

    recv_string_expect_success(client, kBounce);
    assert(get_rcvmore(client) == 1);
    recv_string_expect_success(client, kBounce);
    assert(get_rcvmore(client) == 0);
}

inline void send_bounce_msg_may_fail(zlink::socket_t &socket)
{
    static const char *kBounce = "12345678ABCDEFGH12345678abcdefgh";
    const size_t bounce_len = std::strlen(kBounce);
    const int timeout = 250;
    assert(socket.set(zlink::socket_option::sndtimeo, timeout) == 0);

    const int rc1 = socket.send(kBounce, bounce_len, zlink::send_flag::sndmore);
    assert(rc1 == static_cast<int>(bounce_len)
           || (rc1 == -1 && errno == EAGAIN));

    const int rc2 = socket.send(kBounce, bounce_len);
    assert(rc2 == static_cast<int>(bounce_len)
           || (rc2 == -1 && errno == EAGAIN));
}

inline void recv_bounce_msg_fail(zlink::socket_t &socket)
{
    const int timeout = 250;
    assert(socket.set(zlink::socket_option::rcvtimeo, timeout) == 0);

    char buffer[64];
    const int rc = socket.recv(buffer, sizeof(buffer));
    assert(rc == -1);
    assert(errno == EAGAIN);
}

inline void expect_bounce_fail(zlink::socket_t &server, zlink::socket_t &client)
{
    send_bounce_msg_may_fail(client);
    recv_bounce_msg_fail(server);

    send_bounce_msg_may_fail(server);
    recv_bounce_msg_fail(client);
}

#endif
