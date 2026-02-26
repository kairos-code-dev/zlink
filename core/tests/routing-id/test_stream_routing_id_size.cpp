/* SPDX-License-Identifier: MPL-2.0 */

#include "testutil.hpp"
#include "testutil_unity.hpp"

#include <errno.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <atomic>

#if defined(ZLINK_HAVE_WINDOWS)
#include <winsock2.h>
#else
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

SETUP_TEARDOWN_TESTCONTEXT

static const size_t stream_routing_id_size = 4;
struct stream_probe_t
{
    std::atomic<int> calls;
    std::atomic<int> rid_size_ok;
    std::atomic<int> payload_ok;
    stream_probe_t () : calls (0), rid_size_ok (0), payload_ok (0) {}
};

static stream_probe_t *g_stream_probe = NULL;

static bool wait_monitor_event (void *monitor_,
                                void *activity_socket_,
                                uint64_t expected_event_,
                                unsigned char routing_id_[stream_routing_id_size],
                                int timeout_ms_)
{
    const int poll_slice_ms = 200;
    const int poll_timeout = timeout_ms_ > 0 ? timeout_ms_ : 10000;
    const int attempts = poll_timeout / poll_slice_ms + 1;
    for (int i = 0; i < attempts; ++i) {
        zlink_pollitem_t items[] = {
          {monitor_, 0, ZLINK_POLLIN, 0},
          {activity_socket_, 0, ZLINK_POLLIN, 0},
        };
        const int count = activity_socket_ ? 2 : 1;
        const int rc = zlink_poll (items, count, poll_slice_ms);
        if (rc <= 0 || (items[0].revents & ZLINK_POLLIN) == 0)
            continue;

        for (;;) {
            zlink_monitor_event_t event;
            if (zlink_monitor_recv (monitor_, &event, ZLINK_DONTWAIT) != 0)
                break;
            if (event.event != expected_event_)
                continue;
            if (event.routing_id.size != stream_routing_id_size)
                continue;
            memcpy (routing_id_, event.routing_id.data, stream_routing_id_size);
            return true;
        }
    }

    return false;
}

static bool parse_tcp_endpoint (const char *endpoint_,
                                char host_[64],
                                int *port_)
{
    if (!endpoint_ || !host_ || !port_)
        return false;

    char proto[8] = {0};
    int port = 0;
    if (sscanf (endpoint_, "%7[^:]://%63[^:]:%d", proto, host_, &port) != 3)
        return false;

    if (strcmp (proto, "tcp") != 0 || port <= 0 || port > 65535)
        return false;

    *port_ = port;
    return true;
}

#if defined(ZLINK_HAVE_WINDOWS)
static int connect_raw_tcp (const char *endpoint_)
{
    LIBZLINK_UNUSED (endpoint_);
    errno = EOPNOTSUPP;
    return -1;
}

static int send_stream_packet (int fd_, const void *data_, size_t size_)
{
    LIBZLINK_UNUSED (fd_);
    LIBZLINK_UNUSED (data_);
    LIBZLINK_UNUSED (size_);
    return EOPNOTSUPP;
}

static void close_raw_fd (int fd_)
{
    LIBZLINK_UNUSED (fd_);
}
#else
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
    memset (&addr, 0, sizeof (addr));
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

static int send_all (int fd_, const unsigned char *buf_, size_t size_)
{
    size_t off = 0;
    while (off < size_) {
        const ssize_t n = send (fd_, buf_ + off, size_ - off, 0);
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

static int send_stream_packet (int fd_, const void *data_, size_t size_)
{
    return send_all (fd_, static_cast<const unsigned char *> (data_), size_);
}

static void close_raw_fd (int fd_)
{
    if (fd_ >= 0)
        close (fd_);
}
#endif

static int on_stream_packet (const zlink_routing_id_t *rid_,
                             zlink_msg_t *msg_)
{
    stream_probe_t *p = g_stream_probe;
    if (!p || !rid_ || !msg_)
        return 0;

    p->calls.fetch_add (1, std::memory_order_release);
    if (rid_->size == stream_routing_id_size)
        p->rid_size_ok.store (1, std::memory_order_release);

    const unsigned char *payload =
      static_cast<const unsigned char *> (zlink_msg_data (msg_));
    const size_t payload_size = zlink_msg_size (msg_);
    if (payload_size == 1 && payload && payload[0] == 'x')
        p->payload_ok.store (1, std::memory_order_release);
    (void) zlink_msg_close (msg_);

    return 0;
}

void test_stream_auto_routing_id_size ()
{
    stream_probe_t probe;

    void *server = test_context_socket (ZLINK_STREAM);
    TEST_ASSERT_NOT_NULL (server);

    const int zero = 0;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_setsockopt (server, ZLINK_LINGER, &zero, sizeof (zero)));

    g_stream_probe = &probe;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_stream_attach_raw (server, on_stream_packet));

    char endpoint[MAX_SOCKET_STRING];
    bind_loopback_ipv4 (server, endpoint, sizeof endpoint);

    void *monitor =
      zlink_socket_monitor_open (server, ZLINK_EVENT_CONNECTION_READY);
    TEST_ASSERT_NOT_NULL (monitor);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_setsockopt (monitor, ZLINK_LINGER, &zero, sizeof (zero)));

    const int client_fd = connect_raw_tcp (endpoint);
    TEST_ASSERT_TRUE (client_fd >= 0);

    const char payload[] = "x";
    TEST_ASSERT_EQUAL_INT (0, send_stream_packet (
                                client_fd, payload, sizeof (payload) - 1));

    for (int i = 0; i < 200; ++i) {
        if (probe.calls.load (std::memory_order_acquire) > 0)
            break;
#if defined(ZLINK_HAVE_WINDOWS)
        Sleep (10);
#else
        usleep (10000);
#endif
    }

    TEST_ASSERT_TRUE (probe.calls.load (std::memory_order_acquire) > 0);
    TEST_ASSERT_EQUAL_INT (1, probe.rid_size_ok.load (std::memory_order_acquire));
    TEST_ASSERT_EQUAL_INT (1, probe.payload_ok.load (std::memory_order_acquire));

    close_raw_fd (client_fd);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_stream_detach (server));
    g_stream_probe = NULL;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_close (monitor));
    test_context_socket_close_zero_linger (server);
}

int main ()
{
    setup_test_environment ();

    UNITY_BEGIN ();
    RUN_TEST (test_stream_auto_routing_id_size);
    return UNITY_END ();
}
