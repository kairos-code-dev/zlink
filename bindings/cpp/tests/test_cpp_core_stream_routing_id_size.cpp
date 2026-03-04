#include "test_helpers.hpp"

#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstring>

#if defined(_WIN32)
#include <winsock2.h>
#else
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace {

static std::atomic<int> g_calls (0);
static std::atomic<int> g_rid_size_ok (0);
static std::atomic<int> g_payload_ok (0);

static int on_stream_packet (const zlink_routing_id_t *rid_, zlink_msg_t *msg_)
{
    if (!rid_ || !msg_)
        return 0;

    g_calls.fetch_add (1, std::memory_order_release);
    if (rid_->size == 4)
        g_rid_size_ok.store (1, std::memory_order_release);

    const unsigned char *payload =
      static_cast<const unsigned char *> (zlink_msg_data (msg_));
    const size_t size = zlink_msg_size (msg_);
    if (size == 1 && payload && payload[0] == 'x')
        g_payload_ok.store (1, std::memory_order_release);

    (void) zlink_msg_close (msg_);
    return 0;
}

#if defined(_WIN32)
static int connect_raw_tcp (const char *)
{
    errno = EOPNOTSUPP;
    return -1;
}

static int send_stream_packet (int, const void *, size_t)
{
    errno = EOPNOTSUPP;
    return -1;
}

static void close_raw_fd (int)
{
}
#else
static bool parse_tcp_endpoint (const char *endpoint_, char host_[64], int *port_)
{
    if (!endpoint_ || !host_ || !port_)
        return false;

    char proto[8] = {0};
    int port = 0;
    if (std::sscanf (endpoint_, "%7[^:]://%63[^:]:%d", proto, host_, &port) != 3)
        return false;

    if (std::strcmp (proto, "tcp") != 0 || port <= 0 || port > 65535)
        return false;

    *port_ = port;
    return true;
}

static int connect_raw_tcp (const char *endpoint_)
{
    char host[64];
    int port = 0;
    if (!parse_tcp_endpoint (endpoint_, host, &port)) {
        errno = EINVAL;
        return -1;
    }

    const int fd = socket (AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd < 0)
        return -1;

    struct sockaddr_in addr;
    std::memset (&addr, 0, sizeof (addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons (static_cast<uint16_t> (port));
    if (inet_pton (AF_INET, host, &addr.sin_addr) != 1) {
        close (fd);
        errno = EINVAL;
        return -1;
    }

    if (connect (fd, reinterpret_cast<const struct sockaddr *> (&addr),
                 sizeof (addr))
        != 0) {
        const int err = errno;
        close (fd);
        errno = err;
        return -1;
    }

    return fd;
}

static int send_stream_packet (int fd_, const void *data_, size_t size_)
{
    const unsigned char *src = static_cast<const unsigned char *> (data_);
    size_t off = 0;
    while (off < size_) {
        const ssize_t n = send (fd_, src + off, size_ - off, 0);
        if (n > 0) {
            off += static_cast<size_t> (n);
            continue;
        }
        if (n < 0 && errno == EINTR)
            continue;
        return -1;
    }
    return 0;
}

static void close_raw_fd (int fd_)
{
    if (fd_ >= 0)
        close (fd_);
}
#endif

} // namespace

int main ()
{
#if defined(_WIN32)
    return 0;
#else
    g_calls.store (0, std::memory_order_release);
    g_rid_size_ok.store (0, std::memory_order_release);
    g_payload_ok.store (0, std::memory_order_release);

    zlink::context_t ctx;
    zlink::socket_t server (ctx, zlink::socket_type::stream);
    const int zero = 0;
    assert (server.set (zlink::socket_option::linger, zero) == 0);

    assert (server.stream_attach_raw (&on_stream_packet) == 0);

    assert (server.bind ("tcp://127.0.0.1:*") == 0);
    const std::string endpoint = bound_endpoint (server);

    zlink::socket_t monitor =
      server.monitor_open (zlink::monitor_event::connection_ready);
    assert (monitor.set (zlink::socket_option::linger, zero) == 0);

    const int client_fd = connect_raw_tcp (endpoint.c_str ());
    assert (client_fd >= 0);

    const char payload[] = "x";
    assert (send_stream_packet (client_fd, payload, 1) == 0);

    const auto deadline =
      std::chrono::steady_clock::now () + std::chrono::milliseconds (2000);
    while (g_calls.load (std::memory_order_acquire) == 0
           && std::chrono::steady_clock::now () < deadline)
        sleep_ms (10);

    assert (g_calls.load (std::memory_order_acquire) > 0);
    assert (g_rid_size_ok.load (std::memory_order_acquire) == 1);
    assert (g_payload_ok.load (std::memory_order_acquire) == 1);

    close_raw_fd (client_fd);
    assert (server.stream_detach () == 0);
    assert (monitor.close () == 0);

    return 0;
#endif
}
