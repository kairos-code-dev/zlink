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

template<typename SocketLike>
inline std::string bound_endpoint(SocketLike &socket)
{
    std::string endpoint;
    const int rc =
      socket.get_option(zlink::socket_options::last_endpoint, endpoint);
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

template<typename SocketLike>
inline int set_raw_option(SocketLike &socket,
                          zlink::socket_option option,
                          const void *value,
                          size_t size)
{
    return socket.set_option(option, value, size);
}

template<typename SocketLike, typename T>
inline int set_common_option(SocketLike &socket,
                             zlink::socket_option option,
                             const T &value)
{
    return socket.set_option(option, value);
}

template<typename SocketLike>
inline int get_raw_option(const SocketLike &socket,
                          zlink::socket_option option,
                          void *value,
                          size_t *size)
{
    return socket.get_option(option, value, size);
}

template<typename SocketLike, typename T>
inline int get_common_option(const SocketLike &socket,
                             zlink::socket_option option,
                             T *value)
{
    return socket.get_option(option, value);
}

template<typename SocketLike>
inline int get_common_option(const SocketLike &socket,
                             zlink::socket_option option,
                             std::string &value)
{
    return socket.get_option(option, value);
}

inline zlink::message_t message_from_buffer(const void *data, size_t size)
{
    zlink::message_t msg;
    assert(msg.init_size(size) == 0);
    if (size > 0) {
        assert(data);
        std::memcpy(msg.data(), data, size);
    }
    return msg;
}

template<typename SocketLike>
inline int raw_send(SocketLike &socket,
                    const void *data,
                    size_t size,
                    zlink::send_flag flags = zlink::send_flag::none)
{
    zlink::message_t msg = message_from_buffer(data, size);
    const int rc = socket.send(msg, flags);
    return rc == 0 ? static_cast<int>(size) : -1;
}

template<typename SocketLike>
inline int raw_recv(SocketLike &socket,
                    void *buf,
                    size_t len,
                    zlink::recv_flag flags = zlink::recv_flag::none)
{
    zlink::message_t msg;
    const int rc = socket.recv(msg, flags);
    if (rc != 0)
        return rc;

    if (!buf && len != 0) {
        errno = EINVAL;
        return -1;
    }

    const size_t size = msg.size();
    if (size > len) {
        errno = EMSGSIZE;
        return -1;
    }

    if (size > 0)
        std::memcpy(buf, msg.data(), size);
    return static_cast<int>(size);
}

inline zlink_routing_id_t routing_id_from_bytes(const void *data, size_t size)
{
    zlink_routing_id_t routing_id;
    std::memset(&routing_id, 0, sizeof(routing_id));
    assert(data || size == 0);
    assert(size <= sizeof(routing_id.data));
    routing_id.size = static_cast<uint8_t>(size);
    if (size > 0)
        std::memcpy(routing_id.data, data, size);
    return routing_id;
}

inline zlink_routing_id_t routing_id_from_uint32(uint32_t value)
{
    unsigned char bytes[4];
    bytes[0] = static_cast<unsigned char>((value >> 24) & 0xFFu);
    bytes[1] = static_cast<unsigned char>((value >> 16) & 0xFFu);
    bytes[2] = static_cast<unsigned char>((value >> 8) & 0xFFu);
    bytes[3] = static_cast<unsigned char>(value & 0xFFu);
    return routing_id_from_bytes(bytes, sizeof(bytes));
}

template<typename SocketLike>
inline int routed_raw_send(SocketLike &socket,
                           const zlink_routing_id_t &routing_id,
                           const void *data,
                           size_t size,
                           zlink::send_flag flags = zlink::send_flag::none)
{
    zlink::message_t msg = message_from_buffer(data, size);
    const int rc = socket.send(routing_id, msg, flags);
    return rc == 0 ? static_cast<int>(size) : -1;
}

inline int monitor_recv_nowait(zlink::monitor_handle_t &monitor,
                               zlink_monitor_event_t *event)
{
    const zlink::maybe_t<zlink_socket_monitor_event_t> received =
      monitor.try_recv();
    if (!received) {
        errno = EAGAIN;
        return -1;
    }
    if (event)
        *event = *received;
    return 0;
}

inline size_t auto_routing_id_size(zlink::context_t &ctx, zlink::socket_type type)
{
    void *socket =
      zlink_socket(ctx.handle(), static_cast<zlink_socket_type_t>(type));
    assert(socket != NULL);
    unsigned char routing_id[255];
    std::memset(routing_id, 0, sizeof(routing_id));
    size_t size = sizeof(routing_id);
    assert(
      zlink_get_option(
        socket, static_cast<zlink_option_t>(zlink::socket_option::routing_id),
        routing_id, &size)
      == 0);
    assert(zlink_close(socket) == 0);
    return size;
}

template<typename SocketLike>
inline int recv_with_timeout(SocketLike &socket, void *buf, size_t len, int timeout_ms)
{
    int old_timeout = -1;
    const int have_old_timeout =
      socket.get_option (zlink::socket_options::rcvtimeo, &old_timeout);
    assert (socket.set_option (zlink::socket_options::rcvtimeo, timeout_ms) == 0);

    zlink::message_t msg;
    const int rc = socket.recv (msg);

    if (have_old_timeout == 0)
        assert (socket.set_option (zlink::socket_options::rcvtimeo, old_timeout) == 0);

    if (rc != 0)
        return rc;

    if (!buf && len != 0) {
        errno = EINVAL;
        return -1;
    }

    const size_t size = msg.size ();
    if (size > len) {
        errno = EMSGSIZE;
        return -1;
    }

    if (size > 0)
        std::memcpy (buf, msg.data (), size);
    return static_cast<int> (size);
}

template<typename SocketLike>
inline int recv_msg_with_timeout(SocketLike &socket,
                                 zlink::message_t &msg,
                                 int timeout_ms)
{
    int old_timeout = -1;
    const int have_old_timeout =
      socket.get_option (zlink::socket_options::rcvtimeo, &old_timeout);
    assert (socket.set_option (zlink::socket_options::rcvtimeo, timeout_ms) == 0);
    const int rc = socket.recv (msg);
    if (have_old_timeout == 0)
        assert (socket.set_option (zlink::socket_options::rcvtimeo, old_timeout) == 0);
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

template<typename SocketLike>
inline void send_string_expect_success(SocketLike &socket,
                                       const char *data,
                                       zlink::send_flag flags = zlink::send_flag::none)
{
    assert(data);
    zlink::message_t msg = zlink::message_t::from_string (data);
    assert (socket.send (msg, flags) == 0);
}

template<typename SocketLike>
inline void recv_string_expect_success(SocketLike &socket,
                                       const char *expected,
                                       zlink::recv_flag flags = zlink::recv_flag::none)
{
    assert(expected);
    zlink::message_t msg;
    assert (socket.recv (msg, flags) == 0);
    assert (msg.to_string () == expected);
}

template<typename ServerSocketLike, typename ClientSocketLike>
inline void bounce(ServerSocketLike &server, ClientSocketLike &client)
{
    static const char *kBounce = "12345678ABCDEFGH12345678abcdefgh";
    std::vector<zlink::message_t> outbound;
    outbound.push_back (zlink::message_t::from_string (kBounce));
    outbound.push_back (zlink::message_t::from_string (kBounce));
    assert (client.send (outbound) == 0);

    std::vector<zlink::message_t> inbound;
    assert (server.recv (inbound) == 0);
    assert (inbound.size () == 2);
    assert (inbound[0].to_string () == kBounce);
    assert (inbound[1].to_string () == kBounce);

    outbound.clear ();
    outbound.push_back (zlink::message_t::from_string (kBounce));
    outbound.push_back (zlink::message_t::from_string (kBounce));
    assert (server.send (outbound) == 0);

    inbound.clear ();
    assert (client.recv (inbound) == 0);
    assert (inbound.size () == 2);
    assert (inbound[0].to_string () == kBounce);
    assert (inbound[1].to_string () == kBounce);
}

template<typename SocketLike>
inline void send_bounce_msg_may_fail(SocketLike &socket)
{
    static const char *kBounce = "12345678ABCDEFGH12345678abcdefgh";
    const int timeout = 250;
    assert (socket.set_option (zlink::socket_options::sndtimeo, timeout) == 0);

    std::vector<zlink::message_t> outbound;
    outbound.push_back (zlink::message_t::from_string (kBounce));
    outbound.push_back (zlink::message_t::from_string (kBounce));

    const int rc = socket.send (outbound);
    assert (rc == 0 || (rc == -1 && errno == EAGAIN));
}

template<typename SocketLike>
inline void recv_bounce_msg_fail(SocketLike &socket)
{
    const int timeout = 250;
    assert (socket.set_option (zlink::socket_options::rcvtimeo, timeout) == 0);

    zlink::message_t msg;
    const int rc = socket.recv (msg);
    assert (rc == -1);
    assert (errno == EAGAIN);
}

template<typename ServerSocketLike, typename ClientSocketLike>
inline void expect_bounce_fail(ServerSocketLike &server, ClientSocketLike &client)
{
    send_bounce_msg_may_fail (client);
    recv_bounce_msg_fail (server);

    send_bounce_msg_may_fail (server);
    recv_bounce_msg_fail (client);
}

#endif
