#include "../common/bench_common_zlink.hpp"
#include "../common/bench_common_multi.hpp"
#include <zlink.h>
#include <algorithm>
#include <cerrno>
#include <climits>
#include <cstdint>
#include <cstdlib>
#include <cstring>
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
static const size_t FRAME_PREFIX_SIZE = 4;
static const size_t MAX_STREAM_FRAME_SIZE = 16 * 1024 * 1024;

struct stream_buffer_t {
    std::vector<char> data;
    size_t offset;

    stream_buffer_t () : offset (0) {}

    size_t available () const { return data.size () - offset; }

    void append (const char *buf, size_t len)
    {
        if (len == 0)
            return;
        data.insert (data.end (), buf, buf + len);
    }

    void compact ()
    {
        if (offset == 0)
            return;
        if (offset >= data.size ()) {
            data.clear ();
            offset = 0;
            return;
        }
        if (offset > 4096) {
            data.erase (data.begin (), data.begin () + offset);
            offset = 0;
        }
    }

    void reset ()
    {
        data.clear ();
        offset = 0;
    }
};

typedef std::unordered_map<std::string, stream_buffer_t> stream_stash_map_t;

struct stream_decode_state_t {
    stream_stash_map_t stashes;
    size_t complete_frames;

    stream_decode_state_t () : complete_frames (0) {}
    void reset ()
    {
        stashes.clear ();
        complete_frames = 0;
    }
};

struct tcp_sender_state_t {
    socket_t fd;
    std::vector<char> frame;
    size_t sent_bytes;

    tcp_sender_state_t () : fd (INVALID_SOCKET_FD), sent_bytes (0) {}
    bool valid () const { return fd != INVALID_SOCKET_FD; }
};

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
    return resolve_stream_socket_int_env ("BENCH_STREAM_TCP_NODELAY", 0, 0);
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
        if (n <= 0)
            return false;
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
}

bool setup_sender (tcp_sender_state_t &sender, const std::string &endpoint)
{
    if (sender.valid ())
        return true;

    socket_t fd = connect_tcp_socket (endpoint, false);
    if (fd == INVALID_SOCKET_FD)
        return false;
    if (!set_socket_nonblocking (fd)) {
        close_socket_fd (fd);
        return false;
    }

    sender.fd = fd;
    sender.frame.clear ();
    sender.sent_bytes = 0;
    return true;
}

