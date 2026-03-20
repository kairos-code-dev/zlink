/* SPDX-License-Identifier: MPL-2.0 */

#include "testutil.hpp"
#include "testutil_unity.hpp"

#include <atomic>
#include <chrono>
#include <cstring>
#include <thread>
#include <vector>

#if defined(ZLINK_HAVE_WINDOWS)
#include <winsock2.h>
#else
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#endif

SETUP_TEARDOWN_TESTCONTEXT

namespace
{
const int kStreamHwm = 10;
const int kSendTimeoutMs = 1000;
const int kWakeupSendTimeoutMs = 5000;
const size_t kPayloadSize = 4096;
const int kRouteIdSize = 4;
const int kSocketBufBytes = 4096;
const int kProbeTimeoutMs = 250;
const int kLingerMs = 0;
const int kFillDeadlineMs = 5000;

struct stream_route_probe_t
{
    stream_route_probe_t () : ready (0), rid ()
    {
        memset (&rid, 0, sizeof (rid));
    }

    std::atomic<int> ready;
    zlink_routing_id_t rid;
};

stream_route_probe_t *g_stream_route_probe = NULL;

void configure_stream_socket (void *socket_)
{
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_option (socket_, ZLINK_OPT_LINGER, &kLingerMs, sizeof (kLingerMs)));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_option (socket_, ZLINK_OPT_SNDHWM, &kStreamHwm, sizeof (kStreamHwm)));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_option (socket_, ZLINK_OPT_RCVHWM, &kStreamHwm, sizeof (kStreamHwm)));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_option (socket_, ZLINK_OPT_SNDBUF, &kSocketBufBytes,
                        sizeof (kSocketBufBytes)));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_option (socket_, ZLINK_OPT_RCVBUF, &kSocketBufBytes,
                        sizeof (kSocketBufBytes)));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_option (socket_, ZLINK_OPT_SNDTIMEO, &kSendTimeoutMs,
                        sizeof (kSendTimeoutMs)));
}

#if defined(ZLINK_HAVE_WINDOWS)
int connect_raw_tcp (const char *endpoint_)
{
    LIBZLINK_UNUSED (endpoint_);
    errno = EOPNOTSUPP;
    return -1;
}

int send_all (int fd_, const unsigned char *buf_, size_t size_)
{
    LIBZLINK_UNUSED (fd_);
    LIBZLINK_UNUSED (buf_);
    LIBZLINK_UNUSED (size_);
    errno = EOPNOTSUPP;
    return -1;
}

int recv_raw (int fd_, unsigned char *buf_, size_t cap_)
{
    LIBZLINK_UNUSED (fd_);
    LIBZLINK_UNUSED (buf_);
    LIBZLINK_UNUSED (cap_);
    errno = EOPNOTSUPP;
    return -1;
}

void close_raw_fd (int fd_)
{
    LIBZLINK_UNUSED (fd_);
}

void set_raw_timeout (int fd_, int timeout_ms_)
{
    LIBZLINK_UNUSED (fd_);
    LIBZLINK_UNUSED (timeout_ms_);
}

#else
bool parse_tcp_endpoint (const char *endpoint_, char host_[64], int *port_)
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

int connect_raw_tcp (const char *endpoint_)
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
        const int err = errno;
        close (fd);
        errno = err;
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

int send_all (int fd_, const unsigned char *buf_, size_t size_)
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

int recv_raw (int fd_, unsigned char *buf_, size_t cap_)
{
    const ssize_t n = recv (fd_, buf_, cap_, 0);
    if (n < 0 && errno == EINTR)
        return recv_raw (fd_, buf_, cap_);
    if (n <= 0)
        return -1;
    return static_cast<int> (n);
}

void close_raw_fd (int fd_)
{
    if (fd_ >= 0)
        close (fd_);
}

void set_raw_timeout (int fd_, int timeout_ms_)
{
    const int rcvbuf_rc =
      setsockopt (fd_, SOL_SOCKET, SO_RCVBUF, &kSocketBufBytes,
                  sizeof (kSocketBufBytes));
    TEST_ASSERT_EQUAL_INT (0, rcvbuf_rc);

    struct timeval tv;
    tv.tv_sec = timeout_ms_ / 1000;
    tv.tv_usec = (timeout_ms_ % 1000) * 1000;
    const int rcv_rc = setsockopt (fd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof (tv));
    TEST_ASSERT_EQUAL_INT (0, rcv_rc);
    const int snd_rc = setsockopt (fd_, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof (tv));
    TEST_ASSERT_EQUAL_INT (0, snd_rc);
}
#endif

