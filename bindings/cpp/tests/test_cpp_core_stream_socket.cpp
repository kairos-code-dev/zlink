#include "test_helpers.hpp"

#include <cstdio>
#include <cstring>

#if !defined(ZLINK_HAVE_WINDOWS)
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace {

#if !defined(ZLINK_HAVE_WINDOWS)
int connect_raw_tcp (const char *endpoint_)
{
    char proto[8] = {0};
    char host[64] = {0};
    int port = 0;
    if (std::sscanf (endpoint_, "%7[^:]://%63[^:]:%d", proto, host, &port) != 3)
        return -1;

    if (std::strcmp (proto, "tcp") != 0)
        return -1;

    const int fd = socket (AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd < 0)
        return -1;

    struct sockaddr_in addr;
    std::memset (&addr, 0, sizeof (addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons (static_cast<uint16_t> (port));

    if (inet_pton (AF_INET, host, &addr.sin_addr) != 1) {
        close (fd);
        return -1;
    }

    if (connect (fd, reinterpret_cast<const struct sockaddr *> (&addr), sizeof (addr)) != 0) {
        close (fd);
        return -1;
    }

    return fd;
}

void test_stream_accept_and_reply ()
{
    zlink::context_t ctx;
    zlink::socket_t stream (ctx, zlink::socket_type::stream);

    const int zero = 0;
    assert (stream.set (zlink::socket_option::linger, zero) == 0);

    const std::string endpoint = endpoint_for (transport_case_t{"tcp", ""},
                                               "stream-socket");
    assert (stream.bind (endpoint) == 0);

    const int raw_fd = connect_raw_tcp (endpoint.c_str ());
    assert (raw_fd >= 0);

    const char hello[] = "HELLO";
    assert (send (raw_fd, hello, sizeof (hello) - 1, 0) == static_cast<int> (sizeof (hello) - 1));

    zlink::message_t rid_msg;
    zlink::message_t payload_msg;
    assert (recv_msg_with_timeout (stream, rid_msg, 2000) > 0);
    assert (recv_msg_with_timeout (stream, payload_msg, 2000) > 0);
    const int rid_size = static_cast<int> (rid_msg.size ());
    assert (rid_size > 0);

    const unsigned char *rid_bytes =
      static_cast<const unsigned char *> (rid_msg.data ());
    assert (rid_size == 4);
    const uint32_t rid = (static_cast<uint32_t> (rid_bytes[0]) << 24)
                         | (static_cast<uint32_t> (rid_bytes[1]) << 16)
                         | (static_cast<uint32_t> (rid_bytes[2]) << 8)
                         | static_cast<uint32_t> (rid_bytes[3]);

    assert (payload_msg.size () == sizeof (hello) - 1);
    assert (std::memcmp (payload_msg.data (), hello, sizeof (hello) - 1) == 0);

    const char world[] = "WORLD";
    assert (stream.stream_send (rid, world, sizeof (world) - 1) == static_cast<int> (sizeof (world) - 1));

    char recv_buf[32];
    const int n = recv (raw_fd, recv_buf, sizeof (recv_buf), 0);
    assert (n == static_cast<int> (sizeof (world) - 1));
    assert (std::memcmp (recv_buf, world, sizeof (world) - 1) == 0);

    close (raw_fd);
}
#endif

} // namespace

int main ()
{
#if !defined(ZLINK_HAVE_WINDOWS)
    test_stream_accept_and_reply ();
#endif
    return 0;
}
