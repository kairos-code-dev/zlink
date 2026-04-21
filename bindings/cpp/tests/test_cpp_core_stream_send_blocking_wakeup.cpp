#include "test_helpers.hpp"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <vector>

#if !defined(ZLINK_HAVE_WINDOWS)
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#endif

namespace {

const int kStreamHwm = 10;
const int kSendTimeoutMs = 1000;
const int kWakeupSendTimeoutMs = 5000;
const size_t kPayloadSize = 4096;
const int kRouteIdSize = 4;
const int kSocketBufBytes = 4096;
const int kProbeTimeoutMs = 250;
const int kLingerMs = 0;

template<typename SocketLike>
void configure_stream_socket (SocketLike &socket_)
{
    assert (socket_.set_option (zlink::socket_option::linger, kLingerMs) == 0);
    assert (socket_.set_option (zlink::socket_option::sndhwm, kStreamHwm) == 0);
    assert (socket_.set_option (zlink::socket_option::rcvhwm, kStreamHwm) == 0);
    assert (socket_.set_option (zlink::socket_option::sndbuf, kSocketBufBytes) == 0);
    assert (socket_.set_option (zlink::socket_option::rcvbuf, kSocketBufBytes) == 0);
    assert (socket_.set_option (zlink::socket_option::sndtimeo, kSendTimeoutMs) == 0);
}

#if defined(ZLINK_HAVE_WINDOWS)
int connect_raw_tcp (const char *)
{
    errno = EOPNOTSUPP;
    return -1;
}

int send_all (int, const unsigned char *, size_t)
{
    errno = EOPNOTSUPP;
    return -1;
}

int recv_raw (int, unsigned char *, size_t)
{
    errno = EOPNOTSUPP;
    return -1;
}

void close_raw_fd (int)
{
}

void set_raw_timeout (int, int)
{
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
    if (std::strcmp (proto, "tcp") != 0 || port <= 0 || port > 65535)
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
    std::memset (&addr, 0, sizeof (addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons (static_cast<uint16_t> (port));
    if (inet_pton (AF_INET, host, &addr.sin_addr) != 1) {
        const int err = errno;
        close (fd);
        errno = err;
        return -1;
    }

    if (connect (fd, reinterpret_cast<const struct sockaddr *> (&addr), sizeof (addr))
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
      setsockopt (fd_, SOL_SOCKET, SO_RCVBUF, &kSocketBufBytes, sizeof (kSocketBufBytes));
    assert (rcvbuf_rc == 0);

    struct timeval tv;
    tv.tv_sec = timeout_ms_ / 1000;
    tv.tv_usec = (timeout_ms_ % 1000) * 1000;
    assert (setsockopt (fd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof (tv)) == 0);
    assert (setsockopt (fd_, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof (tv)) == 0);
}
#endif

void establish_stream_route (zlink::stream_socket_t &server_,
                             int raw_fd_,
                             uint32_t *rid_)
{
    const unsigned char request = 0xA5;
    assert (send_all (raw_fd_, &request, sizeof (request)) == 0);

    unsigned char routing_id[kRouteIdSize];
    unsigned char payload[16];

    const int rid_rc = raw_recv (server_, routing_id, sizeof (routing_id));
    assert (rid_rc == kRouteIdSize);

    int more = 0;
    size_t more_size = sizeof (more);
    assert (zlink_getsockopt (server_.handle (), ZLINK_RCVMORE, &more, &more_size) == 0);
    assert (more == 1);

    const int payload_rc = raw_recv (server_, payload, sizeof (payload));
    assert (payload_rc == 1);
    assert (payload[0] == request);

    *rid_ = (static_cast<uint32_t> (routing_id[0]) << 24)
            | (static_cast<uint32_t> (routing_id[1]) << 16)
            | (static_cast<uint32_t> (routing_id[2]) << 8)
            | static_cast<uint32_t> (routing_id[3]);
}

void fill_stream_send_queue_until_hwm (zlink::stream_socket_t &server_,
                                       uint32_t rid_)
{
    std::vector<unsigned char> payload (kPayloadSize, 0x5A);
    const zlink_routing_id_t routing_id = routing_id_from_uint32 (rid_);
    int sent = 0;
    const auto stable_full_window = std::chrono::milliseconds (120);
    auto no_success_since = std::chrono::steady_clock::now ();
    const auto deadline =
      std::chrono::steady_clock::now () + std::chrono::milliseconds (5000);
    bool reached_full = false;

    for (;;) {
        const int rc = routed_raw_send (
          server_, routing_id, payload.data (), kPayloadSize,
          zlink::send_flag::dontwait);
        if (rc == static_cast<int> (kPayloadSize)) {
            ++sent;
            no_success_since = std::chrono::steady_clock::now ();
            if (std::chrono::steady_clock::now () >= deadline)
                break;
            continue;
        }

        assert (rc == -1);
        assert (zlink_errno () == EAGAIN);

        const auto now = std::chrono::steady_clock::now ();
        if (now - no_success_since >= stable_full_window) {
            reached_full = true;
            break;
        }
        if (now >= deadline)
            break;
        sleep_ms (1);
    }

    assert (sent > 0);
    assert (reached_full);
}

void test_stream_queue_reopens_after_peer_reads ()
{
#if defined(ZLINK_HAVE_WINDOWS)
    return;
#else
    zlink::context_t ctx;
    zlink::stream_socket_t server (ctx);
    configure_stream_socket (server);
    assert (server.set_option (zlink::socket_option::sndtimeo, kWakeupSendTimeoutMs) == 0);

    server.bind ("tcp://127.0.0.1:*");
    const std::string endpoint = bound_endpoint (server);

    const int raw_fd = connect_raw_tcp (endpoint.c_str ());
    assert (raw_fd >= 0);
    set_raw_timeout (raw_fd, 200);

    uint32_t rid = 0;
    establish_stream_route (server, raw_fd, &rid);
    fill_stream_send_queue_until_hwm (server, rid);

    std::vector<unsigned char> payload (kPayloadSize, 0x33);
    assert (server.set_option (zlink::socket_option::sndtimeo, kProbeTimeoutMs) == 0);
    assert (routed_raw_send (
              server, routing_id_from_uint32 (rid), payload.data (), kPayloadSize)
            == -1);
    assert (zlink_errno () == EAGAIN);

    unsigned char drain_buf[64 * 1024];
    int drained = 0;
    const int drain_target =
      static_cast<int> (kPayloadSize * ((kStreamHwm + 1) / 2 + 2));
    const auto drain_deadline =
      std::chrono::steady_clock::now () + std::chrono::milliseconds (1000);
    while (std::chrono::steady_clock::now () < drain_deadline && drained < drain_target) {
        const int n = recv_raw (raw_fd, drain_buf, sizeof (drain_buf));
        if (n > 0)
            drained += n;
    }

    bool reopened = false;
    const auto reopen_deadline =
      std::chrono::steady_clock::now () + std::chrono::milliseconds (1500);
    while (std::chrono::steady_clock::now () < reopen_deadline) {
        const int send_rc = routed_raw_send (
          server, routing_id_from_uint32 (rid), payload.data (), kPayloadSize,
          zlink::send_flag::dontwait);
        if (send_rc == static_cast<int> (kPayloadSize)) {
            reopened = true;
            break;
        }

        assert (zlink_errno () == EAGAIN);
        (void) recv_raw (raw_fd, drain_buf, sizeof (drain_buf));
    }

    assert (reopened);
    close_raw_fd (raw_fd);
#endif
}

void test_stream_blocking_send_times_out_without_peer_reads ()
{
#if defined(ZLINK_HAVE_WINDOWS)
    return;
#else
    zlink::context_t ctx;
    zlink::stream_socket_t server (ctx);
    configure_stream_socket (server);

    server.bind ("tcp://127.0.0.1:*");
    const std::string endpoint = bound_endpoint (server);

    const int raw_fd = connect_raw_tcp (endpoint.c_str ());
    assert (raw_fd >= 0);
    set_raw_timeout (raw_fd, 200);

    uint32_t rid = 0;
    establish_stream_route (server, raw_fd, &rid);
    fill_stream_send_queue_until_hwm (server, rid);

    std::vector<unsigned char> payload (kPayloadSize, 0x44);
    void *stopwatch = zlink_stopwatch_start ();
    const int send_rc = routed_raw_send (
      server, routing_id_from_uint32 (rid), payload.data (), kPayloadSize);
    const unsigned int elapsed_ms = zlink_stopwatch_stop (stopwatch) / 1000;

    assert (send_rc == -1);
    assert (zlink_errno () == EAGAIN);
    assert (elapsed_ms >= static_cast<unsigned int> (kSendTimeoutMs - 150));

    close_raw_fd (raw_fd);
#endif
}

} // namespace

int main ()
{
    test_stream_queue_reopens_after_peer_reads ();
    test_stream_blocking_send_times_out_without_peer_reads ();
    return 0;
}
