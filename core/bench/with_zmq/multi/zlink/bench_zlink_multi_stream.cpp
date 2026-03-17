#include "../../../../perf/multi/common/perf_common.hpp"
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
#include <dlfcn.h>
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

#ifndef BENCH_MULTI_STREAM_PATTERN_NAME
#define BENCH_MULTI_STREAM_PATTERN_NAME "MULTI_STREAM"
#endif

#ifndef BENCH_MULTI_STREAM_DISPATCH_MODE_VALUE
#define BENCH_MULTI_STREAM_DISPATCH_MODE_VALUE 0
#endif

inline const char *multi_stream_pattern_name ()
{
    return BENCH_MULTI_STREAM_PATTERN_NAME;
}

enum multi_stream_dispatch_mode_t
{
    multi_stream_dispatch_recv = 0,
    multi_stream_dispatch_callback = 1,
    multi_stream_dispatch_len32be = 2
};

multi_stream_dispatch_mode_t resolve_multi_stream_dispatch_mode ()
{
    const int mode_value = BENCH_MULTI_STREAM_DISPATCH_MODE_VALUE;
    if (mode_value <= 0)
        return multi_stream_dispatch_recv;
    if (mode_value == 1)
        return multi_stream_dispatch_callback;
    return multi_stream_dispatch_len32be;
}

bool multi_stream_dispatch_enabled (multi_stream_dispatch_mode_t mode)
{
    return mode != multi_stream_dispatch_recv;
}

int multi_stream_dispatch_flags (multi_stream_dispatch_mode_t mode)
{
    if (mode == multi_stream_dispatch_len32be)
        return ZLINK_STREAM_DISPATCH_LEN32BE;
    return 0;
}

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
static stream_len32be_dispatch_t *g_stream_dispatch_aux = NULL;
static std::atomic<bool> g_stream_reply_direct (false);
static zlink_stream_on_packets_fn g_stream_attach_bridge_callback = NULL;

bool stream_dispatch_supported ()
{
    return true;
}

void on_stream_attach_bridge (const zlink_routing_id_t *rid_,
                              zlink_msg_t *parts_,
                              size_t part_count_,
                              void *)
{
    zlink_stream_on_packets_fn callback = g_stream_attach_bridge_callback;
    if (callback && rid_ && parts_ && part_count_ > 0)
        (void) callback (rid_, &parts_[0], 1);
    for (size_t i = 1; i < part_count_; ++i)
        (void) zlink_msg_close (&parts_[i]);
}

int stream_attach_compat (void *socket_,
                          zlink_stream_on_packets_fn callback,
                          int flags)
{
    if (!socket_ || !callback) {
        errno = EINVAL;
        return -1;
    }

    if ((flags & ZLINK_STREAM_DISPATCH_LEN32BE) != 0)
        return zlink_stream_attach_len32be (socket_, callback);

    g_stream_attach_bridge_callback = callback;
    const int rc = zlink_recv_handler (socket_, &on_stream_attach_bridge, NULL);
    if (rc != 0)
        g_stream_attach_bridge_callback = NULL;
    return rc;
}

int stream_detach_compat (void *socket_)
{
    LIBZLINK_UNUSED (socket_);
    g_stream_attach_bridge_callback = NULL;
    return 0;
}

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

bool send_stream_reply_with_retry (void *socket,
                                   const zlink_routing_id_t *rid,
                                   const char *payload_data,
                                   size_t payload_size)
{
    const int retry_limit = resolve_multi_int_env (
      "BENCH_MULTI_STREAM_SEND_RETRIES", 256, 1);
    const int retry_backoff_us = resolve_multi_int_env (
      "BENCH_MULTI_STREAM_SEND_RETRY_BACKOFF_US", 20, 0);
    const unsigned char *payload =
      payload_size > 0 ? reinterpret_cast<const unsigned char *> (payload_data)
                       : NULL;

    for (int attempt = 0; attempt < retry_limit; ++attempt) {
        const int rc = zlink_stream_send (socket, rid, payload, payload_size, 0);
        if (rc == static_cast<int> (payload_size))
            return true;

        if (rc >= 0) {
            if (bench_debug_enabled ()) {
                std::fprintf (
                  stderr,
                  "MULTI_STREAM reply send partial: rc=%d expected=%zu attempt=%d/%d\n",
                  rc,
                  payload_size,
                  attempt + 1,
                  retry_limit);
            }
            return false;
        }

        const int err = zlink_errno ();
        if (err != EAGAIN && err != EINTR) {
            if (bench_debug_enabled ()) {
                std::fprintf (
                  stderr,
                  "MULTI_STREAM reply send fail: rc=%d err=%d size=%zu attempt=%d/%d\n",
                  rc,
                  err,
                  payload_size,
                  attempt + 1,
                  retry_limit);
            }
            return false;
        }
        if (retry_backoff_us > 0) {
            std::this_thread::sleep_for (
              std::chrono::microseconds (retry_backoff_us));
        } else {
            std::this_thread::yield ();
        }
    }

    if (bench_debug_enabled ()) {
        std::fprintf (
          stderr,
          "MULTI_STREAM reply send fail: retries_exhausted size=%zu retries=%d\n",
          payload_size,
          retry_limit);
    }
    return false;
}

int on_stream_len32be_packets (const zlink_routing_id_t *rid_,
                               zlink_msg_t *msgs_,
                               size_t msg_count_)
{
    stream_len32be_dispatch_t *dispatch = g_stream_dispatch;
    if (!dispatch || !rid_ || !msgs_ || msg_count_ == 0)
        return 0;

    const bool direct_reply =
      g_stream_reply_direct.load (std::memory_order_acquire);
    std::unique_lock<std::mutex> guard;
    if (!direct_reply)
        guard = std::unique_lock<std::mutex> (dispatch->lock);

    for (size_t i = 0; i < msg_count_; ++i) {
        zlink_msg_t *msg = &msgs_[i];
        const char *payload_data =
          static_cast<const char *> (zlink_msg_data (msg));
        const size_t payload_size = zlink_msg_size (msg);
        if (payload_size == 1 && payload_data
            && (static_cast<unsigned char> (payload_data[0]) == STREAM_EVENT_CONNECT
                || static_cast<unsigned char> (payload_data[0])
                     == STREAM_EVENT_DISCONNECT)) {
            (void) zlink_msg_close (msg);
            continue;
        }
        if (!payload_data && payload_size > 0) {
            (void) zlink_msg_close (msg);
            continue;
        }
        if (direct_reply) {
            if (!send_stream_reply_with_retry (
                  dispatch->socket,
                  rid_,
                  payload_data,
                  payload_size)) {
                (void) zlink_msg_close (msg);
                for (size_t j = i + 1; j < msg_count_; ++j)
                    (void) zlink_msg_close (&msgs_[j]);
                return 1;
            }
            (void) zlink_msg_close (msg);
            continue;
        }

        stream_dispatch_packet_t packet;
        packet.routing_id.assign (
          reinterpret_cast<const char *> (rid_->data),
          static_cast<size_t> (rid_->size));
        packet.payload.assign (payload_size, 0);
        if (payload_size > 0)
            std::memcpy (packet.payload.data (), payload_data, payload_size);
        dispatch->packets.push_back (packet);
        (void) zlink_msg_close (msg);
    }

    if (!direct_reply) {
        guard.unlock ();
        dispatch->cv.notify_all ();
    }
    return dispatch->running.load (std::memory_order_acquire) ? 0 : 1;
}

