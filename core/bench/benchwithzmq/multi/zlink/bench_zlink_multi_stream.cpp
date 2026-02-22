#include "../common/bench_common_zlink.hpp"
#include "../common/bench_common_multi.hpp"
#include "../common/stream_frame.hpp"
#include <zlink.h>
#include <algorithm>
#include <cerrno>
#include <climits>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <condition_variable>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
using socket_t = SOCKET;
static const socket_t INVALID_SOCKET_FD = INVALID_SOCKET;
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>
using socket_t = int;
static const socket_t INVALID_SOCKET_FD = -1;
#endif

#ifndef ZLINK_STREAM
#define ZLINK_STREAM 11
#endif

#ifndef ZLINK_TCP_NODELAY
#define ZLINK_TCP_NODELAY 26
#endif

namespace {

static const unsigned char STREAM_EVENT_CONNECT = 0x01;
static const unsigned char STREAM_EVENT_DISCONNECT = 0x00;

bool is_stream_event_payload (const char *data, size_t size);

typedef std::unordered_map<std::string, stream_buffer_t> stream_stash_map_t;

bool extract_single_framed_payload (const char *data,
                                    size_t size,
                                    std::vector<char> *payload_out)
{
    if (!data || !payload_out || size < FRAME_PREFIX_SIZE)
        return false;

    uint32_t net_len = 0;
    std::memcpy (&net_len, data, FRAME_PREFIX_SIZE);
    const size_t frame_len = static_cast<size_t> (ntohl (net_len));
    if (frame_len > MAX_STREAM_FRAME_SIZE)
        return false;
    if (FRAME_PREFIX_SIZE + frame_len != size)
        return false;

    payload_out->assign (frame_len, 0);
    if (frame_len > 0) {
        std::memcpy (
          payload_out->data (), data + FRAME_PREFIX_SIZE, frame_len);
    }
    return true;
}

void normalize_stream_reply_frame (const char *data,
                                   size_t size,
                                   std::vector<char> *framed_out)
{
    if (!framed_out)
        return;

    std::vector<char> payload;
    if (extract_single_framed_payload (data, size, &payload)) {
        framed_out->assign (data, data + size);
        return;
    }

    payload.assign (size, 0);
    if (size > 0 && data)
        std::memcpy (payload.data (), data, size);
    stream_build_framed_payload (payload, framed_out);
}

struct stream_dispatch_packet_t {
    std::string routing_id;
    std::vector<char> payload;
};

struct stream_len32be_dispatch_t {
    void *socket;
    std::mutex lock;
    std::condition_variable cv;
    std::deque<stream_dispatch_packet_t> packets;
    std::atomic<bool> running;

    stream_len32be_dispatch_t () : socket (NULL), running (false) {}
};

static stream_len32be_dispatch_t *g_stream_dispatch = NULL;

enum recv_sender_frame_status_t
{
    recv_sender_frame_ok = 0,
    recv_sender_frame_invalid_args,
    recv_sender_frame_timeout,
    recv_sender_frame_poll_error,
    recv_sender_frame_peer_closed,
    recv_sender_frame_recv_error,
    recv_sender_frame_invalid_prefix
};

const char *recv_sender_frame_status_name (recv_sender_frame_status_t status)
{
    switch (status) {
    case recv_sender_frame_ok:
        return "ok";
    case recv_sender_frame_invalid_args:
        return "invalid_args";
    case recv_sender_frame_timeout:
        return "timeout";
    case recv_sender_frame_poll_error:
        return "poll_error";
    case recv_sender_frame_peer_closed:
        return "peer_closed";
    case recv_sender_frame_recv_error:
        return "recv_error";
    case recv_sender_frame_invalid_prefix:
        return "invalid_prefix";
    default:
        return "unknown";
    }
}

int on_stream_len32be_packets (const zlink_routing_id_t *rid_,
                               zlink_msg_t *msgs_,
                               size_t msg_count_)
{
    stream_len32be_dispatch_t *dispatch = g_stream_dispatch;
    if (!dispatch || !rid_ || !msgs_ || msg_count_ == 0)
        return 0;

    for (size_t i = 0; i < msg_count_; ++i) {
        const char *payload_data =
          static_cast<const char *> (zlink_msg_data (&msgs_[i]));
        const size_t payload_size = zlink_msg_size (&msgs_[i]);
        if (payload_size == 1 && payload_data
            && (static_cast<unsigned char> (payload_data[0]) == STREAM_EVENT_CONNECT
                || static_cast<unsigned char> (payload_data[0])
                     == STREAM_EVENT_DISCONNECT)) {
            continue;
        }
        if (!payload_data && payload_size > 0)
            continue;
        const int rc = zlink_stream_send (
          dispatch->socket,
          rid_,
          payload_size > 0
            ? reinterpret_cast<const unsigned char *> (payload_data)
            : NULL,
          payload_size,
          ZLINK_DONTWAIT);
        if (rc < 0 && zlink_errno () != EAGAIN && zlink_errno () != EINTR) {
            return 1;
        }
    }
    return dispatch->running.load (std::memory_order_acquire) ? 0 : 1;
}

bool start_stream_len32be_dispatch (void *socket_,
                                    stream_len32be_dispatch_t &dispatch)
{
    dispatch.socket = socket_;
    dispatch.running.store (true, std::memory_order_release);
    g_stream_dispatch = &dispatch;
    if (zlink_stream_start (
          socket_, &on_stream_len32be_packets, ZLINK_STREAM_DISPATCH_LEN32BE)
        != 0) {
        dispatch.running.store (false, std::memory_order_release);
        dispatch.socket = NULL;
        if (g_stream_dispatch == &dispatch)
            g_stream_dispatch = NULL;
        return false;
    }
    return true;
}

void stop_stream_len32be_dispatch (stream_len32be_dispatch_t &dispatch)
{
    if (!dispatch.running.exchange (false, std::memory_order_acq_rel))
        return;
    if (dispatch.socket)
        (void) zlink_stream_stop (dispatch.socket);
    {
        std::lock_guard<std::mutex> guard (dispatch.lock);
        dispatch.packets.clear ();
    }
    dispatch.cv.notify_all ();
    if (g_stream_dispatch == &dispatch)
        g_stream_dispatch = NULL;
    dispatch.socket = NULL;
}

void clear_stream_len32be_dispatch_packets (stream_len32be_dispatch_t &dispatch)
{
    std::lock_guard<std::mutex> guard (dispatch.lock);
    dispatch.packets.clear ();
}

bool pop_stream_len32be_packet (void *socket_,
                                int timeout_ms,
                                bool dontwait,
                                stream_dispatch_packet_t &packet_out)
{
    stream_len32be_dispatch_t *dispatch = g_stream_dispatch;
    if (!dispatch || dispatch->socket != socket_
        || !dispatch->running.load (std::memory_order_acquire)) {
        return false;
    }

    const int wait_ms = dontwait ? 0 : std::max (0, timeout_ms);
    std::unique_lock<std::mutex> guard (dispatch->lock);
    const auto ready = [&] {
        return !dispatch->packets.empty ()
               || !dispatch->running.load (std::memory_order_acquire);
    };

    if (wait_ms == 0) {
        if (!ready ())
            return false;
    } else {
        if (!dispatch->cv.wait_for (
              guard, std::chrono::milliseconds (wait_ms), ready)) {
            return false;
        }
    }
    if (dispatch->packets.empty ())
        return false;

    packet_out = std::move (dispatch->packets.front ());
    dispatch->packets.pop_front ();
    return true;
}

struct tcp_sender_state_t {
    socket_t fd;
    std::vector<char> frame;
    size_t sent_bytes;
    stream_buffer_t recv_stash;