bool wait_stream_route_ready (stream_route_probe_t *probe_, int timeout_ms_)
{
    if (!probe_)
        return false;

    const auto deadline =
      std::chrono::steady_clock::now () + std::chrono::milliseconds (timeout_ms_);
    while (std::chrono::steady_clock::now () < deadline) {
        if (probe_->ready.load (std::memory_order_acquire) != 0)
            return true;
        std::this_thread::sleep_for (std::chrono::milliseconds (1));
    }
    return probe_->ready.load (std::memory_order_acquire) != 0;
}

int capture_stream_route_callback (const zlink_routing_id_t *rid_,
                                   zlink_msg_t *msg_,
                                   void *)
{
    if (msg_) {
        if (g_stream_route_probe && rid_
            && zlink_msg_size (msg_) > 0 && rid_->size == kRouteIdSize
            && g_stream_route_probe->ready.load (std::memory_order_acquire) == 0) {
            g_stream_route_probe->rid.size = rid_->size;
            memcpy (g_stream_route_probe->rid.data, rid_->data, kRouteIdSize);
            g_stream_route_probe->ready.store (1, std::memory_order_release);
        }
        (void) zlink_msg_close (msg_);
    }
    return 0;
}

void capture_stream_route_handler (const zlink_routing_id_t *rid_,
                                   zlink_msg_t *parts_,
                                   size_t part_count_,
                                   void *userdata_)
{
    if (part_count_ > 0)
        (void) capture_stream_route_callback (rid_, &parts_[0], userdata_);
    for (size_t i = 1; i < part_count_; ++i)
        (void) zlink_msg_close (&parts_[i]);
}

void establish_stream_route (void *server_, int raw_fd_, zlink_routing_id_t *rid_)
{
    stream_route_probe_t probe;
    g_stream_route_probe = &probe;
    errno = 0;
    const int attach_rc = zlink_recv_handler (server_, &capture_stream_route_handler, NULL);
    TEST_ASSERT_TRUE (attach_rc == 0 || errno == EBUSY);

    const unsigned char request = 0xA5;
    TEST_ASSERT_EQUAL_INT (0, send_all (raw_fd_, &request, sizeof (request)));
    TEST_ASSERT_TRUE (wait_stream_route_ready (&probe, 5000));

    *rid_ = probe.rid;
    g_stream_route_probe = NULL;
}

void fill_stream_send_queue_until_hwm (void *server_, const zlink_routing_id_t *rid_)
{
    std::vector<unsigned char> payload (kPayloadSize, 0x5A);
    int sent = 0;
    const auto stable_full_window = std::chrono::milliseconds (120);
    auto no_success_since = std::chrono::steady_clock::now ();
    const auto deadline =
      std::chrono::steady_clock::now () + std::chrono::milliseconds (kFillDeadlineMs);
    bool reached_full = false;
    for (;;) {
        const int rc =
          test_stream_send_bytes (server_, rid_, &payload[0], kPayloadSize, ZLINK_DONTWAIT);
        if (rc == static_cast<int> (kPayloadSize)) {
            ++sent;
            no_success_since = std::chrono::steady_clock::now ();
            if (std::chrono::steady_clock::now () >= deadline)
                break;
            continue;
        }

        TEST_ASSERT_EQUAL_INT (-1, rc);
        TEST_ASSERT_EQUAL_INT (EAGAIN, errno);

        const auto now = std::chrono::steady_clock::now ();
        if (now - no_success_since >= stable_full_window) {
            reached_full = true;
            break;
        }
        if (now >= deadline)
            break;
        std::this_thread::sleep_for (std::chrono::milliseconds (1));
    }

    TEST_ASSERT_GREATER_THAN_INT (0, sent);
    TEST_ASSERT_TRUE (reached_full);
}
} // namespace