int on_stream_len32be_packets_aux (const zlink_routing_id_t *rid_,
                                   zlink_msg_t *msgs_,
                                   size_t msg_count_)
{
    stream_len32be_dispatch_t *dispatch = g_stream_dispatch_aux;
    if (!dispatch || !rid_ || !msgs_ || msg_count_ == 0)
        return 0;

    std::unique_lock<std::mutex> guard (dispatch->lock);
    for (size_t i = 0; i < msg_count_; ++i) {
        zlink_msg_t *msg = &msgs_[i];
        const char *payload_data =
          static_cast<const char *> (zlink_msg_data (msg));
        const size_t payload_size = zlink_msg_size (msg);
        if (payload_size == 1 && payload_data
            && (static_cast<unsigned char> (payload_data[0]) == STREAM_EVENT_CONNECT
                || static_cast<unsigned char> (payload_data[0])
                     == STREAM_EVENT_DISCONNECT)) {
            (void) zlink_msg_close (msg);
            continue;
        }
        if (!payload_data && payload_size > 0) {
            (void) zlink_msg_close (msg);
            continue;
        }

        stream_dispatch_packet_t packet;
        packet.routing_id.assign (
          reinterpret_cast<const char *> (rid_->data),
          static_cast<size_t> (rid_->size));
        packet.payload.assign (payload_size, 0);
        if (payload_size > 0)
            std::memcpy (packet.payload.data (), payload_data, payload_size);
        dispatch->packets.push_back (packet);
        (void) zlink_msg_close (msg);
    }
    guard.unlock ();
    dispatch->cv.notify_all ();
    return dispatch->running.load (std::memory_order_acquire) ? 0 : 1;
}

bool start_stream_len32be_dispatch_slot (void *socket_,
                                         stream_len32be_dispatch_t &dispatch,
                                         stream_len32be_dispatch_t **slot,
                                         zlink_stream_on_packets_fn callback,
                                         int dispatch_flags)
{
    dispatch.socket = socket_;
    dispatch.running.store (true, std::memory_order_release);
    *slot = &dispatch;
    if (stream_attach_compat (socket_, callback, dispatch_flags)
        != 0) {
        dispatch.running.store (false, std::memory_order_release);
        dispatch.socket = NULL;
        if (*slot == &dispatch)
            *slot = NULL;
        return false;
    }
    return true;
}

void stop_stream_len32be_dispatch_slot (stream_len32be_dispatch_t &dispatch,
                                        stream_len32be_dispatch_t **slot)
{
    if (!dispatch.running.exchange (false, std::memory_order_acq_rel))
        return;
    if (dispatch.socket)
        (void) stream_detach_compat (dispatch.socket);
    {
        std::lock_guard<std::mutex> guard (dispatch.lock);
        dispatch.packets.clear ();
    }
    dispatch.cv.notify_all ();
    if (*slot == &dispatch)
        *slot = NULL;
    dispatch.socket = NULL;
}

bool start_stream_len32be_dispatch (void *socket_,
                                    stream_len32be_dispatch_t &dispatch,
                                    int dispatch_flags)
{
    return start_stream_len32be_dispatch_slot (
      socket_, dispatch, &g_stream_dispatch, &on_stream_len32be_packets,
      dispatch_flags);
}

bool start_stream_len32be_dispatch_aux (void *socket_,
                                        stream_len32be_dispatch_t &dispatch,
                                        int dispatch_flags)
{
    return start_stream_len32be_dispatch_slot (
      socket_, dispatch, &g_stream_dispatch_aux, &on_stream_len32be_packets_aux,
      dispatch_flags);
}

void stop_stream_len32be_dispatch (stream_len32be_dispatch_t &dispatch)
{
    stop_stream_len32be_dispatch_slot (dispatch, &g_stream_dispatch);
}

