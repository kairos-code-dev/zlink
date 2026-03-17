/* SPDX-License-Identifier: MPL-2.0 */

#include "testutil.hpp"
#include "testutil_monitoring.hpp"
#include "testutil_unity.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string.h>

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
static const size_t stream_routing_id_size = 4;

struct stream_contract_probe_t
{
    stream_contract_probe_t () :
        socket (NULL),
        expected_payload (NULL),
        expected_size (0),
        matched (0),
        send_ok (0),
        send_errno (0),
        routing_id_ready (0)
    {
        memset (routing_id, 0, sizeof (routing_id));
    }

    void *socket;
    const unsigned char *expected_payload;
    size_t expected_size;
    std::mutex mutex;
    std::condition_variable cv;
    int matched;
    int send_ok;
    int send_errno;
    int routing_id_ready;
    unsigned char routing_id[stream_routing_id_size];
};

stream_contract_probe_t *g_stream_contract_probe = NULL;

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

static int recv_stream_packet (int fd_, void *buf_, size_t cap_)
{
    LIBZLINK_UNUSED (fd_);
    LIBZLINK_UNUSED (buf_);
    LIBZLINK_UNUSED (cap_);
    return -1;
}

static void close_raw_fd (int fd_)
{
    LIBZLINK_UNUSED (fd_);
}

static int set_raw_fd_timeout (int fd_, int timeout_ms_)
{
    LIBZLINK_UNUSED (fd_);
    LIBZLINK_UNUSED (timeout_ms_);
    errno = EOPNOTSUPP;
    return -1;
}

static int recv_exact (int fd_, void *buf_, size_t size_)
{
    LIBZLINK_UNUSED (fd_);
    LIBZLINK_UNUSED (buf_);
    LIBZLINK_UNUSED (size_);
    return -1;
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

static int recv_stream_packet (int fd_, void *buf_, size_t cap_)
{
    const ssize_t n = recv (fd_, static_cast<unsigned char *> (buf_), cap_, 0);
    if (n <= 0)
        return -1;
    return static_cast<int> (n);
}

static void close_raw_fd (int fd_)
{
    if (fd_ >= 0)
        close (fd_);
}

static int set_raw_fd_timeout (int fd_, int timeout_ms_)
{
    struct timeval tv;
    tv.tv_sec = timeout_ms_ / 1000;
    tv.tv_usec = (timeout_ms_ % 1000) * 1000;
    if (setsockopt (fd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof (tv)) != 0)
        return -1;
    if (setsockopt (fd_, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof (tv)) != 0)
        return -1;
    return 0;
}

static int recv_exact (int fd_, void *buf_, size_t size_)
{
    unsigned char *dst = static_cast<unsigned char *> (buf_);
    size_t off = 0;
    while (off < size_) {
        const ssize_t n = recv (fd_, dst + off, size_ - off, 0);
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
#endif

static void configure_stream_contract_socket (void *socket_)
{
    TEST_ASSERT_NOT_NULL (socket_);

    const int zero = 0;
    const int hwm = 10;
    const int timeout_ms = 200;
    const int nodelay = 1;
    const int backlog = 256;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_setsockopt (socket_, ZLINK_LINGER, &zero, sizeof (zero)));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_setsockopt (socket_, ZLINK_SNDHWM, &hwm, sizeof (hwm)));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_setsockopt (socket_, ZLINK_RCVHWM, &hwm, sizeof (hwm)));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_setsockopt (socket_, ZLINK_SNDTIMEO, &timeout_ms,
                        sizeof (timeout_ms)));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_setsockopt (socket_, ZLINK_RCVTIMEO, &timeout_ms,
                        sizeof (timeout_ms)));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_setsockopt (socket_, ZLINK_BACKLOG, &backlog, sizeof (backlog)));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_setsockopt (socket_, ZLINK_TCP_NODELAY, &nodelay,
                        sizeof (nodelay)));
}