    tcp_sender_state_t () : fd (INVALID_SOCKET_FD), sent_bytes (0) {}
    bool valid () const { return fd != INVALID_SOCKET_FD; }
};

struct stream_client_poller_t {
    std::vector<zlink_pollitem_t> items;
    std::vector<size_t> sender_indices;
    size_t cursor;

    stream_client_poller_t () : cursor (0) {}

    void reset ()
    {
        items.clear ();
        sender_indices.clear ();
        cursor = 0;
    }

    bool rebuild (const std::vector<tcp_sender_state_t> &senders)
    {
        reset ();
        items.reserve (senders.size ());
        sender_indices.reserve (senders.size ());

        for (size_t i = 0; i < senders.size (); ++i) {
            if (!senders[i].valid ())
                continue;

            zlink_pollitem_t item;
            std::memset (&item, 0, sizeof (item));
            item.socket = NULL;
            item.fd = senders[i].fd;
            item.events = ZLINK_POLLIN;
            item.revents = 0;
            items.push_back (item);
            sender_indices.push_back (i);
        }

        return !items.empty ();
    }

    bool empty () const { return items.empty (); }
};

struct stream_pending_reply_t {
    std::string routing_id;
    std::vector<char> payload;
    bool id_sent;

    stream_pending_reply_t () : id_sent (false) {}
};

typedef std::deque<stream_pending_reply_t> stream_reply_queue_t;

#ifdef _WIN32
void ensure_winsock_initialized ()
{
    static bool initialized = false;
    if (initialized)
        return;

    WSADATA wsa;
    if (WSAStartup (MAKEWORD (2, 2), &wsa) == 0)
        initialized = true;
}
#endif

void close_socket_fd (socket_t fd)
{
    if (fd == INVALID_SOCKET_FD)
        return;
#ifdef _WIN32
    closesocket (fd);
#else
    close (fd);
#endif
}

bool parse_tcp_endpoint (const std::string &endpoint,
                         std::string &host_out,
                         int &port_out)
{
    host_out.clear ();
    port_out = 0;
    if (endpoint.empty ())
        return false;

    std::string host_port = endpoint;
    const std::string prefix = "tcp://";
    if (host_port.find (prefix) == 0)
        host_port = host_port.substr (prefix.size ());

    const size_t colon = host_port.find_last_of (':');
    if (colon == std::string::npos)
        return false;

    host_out = host_port.substr (0, colon);
    if (!host_out.empty () && host_out.front () == '[' && host_out.back () == ']')
        host_out = host_out.substr (1, host_out.size () - 2);

    port_out = std::atoi (host_port.substr (colon + 1).c_str ());
    return !host_out.empty () && port_out > 0;
}

bool set_socket_nonblocking (socket_t fd)
{
#ifdef _WIN32
    u_long mode = 1;
    return ioctlsocket (fd, FIONBIO, &mode) == 0;
#else
    const int flags = fcntl (fd, F_GETFL, 0);
    if (flags < 0)
        return false;
    return fcntl (fd, F_SETFL, flags | O_NONBLOCK) == 0;
#endif
}

int resolve_stream_socket_int_env (const char *env_name,
                                   int default_value,
                                   int min_value)
{
    if (!env_name || !*env_name)
        return default_value;

    const char *value = std::getenv (env_name);
    if (!value || !*value)
        return default_value;

    errno = 0;
    char *end = NULL;
    const long parsed = std::strtol (value, &end, 10);
    if (errno != 0 || end == value)
        return default_value;

    if (parsed < static_cast<long> (min_value))
        return min_value;
    if (parsed > static_cast<long> (INT_MAX))
        return INT_MAX;
    return static_cast<int> (parsed);
}

bool set_fd_int_option (socket_t fd, int level, int option, int value)
{
#ifdef _WIN32
    return setsockopt (
             fd, level, option, reinterpret_cast<const char *> (&value), sizeof (value))
           == 0;
#else
    return setsockopt (fd, level, option, &value, sizeof (value)) == 0;
#endif
}

int resolve_stream_tcp_nodelay (bool force_enable)
{
    if (force_enable)
        return 1;
    return resolve_stream_socket_int_env ("BENCH_STREAM_TCP_NODELAY", 1, 0);
}

void apply_stream_tcp_fd_tuning (socket_t fd, bool force_nodelay)
{
    const int nodelay = resolve_stream_tcp_nodelay (force_nodelay);
    const int sndbuf =
      resolve_stream_socket_int_env ("BENCH_STREAM_SNDBUF", 262144, 0);
    const int rcvbuf =
      resolve_stream_socket_int_env ("BENCH_STREAM_RCVBUF", 262144, 0);

#ifdef TCP_NODELAY
    if (nodelay > 0)
        (void)set_fd_int_option (fd, IPPROTO_TCP, TCP_NODELAY, 1);
#endif
    if (sndbuf > 0)
        (void)set_fd_int_option (fd, SOL_SOCKET, SO_SNDBUF, sndbuf);
    if (rcvbuf > 0)
        (void)set_fd_int_option (fd, SOL_SOCKET, SO_RCVBUF, rcvbuf);
}

void apply_stream_server_tuning (void *server, bool force_nodelay)
{
    const int backlog =
      resolve_stream_socket_int_env ("BENCH_STREAM_BACKLOG", 32768, 1);
    const int sndbuf =
      resolve_stream_socket_int_env ("BENCH_STREAM_SNDBUF", 262144, 0);
    const int rcvbuf =
      resolve_stream_socket_int_env ("BENCH_STREAM_RCVBUF", 262144, 0);
    const int nodelay = resolve_stream_tcp_nodelay (force_nodelay);

    set_sockopt_int (server, ZLINK_BACKLOG, backlog, "ZLINK_BACKLOG");
    if (sndbuf > 0)
        set_sockopt_int (server, ZLINK_SNDBUF, sndbuf, "ZLINK_SNDBUF");
    if (rcvbuf > 0)
        set_sockopt_int (server, ZLINK_RCVBUF, rcvbuf, "ZLINK_RCVBUF");
    if (nodelay > 0)
        set_sockopt_int (
          server, ZLINK_TCP_NODELAY, nodelay, "ZLINK_TCP_NODELAY");
}

socket_t connect_tcp_socket (const std::string &endpoint, bool force_nodelay)
{
#ifdef _WIN32
    ensure_winsock_initialized ();
#endif

    std::string host;
    int port = 0;
    if (!parse_tcp_endpoint (endpoint, host, port))
        return INVALID_SOCKET_FD;

    socket_t fd = socket (AF_INET, SOCK_STREAM, 0);
    if (fd == INVALID_SOCKET_FD)
        return INVALID_SOCKET_FD;
    apply_stream_tcp_fd_tuning (fd, force_nodelay);

    struct sockaddr_in addr;
    std::memset (&addr, 0, sizeof (addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons (static_cast<uint16_t> (port));

#ifdef _WIN32
    addr.sin_addr.s_addr = inet_addr (host.c_str ());
    if (addr.sin_addr.s_addr == INADDR_NONE) {
        close_socket_fd (fd);
        return INVALID_SOCKET_FD;
    }
#else
    if (inet_pton (AF_INET, host.c_str (), &addr.sin_addr) != 1) {
        close_socket_fd (fd);
        return INVALID_SOCKET_FD;
    }
#endif

    if (connect (fd, reinterpret_cast<struct sockaddr *> (&addr), sizeof (addr))
        != 0) {
        close_socket_fd (fd);
        return INVALID_SOCKET_FD;
    }

    return fd;
}

bool is_would_block_error ()
{
#ifdef _WIN32
    const int err = WSAGetLastError ();
    return err == WSAEWOULDBLOCK || err == WSAEINPROGRESS || err == WSAEINTR;
#else
    return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR;
#endif
}

int send_socket_data (socket_t fd, const char *buf, size_t len)
{
    if (len == 0)
        return 0;

#ifdef _WIN32
    const size_t cap = static_cast<size_t> (INT_MAX);
    const int send_len =
      static_cast<int> (std::min<size_t> (len, cap));
    return send (fd, buf, send_len, 0);
#else
    int flags = 0;
#ifdef MSG_NOSIGNAL
    flags |= MSG_NOSIGNAL;
#endif
    return static_cast<int> (send (fd, buf, len, flags));
#endif
}

bool write_all (socket_t fd, const void *buf, size_t len)
{
    const char *cur = static_cast<const char *> (buf);
    size_t left = len;
    while (left > 0) {
        const int n = send_socket_data (fd, cur, left);
        if (n <= 0) {
            if (is_would_block_error ())
                continue;
            return false;
        }
        cur += n;
        left -= static_cast<size_t> (n);
    }
    return true;
}

bool read_all (socket_t fd, void *buf, size_t len)
{
    char *cur = static_cast<char *> (buf);
    size_t left = len;
    while (left > 0) {
#ifdef _WIN32
        const int n = recv (fd, cur, static_cast<int> (left), 0);
#else
        const int n = static_cast<int> (recv (fd, cur, left, 0));
#endif
        if (n <= 0) {
            if (is_would_block_error ())
                continue;
            return false;
        }
        cur += n;
        left -= static_cast<size_t> (n);
    }
    return true;
}

bool send_raw_framed (socket_t fd, const std::vector<char> &payload)
{
    const uint32_t net_len = htonl (static_cast<uint32_t> (payload.size ()));
    if (!write_all (fd, &net_len, sizeof (net_len)))
        return false;
    if (payload.empty ())
        return true;
    return write_all (fd, &payload[0], payload.size ());
}

bool recv_raw_framed (socket_t fd, std::vector<char> *payload_out)
{
    uint32_t net_len = 0;
    if (!read_all (fd, &net_len, sizeof (net_len)))
        return false;

    const size_t len = static_cast<size_t> (ntohl (net_len));
    if (len > MAX_STREAM_FRAME_SIZE)
        return false;

    payload_out->assign (len, 0);
    if (len == 0)
        return true;
    return read_all (fd, &(*payload_out)[0], len);
}

void close_sender (tcp_sender_state_t &sender)
{
    if (sender.valid ())
        close_socket_fd (sender.fd);
    sender.fd = INVALID_SOCKET_FD;
    sender.frame.clear ();
    sender.sent_bytes = 0;
    sender.recv_stash.reset ();
}

bool setup_sender (tcp_sender_state_t &sender, const std::string &endpoint)
{
    if (sender.valid ()) {
        //  Keep connection alive across message-size rounds, but reset any
        //  partially staged frame from a previous round.
        sender.frame.clear ();
        sender.sent_bytes = 0;
        sender.recv_stash.reset ();
        return true;
    }

    socket_t fd = connect_tcp_socket (endpoint, true);
    if (fd == INVALID_SOCKET_FD)
        return false;
    if (!set_socket_nonblocking (fd)) {
        close_socket_fd (fd);
        return false;
    }

    sender.fd = fd;
    sender.frame.clear ();
    sender.sent_bytes = 0;
    sender.recv_stash.reset ();
    return true;
}

multi_send_result_t send_sender_nonblocking (tcp_sender_state_t &sender,
                                             const std::vector<char> &payload)
{
    if (!sender.valid ())
        return multi_send_error;

    if (sender.frame.empty ()) {
        stream_build_framed_payload (payload, &sender.frame);
        sender.sent_bytes = 0;
    }

    const size_t left = sender.frame.size () - sender.sent_bytes;
    const int n = send_socket_data (
      sender.fd, &sender.frame[0] + sender.sent_bytes, left);
    if (n > 0) {
        sender.sent_bytes += static_cast<size_t> (n);
        if (sender.sent_bytes >= sender.frame.size ()) {
            sender.frame.clear ();
            sender.sent_bytes = 0;
            return multi_send_ok;
        }
        return multi_send_would_block;
    }

    if (n == 0)
        return multi_send_error;
    if (is_would_block_error ())
        return multi_send_would_block;
    return multi_send_error;
}

long resolve_stream_long_env (const char *env_name, long default_value, long min_value)
{
    if (!env_name || !*env_name)
        return default_value;

    const char *value = std::getenv (env_name);
    if (!value || !*value)
        return default_value;

    errno = 0;
    char *end = NULL;
    const long parsed = std::strtol (value, &end, 10);
    if (errno != 0 || end == value)
        return default_value;

    if (parsed < min_value)
        return min_value;
    return parsed;
}

long compute_stream_window_msgs (const multi_bench_settings_t &settings,
                                 size_t current_size)
{
    const long configured =
      static_cast<long> (std::max<size_t> (1, settings.clients))
      * static_cast<long> (std::max (1, settings.inflight));
    const long hard_cap = resolve_stream_long_env (
      "BENCH_MULTI_STREAM_MAX_INFLIGHT_MSGS", LONG_MAX / 4, 1);
    const long max_bytes = resolve_stream_long_env (
      "BENCH_MULTI_STREAM_MAX_INFLIGHT_BYTES", 256L * 1024L * 1024L, 1);

    const size_t safe_size = std::max<size_t> (1, current_size);
    const long by_bytes =
      static_cast<long> (
        std::max<size_t> (1, static_cast<size_t> (max_bytes) / safe_size));

    return std::max<long> (1, std::min<long> (configured, std::min<long> (hard_cap, by_bytes)));
}

bool wait_stream_ready_count (connect_monitor_t &monitor,
                              size_t expected_ready,
                              int timeout_ms,
                              size_t *observed_ready_out)
{
    if (expected_ready == 0) {
        if (observed_ready_out)
            *observed_ready_out = 0;
        return true;
    }

    size_t ready_events = 0;
    size_t observed_ready = 0;
    const auto deadline =
      std::chrono::steady_clock::now ()
      + std::chrono::milliseconds (std::max (0, timeout_ms));

    while (std::chrono::steady_clock::now () < deadline) {
        ready_events +=
          static_cast<size_t> (std::max (0, poll_connect_ready_count (monitor)));
        observed_ready = std::max<size_t> (observed_ready, ready_events);
        if (observed_ready >= expected_ready) {
            if (observed_ready_out)
                *observed_ready_out = observed_ready;
            return true;
        }
        std::this_thread::sleep_for (std::chrono::milliseconds (1));
    }

    if (observed_ready_out)
        *observed_ready_out = observed_ready;
    return false;
}

int flush_stream_reply_queue (void *server,
                              stream_reply_queue_t &pending_replies,
                              int send_budget)
{
    if (send_budget <= 0 || pending_replies.empty ())
        return 0;

    int sent = 0;
    while (sent < send_budget && !pending_replies.empty ()) {
        stream_pending_reply_t &reply = pending_replies.front ();

        if (!reply.id_sent) {
            const char *rid_ptr =
              reply.routing_id.empty () ? NULL : &reply.routing_id[0];
            const int rid_rc =
              zlink_send (server, rid_ptr, reply.routing_id.size (),
                          ZLINK_SNDMORE | ZLINK_DONTWAIT);
            if (rid_rc < 0) {
                if (zlink_errno () == EAGAIN || zlink_errno () == EINTR)
                    break;
                return -1;
            }
            reply.id_sent = true;
        }

        const char *payload_ptr =
          reply.payload.empty () ? NULL : &reply.payload[0];
        const int payload_rc = zlink_send (
          server, payload_ptr, reply.payload.size (), ZLINK_DONTWAIT);
        if (payload_rc < 0) {
            if (zlink_errno () == EAGAIN || zlink_errno () == EINTR)
                break;
            return -1;
        }

        pending_replies.pop_front ();
        ++sent;
    }

    return sent;
}

int enqueue_stream_payload_chunks (void *server,
                                  stream_reply_queue_t &pending_replies,
                                  int relay_budget,
                                  long poll_timeout_ms,
                                  size_t max_pending)
{
    if (relay_budget <= 0 || max_pending == 0 || pending_replies.size () >= max_pending)
        return 0;

    stream_len32be_dispatch_t *dispatch = g_stream_dispatch;
    const bool dispatch_active =
      dispatch && dispatch->socket == server
      && dispatch->running.load (std::memory_order_acquire);
    if (dispatch_active) {
        stream_dispatch_packet_t dispatch_packet;
        if (!pop_stream_len32be_packet (
              server, static_cast<int> (poll_timeout_ms), false, dispatch_packet)) {
            return 0;
        }

        int enqueued = 0;
        auto push_reply = [&] (const stream_dispatch_packet_t &packet_) {
            if (packet_.routing_id.empty ())
                return;
            stream_pending_reply_t reply;
            reply.routing_id = packet_.routing_id;
            stream_build_framed_payload (packet_.payload, &reply.payload);
            pending_replies.push_back (reply);
            ++enqueued;
        };

        push_reply (dispatch_packet);
        while (enqueued < relay_budget && pending_replies.size () < max_pending) {
            stream_dispatch_packet_t next_packet;
            if (!pop_stream_len32be_packet (server, 0, true, next_packet))
                break;
            push_reply (next_packet);
        }
        return enqueued;
    }

    zlink_pollitem_t item[] = {{server, 0, ZLINK_POLLIN, 0}};
    const int prc = zlink_poll (item, 1, poll_timeout_ms);
    if (prc < 0)
        return zlink_errno () == EINTR ? 0 : -1;
    if (prc == 0 || (item[0].revents & ZLINK_POLLIN) == 0)
        return 0;

    int enqueued = 0;
    while (enqueued < relay_budget && pending_replies.size () < max_pending) {
        const int flags = enqueued == 0 ? 0 : ZLINK_DONTWAIT;

        zlink_msg_t id_frame;
        zlink_msg_t payload_frame;
        zlink_msg_init (&id_frame);
        zlink_msg_init (&payload_frame);

        const int id_rc = zlink_msg_recv (&id_frame, server, flags);
        if (id_rc < 0) {
            zlink_msg_close (&id_frame);
            zlink_msg_close (&payload_frame);
            if (enqueued > 0
                && (zlink_errno () == EAGAIN || zlink_errno () == EINTR))
                break;
            if (enqueued == 0 && zlink_errno () == EINTR)
                return 0;
            return -1;
        }

        const int data_rc = zlink_msg_recv (&payload_frame, server, flags);
        if (data_rc < 0) {
            zlink_msg_close (&id_frame);
            zlink_msg_close (&payload_frame);
            if (enqueued > 0
                && (zlink_errno () == EAGAIN || zlink_errno () == EINTR))
                break;
            if (enqueued == 0 && zlink_errno () == EINTR)
                return 0;
            return -1;
        }

        const char *payload_data =
          static_cast<const char *> (zlink_msg_data (&payload_frame));
        const size_t payload_size = zlink_msg_size (&payload_frame);

        if (payload_size > 0
            && payload_data
            && !is_stream_event_payload (payload_data, payload_size)) {
            const char *id_data =
              static_cast<const char *> (zlink_msg_data (&id_frame));
            const size_t id_size = zlink_msg_size (&id_frame);
            if (id_data && id_size > 0) {
                stream_pending_reply_t reply;
                reply.routing_id.assign (id_data, id_size);
                normalize_stream_reply_frame (
                  payload_data, payload_size, &reply.payload);
                pending_replies.push_back (reply);
                ++enqueued;
            }
        }

        zlink_msg_close (&id_frame);
        zlink_msg_close (&payload_frame);
    }

    return enqueued;
}

int decode_sender_frames_with_expected_size (stream_buffer_t &stash,
                                             int max_frames,
                                             size_t expected_payload_size)
{
    if (max_frames <= 0)
        return 0;
    if (expected_payload_size == 0)
        return stream_decode_available_frames (stash, max_frames);

    int completed = 0;
    while (completed < max_frames) {
        if (stash.available () < FRAME_PREFIX_SIZE)
            break;

        uint32_t net_len = 0;
        std::memcpy (&net_len, &stash.data[stash.offset], FRAME_PREFIX_SIZE);
        const size_t frame_len = static_cast<size_t> (ntohl (net_len));
        if (frame_len > MAX_STREAM_FRAME_SIZE) {
            stash.reset ();
            return -1;
        }

        const size_t required = FRAME_PREFIX_SIZE + frame_len;
        if (stash.available () < required)
            break;

        stash.offset += required;
        if (frame_len != expected_payload_size)
            continue;
        ++completed;
    }

    stash.compact ();
    return completed;
}

int recv_sender_echoes_nonblocking (tcp_sender_state_t &sender,
                                    int max_frames,
                                    size_t expected_payload_size)
{
    if (!sender.valid () || max_frames <= 0)
        return 0;

    int completed = 0;
    char buf[64 * 1024];
    for (int i = 0; i < 8 && completed < max_frames; ++i) {
#ifdef _WIN32
        const int n = recv (sender.fd, buf, static_cast<int> (sizeof (buf)), 0);
#else
        const int n = static_cast<int> (recv (sender.fd, buf, sizeof (buf), 0));
#endif
        if (n > 0) {
            sender.recv_stash.append (buf, static_cast<size_t> (n));
            const int decoded = decode_sender_frames_with_expected_size (
              sender.recv_stash, max_frames - completed, expected_payload_size);
            if (decoded < 0)
                return -1;
            completed += decoded;
            continue;
        }

        if (n == 0)
            return -1;
        if (is_would_block_error ())
            break;
        return -1;
    }

    return completed;
}

void drain_sender_echoes (tcp_sender_state_t &sender)
{
    if (!sender.valid ())
        return;

    (void) stream_decode_available_frames (sender.recv_stash, INT_MAX);
    for (int i = 0; i < 64; ++i) {
        const int drained = recv_sender_echoes_nonblocking (sender, 4096, 0);
        if (drained <= 0)
            break;
    }
}

bool has_invalid_frame_prefix (const stream_buffer_t &stash,
                               size_t *frame_len_out)
{
    if (frame_len_out)
        *frame_len_out = 0;
    if (stash.available () < FRAME_PREFIX_SIZE)
        return false;

    uint32_t net_len = 0;
    std::memcpy (&net_len, &stash.data[stash.offset], FRAME_PREFIX_SIZE);
    const size_t frame_len = static_cast<size_t> (ntohl (net_len));
    if (frame_len_out)
        *frame_len_out = frame_len;
    return frame_len > MAX_STREAM_FRAME_SIZE;
}

recv_sender_frame_status_t recv_sender_framed_payload (
  tcp_sender_state_t &sender,
  int timeout_ms,
  std::vector<char> *payload_out,
  size_t *invalid_prefix_len_out)
{
    if (invalid_prefix_len_out)
        *invalid_prefix_len_out = 0;
    if (!payload_out)
        return recv_sender_frame_invalid_args;

    payload_out->clear ();
    if (!sender.valid ())
        return recv_sender_frame_invalid_args;

    size_t invalid_len = 0;
    if (has_invalid_frame_prefix (sender.recv_stash, &invalid_len)) {
        if (invalid_prefix_len_out)
            *invalid_prefix_len_out = invalid_len;
        sender.recv_stash.reset ();
        return recv_sender_frame_invalid_prefix;
    }

    if (stream_decode_one_frame (sender.recv_stash, payload_out))
        return recv_sender_frame_ok;

    const auto deadline =
      std::chrono::steady_clock::now ()
      + std::chrono::milliseconds (std::max (0, timeout_ms));
    char buf[64 * 1024];
    while (std::chrono::steady_clock::now () < deadline) {
        const auto now = std::chrono::steady_clock::now ();
        const long remain_ms =
          std::chrono::duration_cast<std::chrono::milliseconds> (deadline - now)
            .count ();

        zlink_pollitem_t item;
        std::memset (&item, 0, sizeof (item));
        item.socket = NULL;
        item.fd = sender.fd;
        item.events = ZLINK_POLLIN;
        item.revents = 0;

        const int prc = zlink_poll (&item, 1, remain_ms > 0 ? remain_ms : 0);
        if (prc < 0) {
            if (errno == EINTR)
                continue;
            return recv_sender_frame_poll_error;
        }
        if (prc == 0 || (item.revents & ZLINK_POLLIN) == 0)
            continue;

#ifdef _WIN32
        const int n = recv (sender.fd, buf, static_cast<int> (sizeof (buf)), 0);
#else
        const int n = static_cast<int> (recv (sender.fd, buf, sizeof (buf), 0));
#endif
        if (n > 0) {
            sender.recv_stash.append (buf, static_cast<size_t> (n));
            if (has_invalid_frame_prefix (sender.recv_stash, &invalid_len)) {
                if (invalid_prefix_len_out)
                    *invalid_prefix_len_out = invalid_len;
                sender.recv_stash.reset ();
                return recv_sender_frame_invalid_prefix;
            }
            if (stream_decode_one_frame (sender.recv_stash, payload_out))
                return recv_sender_frame_ok;
            continue;
        }

        if (n == 0)
            return recv_sender_frame_peer_closed;
        if (is_would_block_error ())
            continue;
        return recv_sender_frame_recv_error;
    }

    return recv_sender_frame_timeout;
}

int poll_sender_ready (stream_client_poller_t &poller, long timeout_ms)
{
    if (poller.empty ())
        return 0;

    for (size_t i = 0; i < poller.items.size (); ++i)
        poller.items[i].revents = 0;

    const int rc = zlink_poll (&poller.items[0],
                               static_cast<int> (poller.items.size ()),
                               timeout_ms);
    if (rc < 0)
        return errno == EINTR ? 0 : -1;
    return rc;
}

int recv_batch_client_echoes (std::vector<tcp_sender_state_t> &senders,
                              stream_client_poller_t &poller,
                              int recv_batch,
                              long poll_timeout_ms,
                              size_t expected_payload_size)
{
    if (recv_batch <= 0 || senders.empty ())
        return 0;
    if (poller.empty () && !poller.rebuild (senders))
        return -1;

    const int ready = poll_sender_ready (poller, poll_timeout_ms);
    if (ready < 0)
        return -1;
    if (ready == 0)
        return 0;

    const size_t ready_size = poller.items.size ();
    if (ready_size == 0)
        return 0;

    const size_t start = poller.cursor % ready_size;
    size_t scanned = 0;
    int received = 0;
    const int per_sender_limit = std::max (1, recv_batch);
    while (scanned < ready_size) {
        const size_t idx = (start + scanned) % ready_size;
        const short revents = poller.items[idx].revents;
        if ((revents & ZLINK_POLLERR) != 0) {
            ++scanned;
            continue;
        }
        if ((revents & ZLINK_POLLIN) == 0) {
            ++scanned;
            continue;
        }

        const size_t sender_idx = poller.sender_indices[idx];
        if (sender_idx >= senders.size ())
            return -1;

        const int count = recv_sender_echoes_nonblocking (
          senders[sender_idx], per_sender_limit, expected_payload_size);
        if (count < 0)
            return -1;
        received += count;
        ++scanned;
        if (received >= recv_batch)
            break;
    }

    poller.cursor = ready_size == 0
                      ? 0
                      : (start + std::max<size_t> (1, scanned)) % ready_size;
    return received;
}

int recv_batch_stream_echoes (std::vector<tcp_sender_state_t> &senders,
                              stream_client_poller_t &poller,
                              int recv_batch,
                              long poll_timeout_ms,
                              size_t expected_payload_size)
{
    return recv_batch_client_echoes (
      senders, poller, recv_batch, poll_timeout_ms, expected_payload_size);
}

bool is_stream_event_payload (const char *data, size_t size)
{
    return size == 1
           && (static_cast<unsigned char> (data[0]) == STREAM_EVENT_CONNECT
               || static_cast<unsigned char> (data[0])
                    == STREAM_EVENT_DISCONNECT);
}

bool recv_one_stream_frame (void *server,
                            stream_stash_map_t &stashes,
                            int timeout_ms,
                            std::string &routing_id_out,
                            std::vector<char> &payload_out)
{
    const auto deadline =
      std::chrono::steady_clock::now ()
      + std::chrono::milliseconds (std::max (0, timeout_ms));

    while (std::chrono::steady_clock::now () < deadline) {
        const auto now = std::chrono::steady_clock::now ();
        const long remain_ms =
          std::chrono::duration_cast<std::chrono::milliseconds> (deadline - now)
            .count ();
        zlink_pollitem_t item;
        std::memset (&item, 0, sizeof (item));
        item.socket = server;
        item.events = ZLINK_POLLIN;
        item.revents = 0;
        const int prc = zlink_poll (&item, 1, remain_ms > 0 ? remain_ms : 0);
        if (prc < 0) {
            if (zlink_errno () == EINTR)
                continue;
            return false;
        }
        if (prc == 0 || (item.revents & ZLINK_POLLIN) == 0)
            continue;

        while (true) {
            zlink_msg_t id_frame;
            zlink_msg_t payload_frame;
            zlink_msg_init (&id_frame);
            zlink_msg_init (&payload_frame);

            const int id_rc = zlink_msg_recv (&id_frame, server, ZLINK_DONTWAIT);
            if (id_rc < 0) {
                zlink_msg_close (&id_frame);
                zlink_msg_close (&payload_frame);
                if (zlink_errno () == EAGAIN || zlink_errno () == EINTR)
                    break;
                return false;
            }

            const int payload_rc =
              zlink_msg_recv (&payload_frame, server, ZLINK_DONTWAIT);
            if (payload_rc < 0) {
                zlink_msg_close (&id_frame);
                zlink_msg_close (&payload_frame);
                if (zlink_errno () == EAGAIN || zlink_errno () == EINTR)
                    break;
                return false;
            }

            const size_t payload_size = zlink_msg_size (&payload_frame);
            const char *payload_data =
              static_cast<const char *> (zlink_msg_data (&payload_frame));
            if (payload_size > 0
                && payload_data
                && !is_stream_event_payload (payload_data, payload_size)) {
                const char *id_data =
                  static_cast<const char *> (zlink_msg_data (&id_frame));
                const size_t id_size = zlink_msg_size (&id_frame);
                if (id_data && id_size > 0) {
                    std::string routing_id (id_data, id_size);
                    if (extract_single_framed_payload (
                          payload_data, payload_size, &payload_out)) {
                        routing_id_out = routing_id;
                        zlink_msg_close (&id_frame);
                        zlink_msg_close (&payload_frame);
                        return true;
                    }

                    stream_buffer_t &stash = stashes[routing_id];
                    std::vector<char> framed_payload;
                    normalize_stream_reply_frame (
                      payload_data, payload_size, &framed_payload);
                    stash.append (
                      framed_payload.empty () ? NULL : framed_payload.data (),
                      framed_payload.size ());
                    if (stream_decode_one_frame (stash, &payload_out)) {
                        routing_id_out = routing_id;
                        zlink_msg_close (&id_frame);
                        zlink_msg_close (&payload_frame);
                        return true;
                    }
                }
            }

            zlink_msg_close (&id_frame);
            zlink_msg_close (&payload_frame);
        }
    }

    return false;
}

bool send_stream_reply (void *server,
                        const std::vector<unsigned char> &routing_id,
                        const std::vector<char> &payload)
{
    if (routing_id.empty ())
        return false;
    zlink_routing_id_t target;
    std::memset (&target, 0, sizeof (target));
    const size_t copy_size = std::min<size_t> (routing_id.size (), sizeof (target.data));
    target.size = static_cast<uint8_t> (copy_size);
    if (copy_size > 0)
        std::memcpy (target.data, routing_id.data (), copy_size);

    return zlink_stream_send (
             server,
             &target,
             payload.empty () ? NULL : payload.data (),
             payload.size (),
             0)
           >= 0;
}

double measure_stream_latency_live (void *server,
                                    tcp_sender_state_t &sender,
                                    size_t msg_size,
                                    int io_timeout_ms)
{
    (void) server;
    if (!sender.valid ())
        return 0.0;

    const int lat_count = resolve_bench_count ("BENCH_LAT_COUNT", 500);
    std::vector<char> payload (std::max<size_t> (1, msg_size), '\xA7');
    std::vector<char> recv_payload;

    stopwatch_t sw;
    sw.start ();
    for (int i = 0; i < lat_count; ++i) {
        if (payload.size () >= 4) {
            const uint32_t seq = static_cast<uint32_t> (i + 1);
            std::memcpy (&payload[0], &seq, sizeof (seq));
        }
        if (!send_raw_framed (sender.fd, payload)) {
            if (bench_debug_enabled ())
                std::fprintf (stderr, "MULTI_STREAM latency live fail: send_raw\n");
            return 0.0;
        }

        size_t invalid_prefix_len = 0;
        const recv_sender_frame_status_t recv_status = recv_sender_framed_payload (
          sender, io_timeout_ms, &recv_payload, &invalid_prefix_len);
        if (recv_status != recv_sender_frame_ok
            || recv_payload.size () != payload.size ()) {
            if (bench_debug_enabled ()) {
                std::fprintf (
                  stderr,
                  "MULTI_STREAM latency live fail: recv_raw status=%s size=%zu expected=%zu stash=%zu invalid_prefix_len=%zu\n",
                  recv_sender_frame_status_name (recv_status), recv_payload.size (),
                  payload.size (), sender.recv_stash.available (),
                  invalid_prefix_len);
            }
            return 0.0;
        }
        if (!std::equal (recv_payload.begin (), recv_payload.end (),
                         payload.begin ())) {
            if (bench_debug_enabled ()) {
                std::fprintf (
                  stderr,
                  "MULTI_STREAM latency live fail: payload_mismatch size=%zu\n",
                  recv_payload.size ());
            }
            return 0.0;
        }
    }

    return (sw.elapsed_ms () * 1000.0) / std::max (1, lat_count * 2);
}

} // namespace

void run_multi_stream (const std::string &transport,
                       size_t msg_size,
                       int /*msg_count*/,
                       const std::string &lib_name)
{
    const std::vector<size_t> msg_sizes = resolve_bench_msg_sizes (msg_size);
    if (transport != "tcp") {
        for (size_t s = 0; s < msg_sizes.size (); ++s) {
            print_prep_result (
              lib_name, "MULTI_STREAM", transport, msg_sizes[s], 0.0, 0.0);
            print_result (
              lib_name, "MULTI_STREAM", transport, msg_sizes[s], 0.0, 0.0);
        }
        return;
    }

    const multi_bench_settings_t settings = resolve_multi_bench_settings ();
    const int stream_send_batch =
      resolve_multi_int_env ("BENCH_MULTI_STREAM_SEND_BATCH", 64, 1);
    const int stream_client_threads_default =
      settings.clients >= 10000 ? 4 : 1;
    int stream_client_threads = resolve_multi_int_env (
      "BENCH_STREAM_CLIENT_THREADS",
      resolve_multi_int_env (
        "BENCH_MULTI_STREAM_SEND_WORKERS", stream_client_threads_default, 1),
      1);
    stream_client_threads = std::max (1, std::min (4, stream_client_threads));
    multi_bench_settings_t stream_settings = settings;
    stream_settings.send_workers = std::max (
      1,
      std::min<int> (
        stream_client_threads, static_cast<int> (std::max<size_t> (1, settings.clients))));
    const int stream_hwm = resolve_multi_int_env ("BENCH_STREAM_HWM", 100000, 1);
    if (settings.clients == 0) {
        for (size_t s = 0; s < msg_sizes.size (); ++s) {
            print_result (
              lib_name, "MULTI_STREAM", transport, msg_sizes[s], 0.0, 0.0);
        }
        return;
    }

    ctx_guard_t ctx;
    if (!ctx.valid ())
        return;

    void *server = zlink_socket (ctx.get (), ZLINK_STREAM);
    if (!server)
        return;

    const int linger_ms = 0;
    set_sockopt_int (server, ZLINK_LINGER, linger_ms, "ZLINK_LINGER");
    apply_benchmark_hwm (server, stream_hwm);
    apply_stream_server_tuning (server, true);

    const int io_timeout_ms = resolve_bench_count ("BENCH_STREAM_TIMEOUT_MS", 5000);
    set_sockopt_int (server, ZLINK_SNDTIMEO, io_timeout_ms, "ZLINK_SNDTIMEO");
    set_sockopt_int (server, ZLINK_RCVTIMEO, io_timeout_ms, "ZLINK_RCVTIMEO");

    const std::string endpoint = bind_and_resolve_endpoint (
      server, transport, lib_name + "_multi_stream");
    if (endpoint.empty ()) {
        zlink_close (server);
        return;
    }

    connect_monitor_t server_monitor;
    if (!open_connect_monitor (server, server_monitor)) {
        zlink_close (server);
        return;
    }

    stream_len32be_dispatch_t dispatch;
    bool dispatch_started = false;
    std::vector<tcp_sender_state_t> senders (settings.clients);
    stream_client_poller_t stream_poller;
    bool ready_wait_done = false;
    bool poller_ready = false;
    std::vector<double> throughput_values (msg_sizes.size (), 0.0);
    std::vector<double> latency_values (msg_sizes.size (), 0.0);
    std::vector<double> prep_connect_values (msg_sizes.size (), 0.0);
    std::vector<double> prep_ready_values (msg_sizes.size (), 0.0);
    size_t completed_sizes = 0;
    auto close_throughput_senders = [&] () {
        for (size_t i = 0; i < senders.size (); ++i)
            close_sender (senders[i]);
        stream_poller.reset ();
    };
    for (size_t s = 0; s < msg_sizes.size (); ++s) {
        const size_t current_size = msg_sizes[s];
        std::vector<char> payload (std::max<size_t> (1, current_size), 'a');

        const multi_bench_result_t bench =
          run_multi_phase_benchmark_with_sender_lifecycle_batched (
            settings.clients, stream_settings,
            stream_send_batch,
            [&] (size_t idx) { return setup_sender (senders[idx], endpoint); },
            [&] (size_t idx) { return send_sender_nonblocking (senders[idx], payload); },
            [&] (multi_bench_phase_t) {
                return recv_batch_stream_echoes (
                  senders, stream_poller, settings.recv_batch, 10, current_size);
            },
            [&] (size_t) {},
            [&] () {
                if (!ready_wait_done) {
                    ready_wait_done = true;
                    size_t observed_ready = 0;
                    const bool ready_ok =
                      wait_stream_ready_count (server_monitor, settings.clients,
                                               settings.connect_ready_timeout_ms,
                                               &observed_ready);
                    if (!ready_ok) {
                        if (bench_debug_enabled ()) {
                            std::fprintf (
                              stderr,
                              "MULTI_STREAM: connect_ready wait timeout (expected=%zu observed=%zu)\n",
                              settings.clients, observed_ready);
                        }
                        return false;
                    }
                }
                if (!dispatch_started) {
                    if (!start_stream_len32be_dispatch (server, dispatch)) {
                        if (bench_debug_enabled ()) {
                            std::fprintf (
                              stderr,
                              "MULTI_STREAM: zlink_stream_start_len32be failed\n");
                        }
                        return false;
                    }
                    dispatch_started = true;
                }
                if (!poller_ready) {
                    poller_ready = stream_poller.rebuild (senders);
                    if (!poller_ready)
                        return false;
                }
                return true;
            },
            false);

        if (bench.failed) {
            break;
        }

        prep_connect_values[s] = bench.connect_ms;
        prep_ready_values[s] = bench.ready_wait_ms;
        throughput_values[s] =
          bench.measure_recv > 0
            ? static_cast<double> (bench.measure_recv)
                / static_cast<double> (std::max (1, settings.measure_seconds))
            : 0.0;
        completed_sizes = s + 1;
    }

    close_throughput_senders ();
    stop_stream_len32be_dispatch (dispatch);
    close_connect_monitor (server_monitor);
    zlink_close (server);

    if (completed_sizes > 0) {
        void *lat_server = zlink_socket (ctx.get (), ZLINK_STREAM);
        if (lat_server) {
            set_sockopt_int (lat_server, ZLINK_LINGER, linger_ms, "ZLINK_LINGER");
            apply_benchmark_hwm (lat_server, stream_hwm);
            apply_stream_server_tuning (lat_server, true);
            set_sockopt_int (
              lat_server, ZLINK_SNDTIMEO, io_timeout_ms, "ZLINK_SNDTIMEO");
            set_sockopt_int (
              lat_server, ZLINK_RCVTIMEO, io_timeout_ms, "ZLINK_RCVTIMEO");

            const std::string lat_endpoint = bind_and_resolve_endpoint (
              lat_server, transport, lib_name + "_multi_stream_lat_phase");
            if (!lat_endpoint.empty ()) {
                connect_monitor_t lat_monitor;
                const bool has_lat_monitor =
                  open_connect_monitor (lat_server, lat_monitor);
                stream_len32be_dispatch_t lat_dispatch;
                const bool lat_dispatch_started =
                  start_stream_len32be_dispatch (lat_server, lat_dispatch);

                if (lat_dispatch_started) {
                    tcp_sender_state_t latency_sender;
                    if (setup_sender (latency_sender, lat_endpoint)) {
                        if (has_lat_monitor) {
                            size_t observed_ready = 0;
                            (void) wait_stream_ready_count (
                              lat_monitor, 1, settings.connect_ready_timeout_ms,
                              &observed_ready);
                        } else {
                            std::this_thread::sleep_for (
                              std::chrono::milliseconds (10));
                        }
                        drain_sender_echoes (latency_sender);
                        for (size_t s = 0; s < completed_sizes; ++s) {
                            latency_values[s] = measure_stream_latency_live (
                              lat_server, latency_sender, msg_sizes[s], io_timeout_ms);
                        }
                        close_sender (latency_sender);
                    } else if (bench_debug_enabled ()) {
                        std::fprintf (
                          stderr,
                          "MULTI_STREAM latency live fail: setup_latency_sender\n");
                    }
                    stop_stream_len32be_dispatch (lat_dispatch);
                } else if (bench_debug_enabled ()) {
                    std::fprintf (
                      stderr,
                      "MULTI_STREAM latency live fail: start_latency_dispatch\n");
                }
                if (has_lat_monitor)
                    close_connect_monitor (lat_monitor);
            }
            zlink_close (lat_server);
        }
    }

    for (size_t s = 0; s < msg_sizes.size (); ++s) {
        print_prep_result (
          lib_name, "MULTI_STREAM", transport, msg_sizes[s],
          prep_connect_values[s], prep_ready_values[s]);
        print_result (
          lib_name, "MULTI_STREAM", transport, msg_sizes[s],
          throughput_values[s], latency_values[s]);
    }
}

int main (int argc, char **argv)
{
    return run_standard_bench_main (argc, argv, run_multi_stream);
}