void stop_stream_len32be_dispatch_aux (stream_len32be_dispatch_t &dispatch)
{
    stop_stream_len32be_dispatch_slot (dispatch, &g_stream_dispatch_aux);
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

bool pop_stream_len32be_packet_aux (void *socket_,
                                    int timeout_ms,
                                    bool dontwait,
                                    stream_dispatch_packet_t &packet_out)
{
    stream_len32be_dispatch_t *dispatch = g_stream_dispatch_aux;
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

enum stream_sender_mode_t
{
    stream_sender_raw_tcp = 0,
    stream_sender_zlink = 1
};

struct stream_sender_state_t {
    socket_t fd;
    void *socket;
    stream_sender_mode_t mode;
    bool multipart_id_sent;
    std::vector<unsigned char> routing_id;
    std::vector<char> frame;
    size_t sent_bytes;
    stream_buffer_t recv_stash;

    stream_sender_state_t () :
        fd (INVALID_SOCKET_FD),
        socket (NULL),
        mode (stream_sender_raw_tcp),
        multipart_id_sent (false),
        sent_bytes (0)
    {
    }

    bool valid () const
    {
        if (mode == stream_sender_raw_tcp)
            return fd != INVALID_SOCKET_FD;
        return socket != NULL;
    }

    bool is_raw_tcp () const
    {
        return mode == stream_sender_raw_tcp;
    }
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

    bool rebuild (const std::vector<stream_sender_state_t> &senders)
    {
        reset ();
        items.reserve (senders.size ());
        sender_indices.reserve (senders.size ());

        for (size_t i = 0; i < senders.size (); ++i) {
            if (!senders[i].valid ())
                continue;

            zlink_pollitem_t item;
            std::memset (&item, 0, sizeof (item));
            if (senders[i].is_raw_tcp ()) {
                item.socket = NULL;
                item.fd = senders[i].fd;
            } else {
                item.socket = senders[i].socket;
                item.fd = 0;
            }
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

bool enqueue_stream_reply_payload (stream_reply_queue_t &pending_replies,
                                   const std::string &routing_id,
                                   const std::vector<char> &payload,
                                   size_t max_pending)
{
    if (routing_id.empty () || pending_replies.size () >= max_pending)
        return false;

    stream_pending_reply_t reply;
    reply.routing_id = routing_id;
    reply.payload = payload;
    pending_replies.push_back (reply);
    return true;
}

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

bool wait_monitor_connect_event (void *monitor_socket,
                                 std::vector<unsigned char> &routing_id_out,
                                 int timeout_ms)
{
    if (!monitor_socket)
        return false;

    const auto deadline =
      std::chrono::steady_clock::now ()
      + std::chrono::milliseconds (std::max (0, timeout_ms));
    while (true) {
        const auto now = std::chrono::steady_clock::now ();
        if (now >= deadline)
            return false;

        const long remain_ms =
          std::chrono::duration_cast<std::chrono::milliseconds> (deadline - now)
            .count ();
        zlink_pollitem_t items[] = {{monitor_socket, 0, ZLINK_POLLIN, 0}};
        const int rc = zlink_poll (items, 1, remain_ms > 0 ? remain_ms : 0);
        if (rc < 0) {
            if (zlink_errno () == EINTR)
                continue;
            return false;
        }
        if (rc <= 0 || (items[0].revents & ZLINK_POLLIN) == 0)
            continue;

        for (;;) {
            zlink_monitor_event_t event;
            std::memset (&event, 0, sizeof (event));
            if (zlink_monitor_recv (monitor_socket, &event, ZLINK_DONTWAIT) != 0)
                break;
            if (event.event != ZLINK_EVENT_CONNECTION_READY
                || event.routing_id.size == 0) {
                continue;
            }

            routing_id_out.assign (
              event.routing_id.data,
              event.routing_id.data + event.routing_id.size);
            return true;
        }
    }

    return false;
}

void reset_sender_runtime_state (stream_sender_state_t &sender)
{
    sender.frame.clear ();
    sender.sent_bytes = 0;
    sender.multipart_id_sent = false;
    sender.recv_stash.reset ();
}

void close_sender (stream_sender_state_t &sender)
{
    if (sender.valid ()) {
        if (sender.is_raw_tcp ())
            close_socket_fd (sender.fd);
        else if (sender.socket)
            zlink_close (sender.socket);
    }
    sender.fd = INVALID_SOCKET_FD;
    sender.socket = NULL;
    sender.routing_id.clear ();
    sender.mode = stream_sender_raw_tcp;
    reset_sender_runtime_state (sender);
}

bool setup_sender_raw_tcp (stream_sender_state_t &sender,
                           const std::string &endpoint)
{
    if (sender.valid ())
        return true;

    sender.mode = stream_sender_raw_tcp;
    socket_t fd = connect_tcp_socket (endpoint, true);
    if (fd == INVALID_SOCKET_FD)
        return false;
    if (!set_socket_nonblocking (fd)) {
        close_socket_fd (fd);
        return false;
    }

    sender.fd = fd;
    sender.socket = NULL;
    sender.routing_id.clear ();
    reset_sender_runtime_state (sender);
    return true;
}

bool setup_sender_zlink (stream_sender_state_t &sender,
                         void *ctx,
                         const std::string &transport,
                         const std::string &endpoint,
                         int stream_hwm,
                         int io_timeout_ms,
                         int connect_ready_timeout_ms)
{
    if (sender.valid ())
        return true;

    sender.mode = stream_sender_zlink;
    void *client = zlink_socket (ctx, ZLINK_STREAM);
    if (!client)
        return false;

    const int linger_ms = 0;
    set_sockopt_int (client, ZLINK_LINGER, linger_ms, "ZLINK_LINGER");
    apply_benchmark_hwm (client, stream_hwm);
    set_sockopt_int (client, ZLINK_SNDTIMEO, io_timeout_ms, "ZLINK_SNDTIMEO");
    set_sockopt_int (client, ZLINK_RCVTIMEO, io_timeout_ms, "ZLINK_RCVTIMEO");
    if (!setup_tls_client (client, transport)) {
        zlink_close (client);
        return false;
    }

    connect_monitor_t monitor;
    if (!open_connect_monitor (client, monitor)) {
        zlink_close (client);
        return false;
    }

    std::vector<unsigned char> routing_id;
    const bool connected =
      connect_checked (client, endpoint, transport)
      && wait_monitor_connect_event (
        monitor.monitor, routing_id, connect_ready_timeout_ms);
    close_connect_monitor (monitor);
    zlink_routing_id_t routing_id_max;
    if (!connected || routing_id.empty ()
        || routing_id.size () > sizeof (routing_id_max.data)) {
        if (bench_debug_enabled ()) {
            std::fprintf (
              stderr,
              "MULTI_STREAM zlink sender setup failed: connected=%d rid_size=%zu\n",
              connected ? 1 : 0,
              routing_id.size ());
        }
        zlink_close (client);
        return false;
    }

    sender.fd = INVALID_SOCKET_FD;
    sender.socket = client;
    sender.routing_id.swap (routing_id);
    reset_sender_runtime_state (sender);
    return true;
}

bool setup_sender (stream_sender_state_t &sender,
                   void *ctx,
                   const std::string &transport,
                   const std::string &endpoint,
                   int stream_hwm,
                   int io_timeout_ms,
                   int connect_ready_timeout_ms,
                   bool use_raw_tcp)
{
    if (sender.valid ()) {
        // Keep connection alive across message-size rounds, but reset any
        // partially staged frame from a previous round.
        reset_sender_runtime_state (sender);
        return true;
    }

    return use_raw_tcp
             ? setup_sender_raw_tcp (sender, endpoint)
             : setup_sender_zlink (
                 sender, ctx, transport, endpoint, stream_hwm, io_timeout_ms,
                 connect_ready_timeout_ms);
}

multi_send_result_t send_sender_nonblocking (stream_sender_state_t &sender,
                                             const std::vector<char> &payload)
{
    if (!sender.valid ())
        return multi_send_error;

    if (!sender.is_raw_tcp ()) {
        if (sender.routing_id.empty ())
            return multi_send_error;

        if (sender.frame.empty ()) {
            stream_build_framed_payload (payload, &sender.frame);
            sender.sent_bytes = 0;
        }

        zlink_routing_id_t rid;
        std::memset (&rid, 0, sizeof (rid));
        if (sender.routing_id.size () > sizeof (rid.data))
            return multi_send_error;
        rid.size = static_cast<uint8_t> (sender.routing_id.size ());
        std::memcpy (rid.data, sender.routing_id.data (), sender.routing_id.size ());
        const int payload_rc = zlink_stream_send (
          sender.socket,
          &rid,
          sender.frame.empty () ? NULL : sender.frame.data (),
          sender.frame.size (),
          ZLINK_DONTWAIT);
        if (payload_rc == static_cast<int> (sender.frame.size ())) {
            sender.frame.clear ();
            sender.sent_bytes = 0;
            sender.multipart_id_sent = false;
            return multi_send_ok;
        }

        const int err = zlink_errno ();
        if (err == EAGAIN || err == EINTR)
            return multi_send_would_block;
        if (bench_debug_enabled ()) {
            std::fprintf (
              stderr,
              "MULTI_STREAM zlink send error: err=%d rid_size=%zu frame=%zu\n",
              err,
              sender.routing_id.size (),
              sender.frame.size ());
        }
        return multi_send_error;
    }

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

    if (!monitor.monitor) {
        if (observed_ready_out)
            *observed_ready_out = 0;
        return false;
    }

    size_t ready_events = static_cast<size_t> (
      std::max (0, poll_connect_ready_count (monitor)));
    size_t observed_ready = ready_events;
    if (ready_events >= expected_ready) {
        if (observed_ready_out)
            *observed_ready_out = observed_ready;
        return true;
    }

    const auto deadline =
      std::chrono::steady_clock::now ()
      + std::chrono::milliseconds (std::max (0, timeout_ms));

    while (ready_events < expected_ready) {
        const auto now = std::chrono::steady_clock::now ();
        if (now >= deadline)
            break;

        const long remain_ms =
          std::chrono::duration_cast<std::chrono::milliseconds> (deadline - now)
            .count ();
        zlink_pollitem_t items[] = {{monitor.monitor, 0, ZLINK_POLLIN, 0}};
        const int rc = zlink_poll (items, 1, remain_ms > 0 ? remain_ms : 0);
        if (rc < 0) {
            if (zlink_errno () == EINTR)
                continue;
            if (observed_ready_out)
                *observed_ready_out = observed_ready;
            return false;
        }
        if (rc == 0 || (items[0].revents & ZLINK_POLLIN) == 0)
            continue;

        ready_events += static_cast<size_t> (
          std::max (0, poll_connect_ready_count (monitor)));
        observed_ready = std::max<size_t> (observed_ready, ready_events);
        if (ready_events >= expected_ready) {
            if (observed_ready_out)
                *observed_ready_out = observed_ready;
            return true;
        }
    }

    if (observed_ready_out)
        *observed_ready_out = observed_ready;
    return ready_events >= expected_ready;
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
        if (reply.routing_id.empty ()) {
            pending_replies.pop_front ();
            continue;
        }

        zlink_routing_id_t target;
        std::memset (&target, 0, sizeof (target));
        const size_t copy_size =
          std::min<size_t> (reply.routing_id.size (), sizeof (target.data));
        if (copy_size == 0) {
            pending_replies.pop_front ();
            continue;
        }
        target.size = static_cast<uint8_t> (copy_size);
        std::memcpy (target.data, reply.routing_id.data (), copy_size);

        const char *payload_ptr = reply.payload.empty () ? NULL : &reply.payload[0];
        if (!send_stream_reply_with_retry (
              server, &target, payload_ptr, reply.payload.size ())) {
            if (bench_debug_enabled ()) {
                std::fprintf (
                  stderr,
                  "MULTI_STREAM reply dropped: err=%d rid_size=%zu payload=%zu\n",
                  zlink_errno (),
                  reply.routing_id.size (),
                  reply.payload.size ());
            }
            // A single disconnected peer must not block the whole relay queue.
            pending_replies.pop_front ();
            continue;
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
                                  size_t max_pending,
                                  size_t expected_payload_size,
                                  bool send_requires_framed_payload,
                                  stream_stash_map_t *dispatch_stashes)
{
    if (relay_budget <= 0 || max_pending == 0 || pending_replies.size () >= max_pending)
        return 0;

    int enqueued = 0;
    auto enqueue_decoded_payload = [&] (const std::string &routing_id,
                                        const std::vector<char> &decoded_payload,
                                        bool send_requires_framed_payload) {
        if (expected_payload_size > 0
            && decoded_payload.size () != expected_payload_size) {
            return;
        }

        std::vector<char> wire_payload;
        if (send_requires_framed_payload) {
            stream_build_framed_payload (decoded_payload, &wire_payload);
        } else {
            wire_payload = decoded_payload;
        }

        if (enqueue_stream_reply_payload (
              pending_replies, routing_id, wire_payload, max_pending)) {
            ++enqueued;
        }
    };

    auto process_payload_chunk = [&] (const std::string &routing_id,
                                      const char *payload_data,
                                      size_t payload_size,
                                      bool may_be_decoded_payload,
                                      bool send_requires_framed_payload) {
        if (routing_id.empty () || !payload_data || payload_size == 0)
            return;

        std::vector<char> decoded_payload;
        if (extract_single_framed_payload (
              payload_data, payload_size, &decoded_payload)) {
            enqueue_decoded_payload (
              routing_id, decoded_payload, send_requires_framed_payload);
            return;
        }

        if (may_be_decoded_payload
            && !send_requires_framed_payload
            && (expected_payload_size == 0
                || payload_size == expected_payload_size)) {
            std::vector<char> direct_payload (payload_size, 0);
            std::memcpy (direct_payload.data (), payload_data, payload_size);
            enqueue_decoded_payload (
              routing_id, direct_payload, send_requires_framed_payload);
            return;
        }

        if (!dispatch_stashes)
            return;

        stream_buffer_t &stash = (*dispatch_stashes)[routing_id];
        stash.append (payload_data, payload_size);
        while (enqueued < relay_budget
               && pending_replies.size () < max_pending
               && stream_decode_one_frame (stash, &decoded_payload)) {
            enqueue_decoded_payload (
              routing_id, decoded_payload, send_requires_framed_payload);
        }
    };

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

        auto push_reply = [&] (const stream_dispatch_packet_t &packet_) {
            if (packet_.routing_id.empty ())
                return;
            process_payload_chunk (
              packet_.routing_id,
              packet_.payload.empty () ? NULL : packet_.payload.data (),
              packet_.payload.size (),
              true,
              send_requires_framed_payload);
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
                process_payload_chunk (
                  std::string (id_data, id_size),
                  payload_data,
                  payload_size,
                  true,
                  send_requires_framed_payload);
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
        if (frame_len != expected_payload_size) {
            continue;
        }
        ++completed;
    }

    stash.compact ();
    return completed;
}

int recv_sender_stream_chunk_nonblocking (stream_sender_state_t &sender,
                                          std::vector<char> *chunk_out,
                                          bool *peer_match_out)
{
    if (!chunk_out || !peer_match_out || !sender.valid () || sender.is_raw_tcp ())
        return -1;

    unsigned char routing_frame[255];
    const int routing_len = zlink_recv (
      sender.socket, routing_frame, sizeof (routing_frame), ZLINK_DONTWAIT);
    if (routing_len < 0) {
        const int err = zlink_errno ();
        if (err == EAGAIN || err == EINTR)
            return 0;
        return -1;
    }

    int more = 0;
    size_t more_size = sizeof (more);
    if (zlink_getsockopt (sender.socket, ZLINK_RCVMORE, &more, &more_size) != 0
        || !more) {
        return -1;
    }

    std::vector<char> payload_buf (512 * 1024, 0);
    const int payload_len = zlink_recv (
      sender.socket, payload_buf.data (), payload_buf.size (), ZLINK_DONTWAIT);
    if (payload_len < 0) {
        const int err = zlink_errno ();
        if (err == EAGAIN || err == EINTR)
            return 0;
        return -1;
    }

    if (payload_len > 0)
        chunk_out->assign (payload_buf.begin (), payload_buf.begin () + payload_len);
    else
        chunk_out->clear ();

    *peer_match_out =
      sender.routing_id.empty ()
      || (sender.routing_id.size () == static_cast<size_t> (routing_len)
          && std::memcmp (
               sender.routing_id.data (), routing_frame, sender.routing_id.size ())
               == 0);
    return 1;
}

int recv_sender_echoes_nonblocking (stream_sender_state_t &sender,
                                    int max_frames,
                                    size_t expected_payload_size)
{
    if (!sender.valid () || max_frames <= 0)
        return 0;

    if (!sender.is_raw_tcp ()) {
        int completed = 0;
        for (int i = 0; i < 8 && completed < max_frames; ++i) {
            std::vector<char> payload_chunk;
            bool peer_match = false;
            const int recv_rc = recv_sender_stream_chunk_nonblocking (
              sender, &payload_chunk, &peer_match);
            if (recv_rc == 0)
                break;
            if (recv_rc < 0) {
                if (bench_debug_enabled ()) {
                    std::fprintf (
                      stderr,
                      "MULTI_STREAM zlink recv_batch chunk_recv error: err=%d\n",
                      zlink_errno ());
                }
                return -1;
            }

            const char *payload_data =
              payload_chunk.empty () ? NULL : payload_chunk.data ();
            const size_t payload_size = payload_chunk.size ();
            if (peer_match
                && payload_size > 0
                && payload_data
                && !is_stream_event_payload (payload_data, payload_size)) {
                std::vector<char> decoded_payload;
                if (extract_single_framed_payload (
                      payload_data, payload_size, &decoded_payload)) {
                    if (expected_payload_size == 0
                        || decoded_payload.size () == expected_payload_size) {
                        ++completed;
                    }
                } else if (expected_payload_size > 0
                           && payload_size == expected_payload_size) {
                    ++completed;
                } else {
                    sender.recv_stash.append (
                      payload_data,
                      payload_size);
                    const int decoded = decode_sender_frames_with_expected_size (
                      sender.recv_stash, max_frames - completed, expected_payload_size);
                    if (decoded < 0) {
                        return -1;
                    }
                    completed += decoded;
                }
            }
        }
        return completed;
    }

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

void drain_sender_echoes (stream_sender_state_t &sender)
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
  stream_sender_state_t &sender,
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

    if (!sender.is_raw_tcp ()) {
        while (std::chrono::steady_clock::now () < deadline) {
            const auto now = std::chrono::steady_clock::now ();
            const long remain_ms =
              std::chrono::duration_cast<std::chrono::milliseconds> (
                deadline - now)
                .count ();

            zlink_pollitem_t item;
            std::memset (&item, 0, sizeof (item));
            item.socket = sender.socket;
            item.fd = 0;
            item.events = ZLINK_POLLIN;
            item.revents = 0;

            const int prc = zlink_poll (&item, 1, remain_ms > 0 ? remain_ms : 0);
            if (prc < 0) {
                if (zlink_errno () == EINTR)
                    continue;
                return recv_sender_frame_poll_error;
            }
            if (prc == 0 || (item.revents & ZLINK_POLLIN) == 0)
                continue;

            while (true) {
                std::vector<char> payload_chunk;
                bool peer_match = false;
                const int recv_rc = recv_sender_stream_chunk_nonblocking (
                  sender, &payload_chunk, &peer_match);
                if (recv_rc == 0)
                    break;
                if (recv_rc < 0) {
                    if (bench_debug_enabled ()) {
                        std::fprintf (
                          stderr,
                          "MULTI_STREAM zlink recv_one chunk_recv error: err=%d\n",
                          zlink_errno ());
                    }
                    return recv_sender_frame_recv_error;
                }

                const char *payload_data =
                  payload_chunk.empty () ? NULL : payload_chunk.data ();
                const size_t payload_size = payload_chunk.size ();
                if (peer_match
                    && payload_size > 0
                    && payload_data
                    && !is_stream_event_payload (payload_data, payload_size)) {
                    if (extract_single_framed_payload (
                          payload_data, payload_size, payload_out)) {
                        return recv_sender_frame_ok;
                    }

                    sender.recv_stash.append (payload_data, payload_size);
                    if (has_invalid_frame_prefix (sender.recv_stash, &invalid_len)) {
                        if (invalid_prefix_len_out)
                            *invalid_prefix_len_out = invalid_len;
                        sender.recv_stash.reset ();
                        return recv_sender_frame_invalid_prefix;
                    }
                    if (stream_decode_one_frame (sender.recv_stash, payload_out)) {
                        return recv_sender_frame_ok;
                    }
                }
            }
        }
        return recv_sender_frame_timeout;
    }

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

int recv_batch_client_echoes (std::vector<stream_sender_state_t> &senders,
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

int recv_batch_stream_echoes (std::vector<stream_sender_state_t> &senders,
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
                                    stream_sender_state_t &sender,
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
        const auto deadline =
          std::chrono::steady_clock::now ()
          + std::chrono::milliseconds (std::max (1, io_timeout_ms));
        bool sent_ok = false;
        while (std::chrono::steady_clock::now () < deadline) {
            const multi_send_result_t send_rc =
              send_sender_nonblocking (sender, payload);
            if (send_rc == multi_send_ok) {
                sent_ok = true;
                break;
            }
            if (send_rc == multi_send_error)
                break;

            std::this_thread::sleep_for (std::chrono::microseconds (50));
        }
        if (!sent_ok) {
            if (bench_debug_enabled ())
                std::fprintf (stderr, "MULTI_STREAM latency live fail: send\n");
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

double measure_stream_latency_live_zlink_dispatch (stream_sender_state_t &sender,
                                                   size_t msg_size,
                                                   int io_timeout_ms)
{
    if (!sender.valid () || sender.is_raw_tcp () || sender.routing_id.size () != 4)
        return 0.0;

    const int lat_count = resolve_bench_count ("BENCH_LAT_COUNT", 500);
    std::vector<char> payload (std::max<size_t> (1, msg_size), '\xA7');
    stream_dispatch_packet_t packet;

    stopwatch_t sw;
    sw.start ();
    for (int i = 0; i < lat_count; ++i) {
        if (payload.size () >= 4) {
            const uint32_t seq = static_cast<uint32_t> (i + 1);
            std::memcpy (&payload[0], &seq, sizeof (seq));
        }

        zlink_routing_id_t rid;
        std::memset (&rid, 0, sizeof (rid));
        rid.size = static_cast<uint8_t> (sender.routing_id.size ());
        std::memcpy (rid.data, sender.routing_id.data (), sender.routing_id.size ());

        const auto deadline =
          std::chrono::steady_clock::now ()
          + std::chrono::milliseconds (std::max (1, io_timeout_ms));
        bool sent_ok = false;
        while (std::chrono::steady_clock::now () < deadline) {
            const int rc = zlink_stream_send (
              sender.socket,
              &rid,
              payload.empty () ? NULL : payload.data (),
              payload.size (),
              ZLINK_DONTWAIT);
            if (rc == static_cast<int> (payload.size ())) {
                sent_ok = true;
                break;
            }
            if (rc >= 0)
                break;

            const int err = zlink_errno ();
            if (err != EAGAIN && err != EINTR)
                break;
            std::this_thread::sleep_for (std::chrono::microseconds (50));
        }
        if (!sent_ok)
            return 0.0;

        if (!pop_stream_len32be_packet_aux (
              sender.socket, io_timeout_ms, false, packet)
            || packet.payload.size () != payload.size ()
            || !std::equal (
              packet.payload.begin (), packet.payload.end (), payload.begin ())) {
            return 0.0;
        }
    }

    return (sw.elapsed_ms () * 1000.0) / std::max (1, lat_count * 2);
}

} // namespace

int run_multi_stream_server_only (const std::string &transport,
                                  size_t msg_size,
                                  const std::string &lib_name)
{
    (void) msg_size;

    if (!transport_available (transport))
        return 1;
    if (transport != "tcp" && transport != "tls" && transport != "ws"
        && transport != "wss")
        return 1;

    ctx_guard_t ctx;
    if (!ctx.valid ())
        return 1;

    void *server = zlink_socket (ctx.get (), ZLINK_STREAM);
    if (!server)
        return 1;

    const int linger_ms = 0;
    set_sockopt_int (server, ZLINK_LINGER, linger_ms, "ZLINK_LINGER");
    const int stream_hwm = resolve_multi_int_env ("BENCH_STREAM_HWM", 100000, 1);
    apply_benchmark_hwm (server, stream_hwm);
    apply_stream_server_tuning (server, true);
    if (!setup_tls_server (server, transport)) {
        zlink_close (server);
        return 1;
    }

    const int io_timeout_ms = resolve_bench_count ("BENCH_STREAM_TIMEOUT_MS", 5000);
    set_sockopt_int (server, ZLINK_SNDTIMEO, io_timeout_ms, "ZLINK_SNDTIMEO");
    set_sockopt_int (server, ZLINK_RCVTIMEO, io_timeout_ms, "ZLINK_RCVTIMEO");

    const std::string endpoint = bind_and_resolve_endpoint (
      server, transport, lib_name + "_multi_stream_server");
    if (endpoint.empty ()) {
        zlink_close (server);
        return 1;
    }

    const multi_bench_settings_t settings = resolve_multi_bench_settings ();
    const int stream_send_batch =
      resolve_multi_int_env ("BENCH_MULTI_STREAM_SEND_BATCH", 64, 1);
    const int relay_budget = std::max (settings.recv_batch, stream_send_batch);
    const size_t max_pending_replies =
      std::max<size_t> (static_cast<size_t> (relay_budget) * static_cast<size_t> (16),
                        static_cast<size_t> (1024));

    stream_len32be_dispatch_t dispatch;
    bool dispatch_started = false;
    const multi_stream_dispatch_mode_t dispatch_mode =
      resolve_multi_stream_dispatch_mode ();
    const int dispatch_flags = multi_stream_dispatch_flags (dispatch_mode);
    const bool dispatch_requested =
      multi_stream_dispatch_enabled (dispatch_mode);
    const bool dispatch_supported =
      dispatch_requested && stream_dispatch_supported ();
    const bool send_requires_framed_payload =
      dispatch_mode != multi_stream_dispatch_len32be;
    if (dispatch_requested && dispatch_supported) {
        if (!start_stream_len32be_dispatch (server, dispatch, dispatch_flags)) {
            zlink_close (server);
            return 1;
        }
        dispatch_started = true;
        g_stream_reply_direct.store (false, std::memory_order_release);
    }

    std::atomic<bool> stop_requested (false);
    std::thread stdin_watcher ([&stop_requested] () {
        std::string line;
        while (std::getline (std::cin, line)) {
            if (line == "STOP" || line == "QUIT") {
                stop_requested.store (true, std::memory_order_release);
                return;
            }
        }
        stop_requested.store (true, std::memory_order_release);
    });
    stdin_watcher.detach ();

    std::cout << "READY," << endpoint << std::endl;

    stream_reply_queue_t pending_replies;
    stream_stash_map_t dispatch_stashes;
    int rc = 0;
    while (!stop_requested.load (std::memory_order_acquire)) {
        const int enqueued = enqueue_stream_payload_chunks (
          server,
          pending_replies,
          relay_budget,
          10,
          max_pending_replies,
          0,
          send_requires_framed_payload,
          &dispatch_stashes);
        if (enqueued < 0) {
            rc = 1;
            break;
        }

        const int flushed =
          flush_stream_reply_queue (server, pending_replies, relay_budget);
        if (flushed < 0) {
            rc = 1;
            break;
        }
    }

    if (dispatch_started) {
        stop_stream_len32be_dispatch (dispatch);
        g_stream_reply_direct.store (false, std::memory_order_release);
    }

    zlink_close (server);
    return rc;
}

void run_multi_stream (const std::string &transport,
                       size_t msg_size,
                       int /*msg_count*/,
                       const std::string &lib_name)
{
    if (!transport_available (transport))
        return;

    const std::vector<size_t> msg_sizes = resolve_bench_msg_sizes (msg_size);
    if (transport != "tcp" && transport != "tls" && transport != "ws"
        && transport != "wss") {
        for (size_t s = 0; s < msg_sizes.size (); ++s) {
            print_prep_result (
              lib_name, multi_stream_pattern_name (), transport, msg_sizes[s], 0.0, 0.0);
            print_result (
              lib_name, multi_stream_pattern_name (), transport, msg_sizes[s], 0.0, 0.0);
        }
        return;
    }
    const bool use_raw_tcp_sender =
      transport == "tcp"
      && resolve_multi_int_env ("BENCH_MULTI_STREAM_USE_RAW_TCP", 0, 0) != 0;
    const bool strict_ready_wait =
      resolve_multi_int_env ("BENCH_MULTI_STREAM_STRICT_READY", 0, 0) != 0;

    multi_bench_settings_t settings = resolve_multi_bench_settings ();
    if (transport != "tcp" && !use_raw_tcp_sender) {
        const int non_tcp_clients_max = resolve_multi_int_env (
          "PERF_MULTI_STREAM_NON_TCP_CLIENTS_MAX", 1000, 1);
        if (settings.clients > static_cast<size_t> (non_tcp_clients_max)) {
            if (bench_debug_enabled ()) {
                std::fprintf (
                  stderr,
                  "MULTI_STREAM non-tcp clients capped: requested=%zu capped=%d\n",
                  settings.clients,
                  non_tcp_clients_max);
            }
            settings.clients = static_cast<size_t> (non_tcp_clients_max);
        }
    }
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
              lib_name, multi_stream_pattern_name (), transport, msg_sizes[s], 0.0, 0.0);
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
    if (!setup_tls_server (server, transport)) {
        zlink_close (server);
        return;
    }

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
    bool dispatch_fallback_logged = false;
    std::vector<stream_sender_state_t> senders (settings.clients);
    stream_client_poller_t stream_poller;
    bool ready_wait_done = false;
    bool poller_ready = false;
    stream_reply_queue_t pending_replies;
    stream_stash_map_t dispatch_stashes;
    const int relay_budget = std::max (settings.recv_batch, stream_send_batch);
    const size_t min_pending_replies =
      static_cast<size_t> (relay_budget) * static_cast<size_t> (8);
    const multi_stream_dispatch_mode_t dispatch_mode =
      resolve_multi_stream_dispatch_mode ();
    const int dispatch_flags = multi_stream_dispatch_flags (dispatch_mode);
    const bool dispatch_requested =
      multi_stream_dispatch_enabled (dispatch_mode);
    std::vector<double> throughput_values (msg_sizes.size (), 0.0);
    std::vector<double> latency_values (msg_sizes.size (), 0.0);
    std::vector<double> prep_connect_values (msg_sizes.size (), 0.0);
    std::vector<double> prep_ready_values (msg_sizes.size (), 0.0);
    size_t completed_sizes = 0;
    bool run_failed = false;
    const bool dispatch_supported =
      dispatch_requested && stream_dispatch_supported ();
    auto close_throughput_senders = [&] () {
        for (size_t i = 0; i < senders.size (); ++i)
            close_sender (senders[i]);
        stream_poller.reset ();
        pending_replies.clear ();
    };
    g_stream_reply_direct.store (false, std::memory_order_release);
    for (size_t s = 0; s < msg_sizes.size (); ++s) {
        const size_t current_size = msg_sizes[s];
        multi_bench_settings_t round_settings = stream_settings;
        round_settings.inflight =
          resolve_multi_stream_inflight_for_size (stream_settings, current_size);
        const size_t max_pending_replies =
          std::max<size_t> (
            min_pending_replies,
            static_cast<size_t> (std::max<size_t> (1, settings.clients))
              * static_cast<size_t> (std::max (1, round_settings.inflight))
              * static_cast<size_t> (2));
        std::vector<char> payload (std::max<size_t> (1, current_size), 'a');
        pending_replies.clear ();
        dispatch_stashes.clear ();
        if (dispatch_started)
            clear_stream_len32be_dispatch_packets (dispatch);

        const auto pre_start = [&] () {
            if (!ready_wait_done) {
                ready_wait_done = true;
                if (strict_ready_wait) {
                    size_t observed_ready = 0;
                    const bool ready_ok =
                      wait_stream_ready_count (server_monitor, settings.clients,
                                               settings.connect_ready_timeout_ms,
                                               &observed_ready);
                    if (!ready_ok) {
                        if (bench_debug_enabled ()) {
                            std::fprintf (
                              stderr,
                              "%s: connect_ready wait timeout (expected=%zu observed=%zu)\n",
                              multi_stream_pattern_name (),
                              settings.clients, observed_ready);
                        }
                        return false;
                    }
                }
            }
            if (!dispatch_started && dispatch_supported) {
                if (!start_stream_len32be_dispatch (
                      server, dispatch, dispatch_flags)) {
                    if (bench_debug_enabled ()) {
                        std::fprintf (
                          stderr,
                          "%s: zlink_stream_attach_len32be failed\n",
                          multi_stream_pattern_name ());
                    }
                    return false;
                }
                dispatch_started = true;
                g_stream_reply_direct.store (
                  false, std::memory_order_release);
            } else if (dispatch_requested && !dispatch_supported
                       && !dispatch_fallback_logged
                       && bench_debug_enabled ()) {
                std::fprintf (
                  stderr,
                  "%s: stream attach unsupported, fallback recv path\n",
                  multi_stream_pattern_name ());
                dispatch_fallback_logged = true;
            }
            if (use_raw_tcp_sender && !poller_ready) {
                poller_ready = stream_poller.rebuild (senders);
                if (!poller_ready)
                    return false;
            }
            return true;
        };

        multi_bench_result_t bench;
        if (use_raw_tcp_sender) {
            bench = run_multi_phase_benchmark_with_sender_lifecycle_batched (
              settings.clients,
              round_settings,
              stream_send_batch,
              [&] (size_t idx) {
                  return setup_sender (
                    senders[idx],
                    ctx.get (),
                    transport,
                    endpoint,
                    stream_hwm,
                    io_timeout_ms,
                    settings.connect_ready_timeout_ms,
                    use_raw_tcp_sender);
              },
              [&] (size_t idx) {
                  return send_sender_nonblocking (senders[idx], payload);
              },
              [&] (multi_bench_phase_t phase_) {
                  const long relay_poll_ms =
                    phase_ == multi_phase_measure ? 10 : 0;
                  const int enqueued = enqueue_stream_payload_chunks (
                    server,
                    pending_replies,
                    relay_budget,
                    relay_poll_ms,
                    max_pending_replies,
                    current_size,
                    true,
                    &dispatch_stashes);
                  if (enqueued < 0)
                      return -1;
                  const int flushed = flush_stream_reply_queue (
                    server, pending_replies, relay_budget);
                  if (flushed < 0)
                      return -1;
                  const int echoed = recv_batch_stream_echoes (
                    senders, stream_poller, round_settings.recv_batch, 10, current_size);
                  if (bench_debug_enabled ()
                      && (enqueued > 0 || flushed > 0 || echoed > 0)) {
                      std::fprintf (
                        stderr,
                        "%s relay raw size=%zu enqueued=%d flushed=%d echoed=%d pending=%zu\n",
                        multi_stream_pattern_name (),
                        current_size,
                        enqueued,
                        flushed,
                        echoed,
                        pending_replies.size ());
                  }
                  return echoed;
              },
              [&] (size_t) {},
              pre_start,
              false);
        } else {
            bench = run_multi_phase_benchmark_with_sender_lifecycle_batched (
              settings.clients,
              round_settings,
              stream_send_batch,
              [&] (size_t idx) {
                  return setup_sender (
                    senders[idx],
                    ctx.get (),
                    transport,
                    endpoint,
                    stream_hwm,
                    io_timeout_ms,
                    settings.connect_ready_timeout_ms,
                    use_raw_tcp_sender);
              },
              [&] (size_t idx) {
                  return send_sender_nonblocking (senders[idx], payload);
              },
              [&] (multi_bench_phase_t phase_) {
                  const long relay_poll_ms =
                    phase_ == multi_phase_measure ? 10 : 0;
                  const int enqueued = enqueue_stream_payload_chunks (
                    server,
                    pending_replies,
                    relay_budget,
                    relay_poll_ms,
                    max_pending_replies,
                    current_size,
                    false,
                    &dispatch_stashes);
                  if (enqueued < 0)
                      return -1;
                  const int flushed = flush_stream_reply_queue (
                    server, pending_replies, relay_budget);
                  if (flushed < 0)
                      return -1;
                  if (bench_debug_enabled () && (enqueued > 0 || flushed > 0)) {
                      std::fprintf (
                        stderr,
                        "%s relay zlink size=%zu enqueued=%d flushed=%d pending=%zu\n",
                        multi_stream_pattern_name (),
                        current_size,
                        enqueued,
                        flushed,
                        pending_replies.size ());
                  }
                  return enqueued;
              },
              [&] (size_t idx) { close_sender (senders[idx]); },
              pre_start,
              false);
        }

        if (bench.failed) {
            run_failed = true;
            break;
        }

        if (bench_debug_enabled ()) {
            std::fprintf (
              stderr,
              "%s round size=%zu connect_ms=%.2f ready_ms=%.2f warmup_recv=%ld measure_recv=%ld send_ok=%ld drain_recv=%ld\n",
              multi_stream_pattern_name (),
              current_size,
              bench.connect_ms,
              bench.ready_wait_ms,
              bench.warmup_recv,
              bench.measure_recv,
              bench.measure_send_ok,
              bench.drain_recv);
        }

        prep_connect_values[s] = bench.connect_ms;
        prep_ready_values[s] = bench.ready_wait_ms;
        throughput_values[s] =
          bench.measure_recv > 0
            ? static_cast<double> (bench.measure_recv)
                / static_cast<double> (std::max (1, round_settings.duration_seconds))
            : 0.0;
        completed_sizes = s + 1;
    }

    close_throughput_senders ();
    stop_stream_len32be_dispatch (dispatch);
    close_connect_monitor (server_monitor);
    zlink_close (server);

    if (run_failed || completed_sizes == 0)
        return;

    const bool enable_live_latency =
      resolve_multi_int_env ("BENCH_MULTI_STREAM_ENABLE_LIVE_LATENCY", 0, 0) != 0;
    if (!enable_live_latency) {
        for (size_t s = 0; s < completed_sizes; ++s) {
            if (throughput_values[s] > 0.0)
                latency_values[s] = 1000000.0 / throughput_values[s];
        }
    }

    if (enable_live_latency && completed_sizes > 0) {
        void *lat_server = zlink_socket (ctx.get (), ZLINK_STREAM);
        if (lat_server) {
            set_sockopt_int (lat_server, ZLINK_LINGER, linger_ms, "ZLINK_LINGER");
            apply_benchmark_hwm (lat_server, stream_hwm);
            apply_stream_server_tuning (lat_server, true);
            set_sockopt_int (
              lat_server, ZLINK_SNDTIMEO, io_timeout_ms, "ZLINK_SNDTIMEO");
            set_sockopt_int (
              lat_server, ZLINK_RCVTIMEO, io_timeout_ms, "ZLINK_RCVTIMEO");
            if (!setup_tls_server (lat_server, transport)) {
                zlink_close (lat_server);
                lat_server = NULL;
            }

            const std::string lat_endpoint = lat_server
                                               ? bind_and_resolve_endpoint (
                                                   lat_server,
                                                   transport,
                                                   lib_name
                                                     + "_multi_stream_lat_phase")
                                               : std::string ();
            if (lat_server && !lat_endpoint.empty ()) {
                connect_monitor_t lat_monitor;
                const bool has_lat_monitor =
                  open_connect_monitor (lat_server, lat_monitor);
                stream_len32be_dispatch_t lat_dispatch;
                bool lat_dispatch_started = false;
                if (dispatch_supported) {
                    g_stream_reply_direct.store (true, std::memory_order_release);
                    lat_dispatch_started = start_stream_len32be_dispatch (
                      lat_server, lat_dispatch, dispatch_flags);
                } else if (bench_debug_enabled ()) {
                    std::fprintf (
                      stderr,
                      "MULTI_STREAM latency live skipped: stream attach unsupported\n");
                }

                if (lat_dispatch_started) {
                    stream_sender_state_t latency_sender;
                    if (setup_sender (
                          latency_sender,
                          ctx.get (),
                          transport,
                          lat_endpoint,
                          stream_hwm,
                          io_timeout_ms,
                          settings.connect_ready_timeout_ms,
                          use_raw_tcp_sender)) {
                        if (!has_lat_monitor) {
                            if (bench_debug_enabled ()) {
                                std::fprintf (
                                  stderr,
                                  "MULTI_STREAM latency live fail: open_connect_monitor\n");
                            }
                            close_sender (latency_sender);
                            stop_stream_len32be_dispatch (lat_dispatch);
                            if (lat_dispatch_started) {
                                g_stream_reply_direct.store (
                                  false, std::memory_order_release);
                            }
                            zlink_close (lat_server);
                            return;
                        }

                        size_t observed_ready = 0;
                        const bool ready_ok =
                          !strict_ready_wait
                            || wait_stream_ready_count (
                              lat_monitor, 1, settings.connect_ready_timeout_ms,
                              &observed_ready);
                        if (!ready_ok && bench_debug_enabled ()) {
                            std::fprintf (
                              stderr,
                              "MULTI_STREAM latency connect_ready timeout: expected=1 observed=%zu\n",
                              observed_ready);
                        }
                        if (!ready_ok) {
                            close_sender (latency_sender);
                            close_connect_monitor (lat_monitor);
                            stop_stream_len32be_dispatch (lat_dispatch);
                            if (lat_dispatch_started) {
                                g_stream_reply_direct.store (
                                  false, std::memory_order_release);
                            }
                            zlink_close (lat_server);
                            return;
                        }
                        if (use_raw_tcp_sender) {
                            drain_sender_echoes (latency_sender);
                            for (size_t s = 0; s < completed_sizes; ++s) {
                                latency_values[s] = measure_stream_latency_live (
                                  lat_server, latency_sender, msg_sizes[s], io_timeout_ms);
                            }
                        } else {
                            stream_len32be_dispatch_t lat_client_dispatch;
                            if (start_stream_len32be_dispatch_aux (
                                  latency_sender.socket, lat_client_dispatch,
                                  dispatch_flags)) {
                                clear_stream_len32be_dispatch_packets (
                                  lat_client_dispatch);
                                for (size_t s = 0; s < completed_sizes; ++s) {
                                    latency_values[s] =
                                      measure_stream_latency_live_zlink_dispatch (
                                        latency_sender, msg_sizes[s], io_timeout_ms);
                                }
                                stop_stream_len32be_dispatch_aux (lat_client_dispatch);
                            } else if (bench_debug_enabled ()) {
                                std::fprintf (
                                  stderr,
                                  "MULTI_STREAM latency live fail: start_client_dispatch\n");
                            }
                        }
                        close_sender (latency_sender);
                    } else {
                        if (bench_debug_enabled ()) {
                            std::fprintf (
                              stderr,
                              "MULTI_STREAM latency live fail: setup_latency_sender\n");
                        }
                        if (has_lat_monitor)
                            close_connect_monitor (lat_monitor);
                        stop_stream_len32be_dispatch (lat_dispatch);
                        if (lat_dispatch_started) {
                            g_stream_reply_direct.store (
                              false, std::memory_order_release);
                        }
                        zlink_close (lat_server);
                        return;
                    }
                    stop_stream_len32be_dispatch (lat_dispatch);
                } else if (dispatch_supported) {
                    if (bench_debug_enabled ()) {
                        std::fprintf (
                          stderr,
                          "MULTI_STREAM latency live fail: start_latency_dispatch\n");
                    }
                    if (has_lat_monitor)
                        close_connect_monitor (lat_monitor);
                    zlink_close (lat_server);
                    return;
                }
                if (lat_dispatch_started)
                    g_stream_reply_direct.store (false, std::memory_order_release);
                if (has_lat_monitor)
                    close_connect_monitor (lat_monitor);
            }
            zlink_close (lat_server);
        }
    }

    for (size_t s = 0; s < msg_sizes.size (); ++s) {
        print_prep_result (
          lib_name, multi_stream_pattern_name (), transport, msg_sizes[s],
          prep_connect_values[s], prep_ready_values[s]);
        print_result (
          lib_name, multi_stream_pattern_name (), transport, msg_sizes[s],
          throughput_values[s], latency_values[s]);
    }
}

int main (int argc, char **argv)
{
    for (int i = 4; i < argc; ++i) {
        if (std::strcmp (argv[i], "--server-only") == 0) {
            if (argc < 4)
                return 1;
            char *end = NULL;
            const unsigned long parsed = std::strtoul (argv[3], &end, 10);
            const size_t msg_size =
              (end != argv[3] && parsed > 0) ? static_cast<size_t> (parsed) : 64;
            return run_multi_stream_server_only (argv[2], msg_size, argv[1]);
        }
    }
    return run_standard_bench_main (argc, argv, run_multi_stream);
}
