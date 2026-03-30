#include "test_helpers.hpp"

#if !defined(ZLINK_HAVE_WINDOWS)
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <cstring>

namespace {

static bool wait_monitor_event (zlink::monitor_handle_t &monitor_, uint64_t event_)
{
    for (int i = 0; i < 200; ++i) {
        zlink_monitor_event_t ev;
        while (monitor_recv_nowait (monitor_, &ev) == 0) {
            if (ev.event == event_)
                return true;
        }
        sleep_ms (10);
    }
    return false;
}

#if !defined(ZLINK_HAVE_WINDOWS)
static int connect_raw_tcp (const std::string &endpoint_)
{
    char proto[8] = {0};
    char host[64] = {0};
    int port = 0;
    if (std::sscanf (endpoint_.c_str (), "%7[^:]://%63[^:]:%d", proto, host, &port)
        != 3)
        return -1;
    if (std::strcmp (proto, "tcp") != 0 || port <= 0 || port > 65535)
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

    if (connect (fd, reinterpret_cast<const struct sockaddr *> (&addr),
                 sizeof (addr))
        != 0) {
        close (fd);
        return -1;
    }
    return fd;
}
#endif

void test_handshake_timeout ()
{
    zlink::context_t ctx;
    zlink::router_socket_t server (ctx);

    const int timeout = 100;
    const int linger = 0;
    assert (server.set_option (zlink::socket_option::handshake_ivl, timeout) == 0);
    assert (server.set_option (zlink::socket_option::linger, linger) == 0);

    const std::string endpoint =
      endpoint_for (transport_case_t{"tcp", ""}, "heartbeats");
    assert (server.bind (endpoint) == 0);

    zlink::monitor_handle_t monitor = server.monitor_handle (
      zlink::monitor_event::accepted | zlink::monitor_event::disconnected);

#if !defined(ZLINK_HAVE_WINDOWS)
    const int fd = connect_raw_tcp (endpoint);
    assert (fd >= 0);

    assert (wait_monitor_event (
      monitor, static_cast<uint64_t> (ZLINK_EVENT_ACCEPTED)));
    assert (wait_monitor_event (
      monitor, static_cast<uint64_t> (ZLINK_EVENT_DISCONNECTED)));

    close (fd);
#else
    (void) monitor;
#endif
}

} // namespace

int main ()
{
    test_handshake_timeout ();
    return 0;
}