static bool wait_monitor_event (void *monitor_,
                                uint64_t expected_event_,
                                unsigned char routing_id_[stream_routing_id_size],
                                int timeout_ms_)
{
    const int slice_ms = 200;
    const int loops = timeout_ms_ > 0 ? timeout_ms_ / slice_ms + 1 : 1;

    for (int i = 0; i < loops; ++i) {
        if (zlink::wait_socket_events_internal (monitor_, 1, slice_ms) <= 0)
            continue;

        for (;;) {
            zlink_monitor_event_t event;
            if (recv_monitor_event_from_socket (monitor_, &event, ZLINK_DONTWAIT)
                != 0) {
                break;
            }
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

static void send_stream_msg (void *socket_,
                             const unsigned char routing_id_[stream_routing_id_size],
                             const void *data_,
                             size_t size_)
{
    TEST_ASSERT_EQUAL_INT (
      static_cast<int> (stream_routing_id_size),
      TEST_ASSERT_SUCCESS_ERRNO (zlink_send (socket_, routing_id_,
                                             stream_routing_id_size,
                                             ZLINK_SNDMORE)));
    TEST_ASSERT_EQUAL_INT (
      static_cast<int> (size_),
      TEST_ASSERT_SUCCESS_ERRNO (zlink_send (socket_, data_, size_, 0)));
}

static bool is_stream_control_chunk (const unsigned char *data_, size_t size_)
{
    (void) data_;
    return size_ == 0;
}

static void release_stream_callback_msg (zlink_msg_t *msg_)
{
    if (msg_)
        (void) zlink_msg_close (msg_);
}

static int stream_contract_echo_callback (const zlink_routing_id_t *rid_,
                                          zlink_msg_t *msg_,
                                   void *)
{
    stream_contract_probe_t *probe = g_stream_contract_probe;
    if (!probe || !rid_ || !msg_) {
        release_stream_callback_msg (msg_);
        return 0;
    }

    const unsigned char *data =
      static_cast<const unsigned char *> (zlink_msg_data (msg_));
    const size_t size = zlink_msg_size (msg_);
    if (is_stream_control_chunk (data, size)) {
        release_stream_callback_msg (msg_);
        return 0;
    }

    {
        std::unique_lock<std::mutex> lock (probe->mutex);
        if (probe->expected_payload && size == probe->expected_size
            && memcmp (data, probe->expected_payload, size) == 0) {
            if (rid_->size == stream_routing_id_size) {
                memcpy (probe->routing_id, rid_->data, stream_routing_id_size);
                probe->routing_id_ready = 1;
            }
            ++probe->matched;
        }
    }

    const int send_rc = zlink_stream_send_msg (probe->socket, rid_, msg_, 0);

    {
        std::unique_lock<std::mutex> lock (probe->mutex);
        if (send_rc == static_cast<int> (size))
            probe->send_ok = 1;
        else
            probe->send_errno = errno;
    }

    release_stream_callback_msg (msg_);
    probe->cv.notify_all ();
    return 0;
}

static void stream_contract_echo_handler (const zlink_routing_id_t *rid_,
                                          zlink_msg_t *parts_,
                                          size_t part_count_,
                                          void *userdata_)
{
    if (part_count_ > 0)
        (void) stream_contract_echo_callback (rid_, &parts_[0], userdata_);
    for (size_t i = 1; i < part_count_; ++i)
        (void) zlink_msg_close (&parts_[i]);
}

static bool wait_stream_probe_ready (stream_contract_probe_t *probe_,
                                     int timeout_ms_)
{
    if (!probe_)
        return false;

    std::unique_lock<std::mutex> lock (probe_->mutex);
    return probe_->cv.wait_for (lock, std::chrono::milliseconds (timeout_ms_),
                                [probe_]() {
                                    return probe_->matched >= 1
                                           && probe_->send_ok == 1
                                           && probe_->routing_id_ready == 1;
                                });
}
}

void test_stream_monitor_ready_implies_first_payload_contract ()
{
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_ctx_set (get_test_context (), ZLINK_IO_THREADS, 8));

    void *server = test_context_socket (ZLINK_STREAM);
    TEST_ASSERT_NOT_NULL (server);
    configure_stream_contract_socket (server);

    char endpoint[MAX_SOCKET_STRING];
    bind_loopback_ipv4 (server, endpoint, sizeof (endpoint));

    void *monitor = zlink_socket_monitor_open (
      server, ZLINK_EVENT_CONNECTION_READY | ZLINK_EVENT_DISCONNECTED,
      &zlink_monitor_ignore_handler, NULL);
    TEST_ASSERT_NOT_NULL (monitor);

    const int zero = 0;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_setsockopt (monitor, ZLINK_LINGER, &zero, sizeof (zero)));

    stream_contract_probe_t probe;
    const unsigned char client_payload[] = "stream-client-payload";
    probe.socket = server;
    probe.expected_payload = client_payload;
    probe.expected_size = sizeof (client_payload) - 1;
    g_stream_contract_probe = &probe;
    zlink_socket_handler_t handler;
    memset (&handler, 0, sizeof (handler));
    handler.kind = ZLINK_SOCKET_HANDLER_MSG;
    handler.fn.msg = &stream_contract_echo_handler;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_socket_attach_handler (server, &handler));

    const int client_fd = connect_raw_tcp (endpoint);
    TEST_ASSERT_TRUE (client_fd >= 0);
    TEST_ASSERT_EQUAL_INT (0, set_raw_fd_timeout (client_fd, 3000));

    TEST_ASSERT_EQUAL_INT (
      0, send_stream_packet (client_fd, client_payload,
                             sizeof (client_payload) - 1));
    TEST_ASSERT_TRUE (wait_stream_probe_ready (&probe, 5000));
    TEST_ASSERT_EQUAL_INT (0, probe.send_errno);

    unsigned char echoed[sizeof (client_payload)];
    TEST_ASSERT_EQUAL_INT (
      static_cast<int> (sizeof (client_payload) - 1),
      recv_stream_packet (client_fd, echoed, sizeof (echoed)));
    TEST_ASSERT_EQUAL_UINT8_ARRAY (
      client_payload, echoed,
      static_cast<unsigned int> (sizeof (client_payload) - 1));

    unsigned char ready_routing_id[stream_routing_id_size];
    TEST_ASSERT_TRUE (wait_monitor_event (
      monitor, ZLINK_EVENT_CONNECTION_READY, ready_routing_id, 3000));
    TEST_ASSERT_EQUAL_UINT8_ARRAY (
      ready_routing_id, probe.routing_id,
      static_cast<unsigned int> (stream_routing_id_size));

    const unsigned char server_payload[] = "stream-server-payload";
    send_stream_msg (server, ready_routing_id, server_payload,
                     sizeof (server_payload) - 1);

    unsigned char recv_buf[sizeof (server_payload)];
    TEST_ASSERT_EQUAL_INT (
      0, recv_exact (client_fd, recv_buf, sizeof (server_payload) - 1));
    TEST_ASSERT_EQUAL_UINT8_ARRAY (
      server_payload, recv_buf,
      static_cast<unsigned int> (sizeof (server_payload) - 1));

    close_raw_fd (client_fd);

    unsigned char disconnected_routing_id[stream_routing_id_size];
    TEST_ASSERT_TRUE (wait_monitor_event (
      monitor, ZLINK_EVENT_DISCONNECTED, disconnected_routing_id, 10000));
    TEST_ASSERT_EQUAL_UINT8_ARRAY (
      ready_routing_id, disconnected_routing_id,
      static_cast<unsigned int> (stream_routing_id_size));

    g_stream_contract_probe = NULL;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_close (monitor));
    test_context_socket_close_zero_linger (server);
}

int main ()
{
    setup_test_environment ();

    UNITY_BEGIN ();
    RUN_TEST (test_stream_monitor_ready_implies_first_payload_contract);
    return UNITY_END ();
}