void test_stream_queue_reopens_after_peer_reads ()
{
#if defined(ZLINK_HAVE_WINDOWS)
    TEST_IGNORE_MESSAGE ("raw tcp helper unavailable on Windows");
#else
    void *server = test_context_socket (ZLINK_SOCKET_STREAM);
    TEST_ASSERT_NOT_NULL (server);
    configure_stream_socket (server);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_option (server, ZLINK_OPT_SNDTIMEO, &kWakeupSendTimeoutMs,
                        sizeof (kWakeupSendTimeoutMs)));

    char endpoint[MAX_SOCKET_STRING];
    memset (endpoint, 0, sizeof (endpoint));
    bind_loopback_ipv4 (server, endpoint, sizeof (endpoint));

    const int raw_fd = connect_raw_tcp (endpoint);
    TEST_ASSERT_GREATER_OR_EQUAL_INT (0, raw_fd);
    set_raw_timeout (raw_fd, 200);

    zlink_routing_id_t rid;
    establish_stream_route (server, raw_fd, &rid);
    fill_stream_send_queue_until_hwm (server, &rid);

    std::vector<unsigned char> payload (kPayloadSize, 0x33);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_option (server, ZLINK_OPT_SNDTIMEO, &kProbeTimeoutMs,
                        sizeof (kProbeTimeoutMs)));
    TEST_ASSERT_EQUAL_INT (
      -1, test_stream_send_bytes (server, &rid, &payload[0], kPayloadSize, 0));
    TEST_ASSERT_EQUAL_INT (EAGAIN, errno);

    unsigned char drain_buf[64 * 1024];
    int drained = 0;
    const int drain_target = static_cast<int> (kPayloadSize * ((kStreamHwm + 1) / 2 + 2));
    const auto drain_deadline =
      std::chrono::steady_clock::now () + std::chrono::milliseconds (1000);
    while (std::chrono::steady_clock::now () < drain_deadline
           && drained < drain_target) {
        const int n = recv_raw (raw_fd, drain_buf, sizeof (drain_buf));
        if (n > 0)
            drained += n;
    }

    bool reopened = false;
    const auto reopen_deadline =
      std::chrono::steady_clock::now () + std::chrono::milliseconds (1500);
    while (std::chrono::steady_clock::now () < reopen_deadline) {
        const int send_rc =
          test_stream_send_bytes (server, &rid, &payload[0], kPayloadSize, ZLINK_DONTWAIT);
        if (send_rc == static_cast<int> (kPayloadSize)) {
            reopened = true;
            break;
        }
        TEST_ASSERT_EQUAL_INT (EAGAIN, errno);
        (void) recv_raw (raw_fd, drain_buf, sizeof (drain_buf));
    }
    TEST_ASSERT_TRUE (reopened);

    close_raw_fd (raw_fd);
    test_context_socket_close (server);
#endif
}

void test_stream_blocking_send_times_out_without_peer_reads ()
{
#if defined(ZLINK_HAVE_WINDOWS)
    TEST_IGNORE_MESSAGE ("raw tcp helper unavailable on Windows");
#else
    void *server = test_context_socket (ZLINK_SOCKET_STREAM);
    TEST_ASSERT_NOT_NULL (server);
    configure_stream_socket (server);

    char endpoint[MAX_SOCKET_STRING];
    memset (endpoint, 0, sizeof (endpoint));
    bind_loopback_ipv4 (server, endpoint, sizeof (endpoint));

    const int raw_fd = connect_raw_tcp (endpoint);
    TEST_ASSERT_GREATER_OR_EQUAL_INT (0, raw_fd);
    set_raw_timeout (raw_fd, 200);

    zlink_routing_id_t rid;
    establish_stream_route (server, raw_fd, &rid);
    fill_stream_send_queue_until_hwm (server, &rid);

    std::vector<unsigned char> payload (kPayloadSize, 0x44);
    void *stopwatch = zlink_stopwatch_start ();
    const int send_rc = test_stream_send_bytes (server, &rid, &payload[0], kPayloadSize, 0);
    const unsigned int elapsed_ms = zlink_stopwatch_stop (stopwatch) / 1000;

    TEST_ASSERT_EQUAL_INT (-1, send_rc);
    TEST_ASSERT_EQUAL_INT (EAGAIN, errno);
    TEST_ASSERT_TRUE (
      elapsed_ms >= static_cast<unsigned int> (kSendTimeoutMs - 150));

    close_raw_fd (raw_fd);
    test_context_socket_close (server);
#endif
}

int main ()
{
    setup_test_environment ();

    UNITY_BEGIN ();
    RUN_TEST (test_stream_queue_reopens_after_peer_reads);
    RUN_TEST (test_stream_blocking_send_times_out_without_peer_reads);
    return UNITY_END ();
}
