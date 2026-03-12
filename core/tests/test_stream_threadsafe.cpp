/* SPDX-License-Identifier: MPL-2.0 */

#include "testutil.hpp"
#include "testutil_unity.hpp"

#include <atomic>
#include <cstdlib>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <mutex>
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
const int kRouteIdSize = 4;
const int kLingerMs = 0;
const int kTimeoutMs = 1000;
const int kSocketBufBytes = 1 << 16;
const int kConcurrentSenders = 4;
const int kMessagesPerSender = 64;
const size_t kPayloadSize = 64;

struct route_probe_t
{
    route_probe_t () : ready (0), rid ()
    {
        memset (&rid, 0, sizeof (rid));
    }

    std::atomic<int> ready;
    zlink_routing_id_t rid;
};

struct lifecycle_probe_t
{
    lifecycle_probe_t () :
        socket (NULL),
        hits (0),
        detach_rc (1),
        detach_errno (0),
        close_rc (1),
        close_errno (0)
    {
    }

    void *socket;
    std::atomic<int> hits;
    std::atomic<int> detach_rc;
    std::atomic<int> detach_errno;
    std::atomic<int> close_rc;
    std::atomic<int> close_errno;
};

struct worker_probe_t
{
    worker_probe_t () :
        socket (NULL),
        has_message (false),
        done (false),
        send_rc (-1),
        send_errno (0)
    {
        memset (&rid, 0, sizeof (rid));
        if (zlink_msg_init (&msg) != 0)
            std::abort ();
    }

    ~worker_probe_t () { (void) zlink_msg_close (&msg); }

    void *socket;
    std::mutex mutex;
    std::condition_variable cv;
    bool has_message;
    bool done;
    int send_rc;
    int send_errno;
    zlink_routing_id_t rid;
    zlink_msg_t msg;
};

route_probe_t *g_route_probe = NULL;
lifecycle_probe_t *g_lifecycle_probe = NULL;
worker_probe_t *g_worker_probe = NULL;

void configure_stream_socket (void *socket_)
{
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_setsockopt (socket_, ZLINK_LINGER, &kLingerMs, sizeof (kLingerMs)));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_setsockopt (socket_, ZLINK_SNDBUF, &kSocketBufBytes,
                        sizeof (kSocketBufBytes)));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_setsockopt (socket_, ZLINK_RCVBUF, &kSocketBufBytes,
                        sizeof (kSocketBufBytes)));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_setsockopt (socket_, ZLINK_SNDTIMEO, &kTimeoutMs,
                        sizeof (kTimeoutMs)));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_setsockopt (socket_, ZLINK_RCVTIMEO, &kTimeoutMs,
                        sizeof (kTimeoutMs)));
}

#if defined(ZLINK_HAVE_WINDOWS)
int connect_raw_tcp (const char *endpoint_)
{
    LIBZLINK_UNUSED (endpoint_);
    errno = EOPNOTSUPP;
    return -1;
}

void close_raw_fd (int fd_)
{
    LIBZLINK_UNUSED (fd_);
}

int send_all (int fd_, const unsigned char *buf_, size_t size_)
{
    LIBZLINK_UNUSED (fd_);
    LIBZLINK_UNUSED (buf_);
    LIBZLINK_UNUSED (size_);
    errno = EOPNOTSUPP;
    return -1;
}

void set_raw_timeout (int fd_, int timeout_ms_)
{
    LIBZLINK_UNUSED (fd_);
    LIBZLINK_UNUSED (timeout_ms_);
}