multi_send_result_t send_sender_nonblocking (tcp_sender_state_t &sender,
                                             const std::vector<char> &payload)
{
    if (!sender.valid ())
        return multi_send_error;

    if (sender.frame.empty ()) {
        const uint32_t net_len = htonl (static_cast<uint32_t> (payload.size ()));
        sender.frame.resize (sizeof (net_len) + payload.size ());
        std::memcpy (&sender.frame[0], &net_len, FRAME_PREFIX_SIZE);
        if (!payload.empty ()) {
            std::memcpy (&sender.frame[0] + FRAME_PREFIX_SIZE,
                         &payload[0], payload.size ());
        }
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

bool is_stream_event_payload (const char *data, size_t size)
{
    return size == 1
           && (static_cast<unsigned char> (data[0]) == STREAM_EVENT_CONNECT
               || static_cast<unsigned char> (data[0]) == STREAM_EVENT_DISCONNECT);
}

int recv_batch_stream (void *server,
                       stream_decode_state_t &decode_state,
                       int recv_batch,
                       long poll_timeout_ms)
{
    if (recv_batch <= 0)
        return 0;

    int decoded = 0;
    if (decode_state.complete_frames > 0) {
        const size_t take =
          std::min<size_t> (
            static_cast<size_t> (recv_batch), decode_state.complete_frames);
        decode_state.complete_frames -= take;
        decoded = static_cast<int> (take);
        if (decoded >= recv_batch)
            return decoded;
    }

    zlink_pollitem_t item[] = {{server, 0, ZLINK_POLLIN, 0}};
    const int prc = zlink_poll (item, 1, poll_timeout_ms);
    if (prc < 0)
        return zlink_errno () == EINTR ? 0 : -1;
    if (prc == 0 || (item[0].revents & ZLINK_POLLIN) == 0)
        return decoded;

    while (decoded < recv_batch) {
        const int flags = decoded == 0 ? 0 : ZLINK_DONTWAIT;

        zlink_msg_t id_frame;
        zlink_msg_t payload_frame;
        zlink_msg_init (&id_frame);
        zlink_msg_init (&payload_frame);

        const int id_rc = zlink_msg_recv (&id_frame, server, flags);
        if (id_rc < 0) {
            zlink_msg_close (&id_frame);
            zlink_msg_close (&payload_frame);
            if (decoded > 0
                && (zlink_errno () == EAGAIN || zlink_errno () == EINTR))
                break;
            if (decoded == 0 && zlink_errno () == EINTR)
                return 0;
            return -1;
        }

        const int data_rc = zlink_msg_recv (&payload_frame, server, flags);
        if (data_rc < 0) {
            zlink_msg_close (&id_frame);
            zlink_msg_close (&payload_frame);
            if (decoded > 0
                && (zlink_errno () == EAGAIN || zlink_errno () == EINTR))
                break;
            if (decoded == 0 && zlink_errno () == EINTR)
                return 0;
            return -1;
        }

        const char *id_data =
          static_cast<const char *> (zlink_msg_data (&id_frame));
        const size_t id_size = zlink_msg_size (&id_frame);
        const char *payload_data =
          static_cast<const char *> (zlink_msg_data (&payload_frame));
        const size_t payload_size = zlink_msg_size (&payload_frame);
        if (payload_size > 0
            && payload_data
            && !is_stream_event_payload (payload_data, payload_size)) {
            if (!id_data || id_size == 0) {
                zlink_msg_close (&id_frame);
                zlink_msg_close (&payload_frame);
                continue;
            }

            std::string routing_id (id_data, id_size);
            stream_buffer_t &stash = decode_state.stashes[routing_id];
            stash.append (payload_data, payload_size);

            bool invalid_header = false;
            size_t decoded_now = 0;
            while (stash.available () >= FRAME_PREFIX_SIZE) {
                const unsigned char *ptr = reinterpret_cast<const unsigned char *> (
                  &stash.data[stash.offset]);
                const uint32_t net_len = (static_cast<uint32_t> (ptr[0]) << 24)
                                         | (static_cast<uint32_t> (ptr[1]) << 16)
                                         | (static_cast<uint32_t> (ptr[2]) << 8)
                                         | static_cast<uint32_t> (ptr[3]);
                const size_t single_frame_size = static_cast<size_t> (net_len);
                if (single_frame_size > MAX_STREAM_FRAME_SIZE) {
                    stash.reset ();
                    invalid_header = true;
                    break;
                }
                if (stash.available () < FRAME_PREFIX_SIZE + single_frame_size)
                    break;
                stash.offset += FRAME_PREFIX_SIZE + single_frame_size;
                ++decoded_now;
            }
            stash.compact ();
            if (decoded_now > 0)
                decode_state.complete_frames += decoded_now;
            else if (invalid_header)
                decode_state.complete_frames += 1;

            if (decode_state.complete_frames > 0) {
                const size_t take =
                  std::min<size_t> (
                    static_cast<size_t> (recv_batch - decoded),
                    decode_state.complete_frames);
                decode_state.complete_frames -= take;
                decoded += static_cast<int> (take);
            }
        }

        zlink_msg_close (&id_frame);
        zlink_msg_close (&payload_frame);
    }

    return decoded;
}

void drain_pending_stream_frames (void *server,
                                  stream_decode_state_t &decode_state)
{
    for (int i = 0; i < 16; ++i) {
        const int drained = recv_batch_stream (server, decode_state, 4096, 0);
        if (drained <= 0)
            break;
    }
    decode_state.reset ();
}

bool recv_one_stream_payload (void *server,
                              int timeout_ms,
                              std::vector<char> &routing_id_out,
                              std::vector<char> &payload_out)
{
    const auto deadline = std::chrono::steady_clock::now ()
                          + std::chrono::milliseconds (std::max (0, timeout_ms));

    while (std::chrono::steady_clock::now () < deadline) {
        const auto now = std::chrono::steady_clock::now ();
        const long remain_ms =
          std::chrono::duration_cast<std::chrono::milliseconds> (deadline - now)
            .count ();
        zlink_pollitem_t item[] = {{server, 0, ZLINK_POLLIN, 0}};
        const int prc = zlink_poll (item, 1, remain_ms > 0 ? remain_ms : 0);
        if (prc < 0) {
            if (zlink_errno () == EINTR)
                continue;
            return false;
        }
        if (prc == 0 || (item[0].revents & ZLINK_POLLIN) == 0)
            continue;

        for (;;) {
            zlink_msg_t id_frame;
            zlink_msg_t payload_frame;
            zlink_msg_init (&id_frame);
            zlink_msg_init (&payload_frame);

            const int id_rc = zlink_msg_recv (&id_frame, server, ZLINK_DONTWAIT);
            if (id_rc < 0) {
                zlink_msg_close (&id_frame);
                zlink_msg_close (&payload_frame);
                break;
            }

            const int data_rc =
              zlink_msg_recv (&payload_frame, server, ZLINK_DONTWAIT);
            if (data_rc < 0) {
                zlink_msg_close (&id_frame);
                zlink_msg_close (&payload_frame);
                break;
            }

            const char *id_data =
              static_cast<const char *> (zlink_msg_data (&id_frame));
            const size_t id_size = zlink_msg_size (&id_frame);
            const char *payload_data =
              static_cast<const char *> (zlink_msg_data (&payload_frame));
            const size_t payload_size = zlink_msg_size (&payload_frame);

            if (payload_size > 0
                && payload_data
                && !is_stream_event_payload (payload_data, payload_size)) {
                if (!id_data || id_size == 0) {
                    zlink_msg_close (&id_frame);
                    zlink_msg_close (&payload_frame);
                    continue;
                }
                routing_id_out.assign (id_data, id_data + id_size);
                payload_out.assign (payload_data, payload_data + payload_size);
                zlink_msg_close (&id_frame);
                zlink_msg_close (&payload_frame);
                return true;
            }

            zlink_msg_close (&id_frame);
            zlink_msg_close (&payload_frame);
        }
    }

    return false;
}

bool send_stream_payload (void *server,
                          const std::vector<char> &routing_id,
                          const std::vector<char> &payload)
{
    if (routing_id.empty ())
        return false;
    if (zlink_send (
          server, &routing_id[0], routing_id.size (), ZLINK_SNDMORE)
        < 0) {
        return false;
    }

    return zlink_send (
             server,
             payload.empty () ? "" : &payload[0],
             payload.size (),
             0)
           >= 0;
}

double measure_stream_latency_us (void *ctx,
                                  const std::string &transport,
                                  const std::string &lib_name,
                                  size_t msg_size,
                                  int hwm,
                                  int ready_timeout_ms)
{
    if (transport != "tcp")
        return 0.0;

    void *server = zlink_socket (ctx, ZLINK_STREAM);
    if (!server)
        return 0.0;

    const int linger_ms = 0;
    set_sockopt_int (server, ZLINK_LINGER, linger_ms, "ZLINK_LINGER");
    apply_benchmark_hwm (server, hwm);
    apply_stream_server_tuning (server, true);

    const int io_timeout_ms = resolve_bench_count ("BENCH_STREAM_TIMEOUT_MS", 5000);
    set_sockopt_int (server, ZLINK_SNDTIMEO, io_timeout_ms, "ZLINK_SNDTIMEO");
    set_sockopt_int (server, ZLINK_RCVTIMEO, io_timeout_ms, "ZLINK_RCVTIMEO");

    const std::string endpoint = bind_and_resolve_endpoint (
      server, transport, lib_name + "_multi_stream_lat");
    if (endpoint.empty ()) {
        zlink_close (server);
        return 0.0;
    }

    connect_monitor_t monitor;
    if (!open_connect_monitor (server, monitor)) {
        zlink_close (server);
        return 0.0;
    }

    socket_t client = connect_tcp_socket (endpoint, true);
    if (client == INVALID_SOCKET_FD
        || !wait_connect_ready_count (monitor, 1, ready_timeout_ms)) {
        if (client != INVALID_SOCKET_FD)
            close_socket_fd (client);
        close_connect_monitor (monitor);
        zlink_close (server);
        return 0.0;
    }
    close_connect_monitor (monitor);

    settle ();

    std::vector<char> payload (std::max<size_t> (1, msg_size), 'a');
    std::vector<char> recv_payload;
    std::vector<char> routing_id;

    const int lat_count = resolve_bench_count ("BENCH_LAT_COUNT", 500);
    stopwatch_t sw;
    sw.start ();
    for (int i = 0; i < lat_count; ++i) {
        if (!send_raw_framed (client, payload)
            || !recv_one_stream_payload (
              server, io_timeout_ms, routing_id, recv_payload)
            || !send_stream_payload (server, routing_id, recv_payload)
            || !recv_raw_framed (client, &recv_payload)) {
            close_socket_fd (client);
            zlink_close (server);
            return 0.0;
        }
    }

    close_socket_fd (client);
    zlink_close (server);
    return (sw.elapsed_ms () * 1000.0) / (lat_count * 2);
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
    multi_bench_settings_t stream_settings = settings;
    stream_settings.send_workers = std::max (
      stream_settings.send_workers,
      resolve_multi_int_env ("BENCH_MULTI_STREAM_SEND_WORKERS", 3, 1));
    stream_settings.drain_ms = std::max (
      stream_settings.drain_ms,
      resolve_multi_int_env ("BENCH_MULTI_STREAM_DRAIN_MS", 2000, 0));
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
    apply_benchmark_hwm (server, settings.hwm);
    apply_stream_server_tuning (server, false);

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

    std::vector<tcp_sender_state_t> senders (settings.clients);
    bool ready_wait_done = false;
    for (size_t s = 0; s < msg_sizes.size (); ++s) {
        const size_t current_size = msg_sizes[s];
        std::vector<char> payload (std::max<size_t> (1, current_size), 'a');
        stream_decode_state_t decode_state;

        const multi_bench_result_t bench =
          run_multi_phase_benchmark_with_sender_lifecycle_batched (
            settings.clients, stream_settings,
            stream_send_batch,
            [&] (size_t idx) { return setup_sender (senders[idx], endpoint); },
            [&] (size_t idx) { return send_sender_nonblocking (senders[idx], payload); },
            [&] (multi_bench_phase_t) {
                return recv_batch_stream (
                  server, decode_state, settings.recv_batch, 10);
            },
            [&] (size_t) {},
            [&] () {
                if (!ready_wait_done) {
                    ready_wait_done = true;
                    if (!wait_connect_ready_count (
                          server_monitor, settings.clients,
                          settings.connect_ready_timeout_ms)) {
                        return false;
                    }
                }
                drain_pending_stream_frames (server, decode_state);
                return true;
            },
            false);

        if (bench.failed) {
            print_prep_result (lib_name, "MULTI_STREAM", transport, current_size,
                               bench.connect_ms, bench.ready_wait_ms);
            break;
        }

        const double latency = measure_stream_latency_us (
          ctx.get (), transport, lib_name, current_size, settings.hwm,
          settings.connect_ready_timeout_ms);

        const double throughput =
          bench.measure_recv > 0
            ? static_cast<double> (bench.measure_recv)
                / static_cast<double> (std::max (1, settings.measure_seconds))
            : 0.0;

        print_prep_result (lib_name, "MULTI_STREAM", transport, current_size,
                           bench.connect_ms, bench.ready_wait_ms);
        print_result (
          lib_name, "MULTI_STREAM", transport, current_size, throughput, latency);
    }

    for (size_t i = 0; i < senders.size (); ++i)
        close_sender (senders[i]);
    close_connect_monitor (server_monitor);
    zlink_close (server);
}

int main (int argc, char **argv)
{
    return run_standard_bench_main (argc, argv, run_multi_stream);
}