int recv_raw (int fd_, unsigned char *buf_, size_t cap_)
{
    LIBZLINK_UNUSED (fd_);
    LIBZLINK_UNUSED (buf_);
    LIBZLINK_UNUSED (cap_);
    errno = EOPNOTSUPP;
    return -1;
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

void close_raw_fd (int fd_)
{
    if (fd_ >= 0)
        close (fd_);
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

void set_raw_timeout (int fd_, int timeout_ms_)
{
    struct timeval tv;
    tv.tv_sec = timeout_ms_ / 1000;
    tv.tv_usec = (timeout_ms_ % 1000) * 1000;
    TEST_ASSERT_EQUAL_INT (
      0, setsockopt (fd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof (tv)));
    TEST_ASSERT_EQUAL_INT (
      0, setsockopt (fd_, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof (tv)));
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
#endif

bool wait_flag (std::atomic<int> *flag_, int timeout_ms_)
{
    const auto deadline =
      std::chrono::steady_clock::now () + std::chrono::milliseconds (timeout_ms_);
    while (std::chrono::steady_clock::now () < deadline) {
        if (flag_->load (std::memory_order_acquire) != 0)
            return true;
        std::this_thread::sleep_for (std::chrono::milliseconds (1));
    }
    return flag_->load (std::memory_order_acquire) != 0;
}

int capture_route_callback (const zlink_routing_id_t *rid_, zlink_msg_t *msg_)
{
    if (g_route_probe && rid_ && msg_ && rid_->size == kRouteIdSize
        && zlink_msg_size (msg_) > 0
        && g_route_probe->ready.load (std::memory_order_acquire) == 0) {
        g_route_probe->rid.size = rid_->size;
        memcpy (g_route_probe->rid.data, rid_->data, kRouteIdSize);
        g_route_probe->ready.store (1, std::memory_order_release);
    }

    if (msg_)
        (void) zlink_msg_close (msg_);
    return 0;
}

int lifecycle_reject_callback (const zlink_routing_id_t *, zlink_msg_t *msg_)
{
    lifecycle_probe_t *probe = g_lifecycle_probe;
    if (!probe || !probe->socket || !msg_)
        return 0;

    probe->hits.fetch_add (1, std::memory_order_release);

    errno = 0;
    const int detach_rc = zlink_stream_detach (probe->socket);
    probe->detach_rc.store (detach_rc, std::memory_order_release);
    probe->detach_errno.store (errno, std::memory_order_release);

    errno = 0;
    const int close_rc = zlink_close (probe->socket);
    probe->close_rc.store (close_rc, std::memory_order_release);
    probe->close_errno.store (errno, std::memory_order_release);

    (void) zlink_msg_close (msg_);
    return 0;
}

void establish_route (void *server_, int raw_fd_, zlink_routing_id_t *rid_,
                      bool keep_attached_)
{
    route_probe_t probe;
    g_route_probe = &probe;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_stream_attach_raw (server_, capture_route_callback));

    const unsigned char payload = 0xA5;
    TEST_ASSERT_EQUAL_INT (0, send_all (raw_fd_, &payload, sizeof (payload)));
    TEST_ASSERT_TRUE (wait_flag (&probe.ready, 5000));

    *rid_ = probe.rid;
    g_route_probe = NULL;

    if (!keep_attached_)
        TEST_ASSERT_SUCCESS_ERRNO (zlink_stream_detach (server_));
}

void drain_exact_bytes (int fd_,
                        size_t expected_bytes_,
                        std::atomic<size_t> *received_,
                        std::atomic<int> *errors_)
{
    unsigned char buf[4096];
    const auto deadline =
      std::chrono::steady_clock::now () + std::chrono::seconds (10);

    while (received_->load (std::memory_order_acquire) < expected_bytes_
           && std::chrono::steady_clock::now () < deadline) {
        const int n = recv_raw (fd_, buf, sizeof (buf));
        if (n > 0) {
            received_->fetch_add (static_cast<size_t> (n),
                                  std::memory_order_release);
            continue;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            continue;
        errors_->fetch_add (1, std::memory_order_release);
        return;
    }

    if (received_->load (std::memory_order_acquire) < expected_bytes_)
        errors_->fetch_add (1, std::memory_order_release);
}

bool recv_exact_bytes (int fd_, unsigned char *buf_, size_t size_)
{
    size_t off = 0;
    while (off < size_) {
        const int n = recv_raw (fd_, buf_ + off, size_ - off);
        if (n > 0) {
            off += static_cast<size_t> (n);
            continue;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            continue;
        return false;
    }
    return true;
}

void sender_thread_run (void *server_,
                        const zlink_routing_id_t rid_,
                        unsigned char fill_,
                        int messages_,
                        std::atomic<int> *errors_)
{
    std::vector<unsigned char> payload (kPayloadSize, fill_);
    for (int i = 0; i < messages_; ++i) {
        const int rc = zlink_stream_send (
          server_, &rid_, &payload[0], payload.size (), 0);
        if (rc != static_cast<int> (payload.size ())) {
            errors_->fetch_add (1, std::memory_order_release);
            return;
        }
    }
}

int handoff_to_worker_callback (const zlink_routing_id_t *rid_, zlink_msg_t *msg_)
{
    worker_probe_t *probe = g_worker_probe;
    if (!probe || !rid_ || !msg_)
        return 0;

    const unsigned char *payload =
      static_cast<const unsigned char *> (zlink_msg_data (msg_));
    const size_t payload_size = zlink_msg_size (msg_);
    if (payload_size == 0
        || (payload_size == 1 && payload
            && (payload[0] == 0x00 || payload[0] == 0x01))) {
        (void) zlink_msg_close (msg_);
        return 0;
    }

    {
        std::lock_guard<std::mutex> lk (probe->mutex);
        if (!probe->has_message) {
            probe->rid = *rid_;
            TEST_ASSERT_EQUAL_INT (0, zlink_msg_move (&probe->msg, msg_));
            probe->has_message = true;
        }
    }
    probe->cv.notify_one ();
    (void) zlink_msg_close (msg_);
    return 0;
}

void worker_send_msg_run (worker_probe_t *probe_)
{
    std::unique_lock<std::mutex> lk (probe_->mutex);
    const bool ready = probe_->cv.wait_for (
      lk, std::chrono::seconds (5), [probe_]() { return probe_->has_message; });
    if (!ready) {
        probe_->send_rc = -1;
        probe_->send_errno = ETIMEDOUT;
        probe_->done = true;
        lk.unlock ();
        probe_->cv.notify_one ();
        return;
    }

    const zlink_routing_id_t rid = probe_->rid;
    lk.unlock ();

    errno = 0;
    const int rc = zlink_stream_send_msg (probe_->socket, &rid, &probe_->msg, 0);
    const int err = errno;

    lk.lock ();
    probe_->send_rc = rc;
    probe_->send_errno = err;
    probe_->done = true;
    lk.unlock ();
    probe_->cv.notify_one ();
}
}

void test_stream_callback_rejects_detach_and_close ()
{
#if defined(ZLINK_HAVE_WINDOWS)
    TEST_IGNORE_MESSAGE ("raw tcp helper unavailable on Windows");
#else
    void *server = test_context_socket (ZLINK_STREAM);
    TEST_ASSERT_NOT_NULL (server);
    configure_stream_socket (server);

    char endpoint[MAX_SOCKET_STRING];
    bind_loopback_ipv4 (server, endpoint, sizeof (endpoint));

    const int raw_fd = connect_raw_tcp (endpoint);
    TEST_ASSERT_GREATER_OR_EQUAL_INT (0, raw_fd);
    set_raw_timeout (raw_fd, 500);

    lifecycle_probe_t probe;
    probe.socket = server;
    g_lifecycle_probe = &probe;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_stream_attach_raw (server, lifecycle_reject_callback));

    const unsigned char payload = 0x41;
    TEST_ASSERT_EQUAL_INT (0, send_all (raw_fd, &payload, sizeof (payload)));
    TEST_ASSERT_TRUE (wait_flag (&probe.hits, 5000));

    TEST_ASSERT_EQUAL_INT (-1, probe.detach_rc.load (std::memory_order_acquire));
    TEST_ASSERT_EQUAL_INT (EBUSY,
                           probe.detach_errno.load (std::memory_order_acquire));
    TEST_ASSERT_EQUAL_INT (-1, probe.close_rc.load (std::memory_order_acquire));
    TEST_ASSERT_EQUAL_INT (EBUSY,
                           probe.close_errno.load (std::memory_order_acquire));

    TEST_ASSERT_SUCCESS_ERRNO (zlink_stream_detach (server));
    g_lifecycle_probe = NULL;
    close_raw_fd (raw_fd);
    test_context_socket_close_zero_linger (server);
#endif
}

void test_stream_send_is_thread_safe_across_app_threads ()
{
#if defined(ZLINK_HAVE_WINDOWS)
    TEST_IGNORE_MESSAGE ("raw tcp helper unavailable on Windows");
#else
    void *server = test_context_socket (ZLINK_STREAM);
    TEST_ASSERT_NOT_NULL (server);
    configure_stream_socket (server);

    char endpoint[MAX_SOCKET_STRING];
    bind_loopback_ipv4 (server, endpoint, sizeof (endpoint));

    const int raw_fd = connect_raw_tcp (endpoint);
    TEST_ASSERT_GREATER_OR_EQUAL_INT (0, raw_fd);
    set_raw_timeout (raw_fd, 500);

    zlink_routing_id_t rid;
    establish_route (server, raw_fd, &rid, false);

    std::atomic<size_t> received_bytes (0);
    std::atomic<int> recv_errors (0);
    const size_t expected_bytes =
      kConcurrentSenders * kMessagesPerSender * kPayloadSize;
    std::thread reader (drain_exact_bytes, raw_fd, expected_bytes,
                        &received_bytes, &recv_errors);

    std::atomic<int> send_errors (0);
    std::vector<std::thread> senders;
    for (int i = 0; i < kConcurrentSenders; ++i) {
        senders.push_back (std::thread (
          sender_thread_run, server, rid, static_cast<unsigned char> (0x30 + i),
          kMessagesPerSender, &send_errors));
    }

    for (size_t i = 0; i < senders.size (); ++i)
        senders[i].join ();
    reader.join ();

    TEST_ASSERT_EQUAL_INT (0, send_errors.load (std::memory_order_acquire));
    TEST_ASSERT_EQUAL_INT (0, recv_errors.load (std::memory_order_acquire));
    TEST_ASSERT_EQUAL_UINT (
      static_cast<unsigned int> (expected_bytes),
      static_cast<unsigned int> (
        received_bytes.load (std::memory_order_acquire)));

    close_raw_fd (raw_fd);
    test_context_socket_close_zero_linger (server);
#endif
}

void test_stream_send_and_detach_race_is_safe ()
{
#if defined(ZLINK_HAVE_WINDOWS)
    TEST_IGNORE_MESSAGE ("raw tcp helper unavailable on Windows");
#else
    void *server = test_context_socket (ZLINK_STREAM);
    TEST_ASSERT_NOT_NULL (server);
    configure_stream_socket (server);

    char endpoint[MAX_SOCKET_STRING];
    bind_loopback_ipv4 (server, endpoint, sizeof (endpoint));

    const int raw_fd = connect_raw_tcp (endpoint);
    TEST_ASSERT_GREATER_OR_EQUAL_INT (0, raw_fd);
    set_raw_timeout (raw_fd, 500);

    zlink_routing_id_t rid;
    establish_route (server, raw_fd, &rid, true);

    std::atomic<int> send_errors (0);
    std::thread sender (
      sender_thread_run, server, rid, static_cast<unsigned char> (0x44),
      kMessagesPerSender, &send_errors);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_stream_detach (server));
    sender.join ();

    TEST_ASSERT_EQUAL_INT (0, send_errors.load (std::memory_order_acquire));

    close_raw_fd (raw_fd);
    test_context_socket_close_zero_linger (server);
#endif
}

void test_stream_send_and_close_race_is_safe ()
{
#if defined(ZLINK_HAVE_WINDOWS)
    TEST_IGNORE_MESSAGE ("raw tcp helper unavailable on Windows");
#else
    void *server = zlink_socket (get_test_context (), ZLINK_STREAM);
    TEST_ASSERT_NOT_NULL (server);
    configure_stream_socket (server);

    char endpoint[MAX_SOCKET_STRING];
    bind_loopback_ipv4 (server, endpoint, sizeof (endpoint));

    const int raw_fd = connect_raw_tcp (endpoint);
    TEST_ASSERT_GREATER_OR_EQUAL_INT (0, raw_fd);
    set_raw_timeout (raw_fd, 200);

    zlink_routing_id_t rid;
    establish_route (server, raw_fd, &rid, false);

    std::atomic<int> send_errors (0);
    std::atomic<int> sender_done (0);
    std::thread sender ([&] () {
        std::vector<unsigned char> payload (kPayloadSize, 0x5A);
        for (;;) {
            const int rc = zlink_stream_send (
              server, &rid, &payload[0], payload.size (), 0);
            if (rc == static_cast<int> (payload.size ()))
                continue;

            if (errno == ENOTSOCK || errno == ETERM || errno == EAGAIN
                || errno == EINTR) {
                break;
            }
            send_errors.fetch_add (1, std::memory_order_release);
            break;
        }
        sender_done.store (1, std::memory_order_release);
    });

    std::this_thread::sleep_for (std::chrono::milliseconds (50));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_close (server));
    sender.join ();

    TEST_ASSERT_EQUAL_INT (0, send_errors.load (std::memory_order_acquire));
    TEST_ASSERT_EQUAL_INT (1, sender_done.load (std::memory_order_acquire));

    close_raw_fd (raw_fd);
#endif
}

void test_stream_callback_handoff_to_worker_thread_send_msg_is_safe ()
{
#if defined(ZLINK_HAVE_WINDOWS)
    TEST_IGNORE_MESSAGE ("raw tcp helper unavailable on Windows");
#else
    void *server = test_context_socket (ZLINK_STREAM);
    TEST_ASSERT_NOT_NULL (server);
    configure_stream_socket (server);

    char endpoint[MAX_SOCKET_STRING];
    bind_loopback_ipv4 (server, endpoint, sizeof (endpoint));

    const int raw_fd = connect_raw_tcp (endpoint);
    TEST_ASSERT_GREATER_OR_EQUAL_INT (0, raw_fd);
    set_raw_timeout (raw_fd, 5000);

    worker_probe_t probe;
    probe.socket = server;
    g_worker_probe = &probe;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_stream_attach_raw (server, handoff_to_worker_callback));

    std::thread worker (worker_send_msg_run, &probe);

    std::vector<unsigned char> payload (kPayloadSize, 0x61);
    TEST_ASSERT_EQUAL_INT (
      0, send_all (raw_fd, &payload[0], payload.size ()));

    {
        std::unique_lock<std::mutex> lk (probe.mutex);
        TEST_ASSERT_TRUE (probe.cv.wait_for (
          lk, std::chrono::seconds (10), [&probe]() { return probe.done; }));
    }

    std::vector<unsigned char> echoed (payload.size ());
    TEST_ASSERT_TRUE (recv_exact_bytes (raw_fd, &echoed[0], echoed.size ()));
    TEST_ASSERT_EQUAL_INT (
      0, memcmp (&payload[0], &echoed[0], payload.size ()));

    worker.join ();

    TEST_ASSERT_EQUAL_INT (static_cast<int> (payload.size ()), probe.send_rc);
    TEST_ASSERT_EQUAL_INT (0, probe.send_errno);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_stream_detach (server));
    g_worker_probe = NULL;
    close_raw_fd (raw_fd);
    test_context_socket_close_zero_linger (server);
#endif
}

int main ()
{
    setup_test_environment ();

    UNITY_BEGIN ();
    RUN_TEST (test_stream_callback_rejects_detach_and_close);
    RUN_TEST (test_stream_send_is_thread_safe_across_app_threads);
    RUN_TEST (test_stream_send_and_detach_race_is_safe);
    RUN_TEST (test_stream_send_and_close_race_is_safe);
    RUN_TEST (test_stream_callback_handoff_to_worker_thread_send_msg_is_safe);
    return UNITY_END ();
}
