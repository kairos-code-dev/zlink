// STREAM multi server benchmark: poll-based echo relay.
// Topology: client STREAM(connect, N) -> server STREAM(bind, 1)
// Measurement role: echo incoming payloads back to the originating peer.

#include "../common/perf_common.hpp"
#include "../common/perf_entry.hpp"

#include <atomic>
#include <cerrno>
#include <csignal>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace {

static const char *k_pattern = "STREAM";

static std::atomic<bool> g_stop_requested (false);
static void *g_server_socket = NULL;
static std::atomic<bool> g_queue_probe_pending (false);
static std::atomic<size_t> g_queue_probe_size (0);

inline void on_signal (int)
{
    g_stop_requested.store (true, std::memory_order_release);
}

inline void install_signal_handlers ()
{
    std::signal (SIGINT, on_signal);
#if defined(SIGTERM)
    std::signal (SIGTERM, on_signal);
#endif
}

inline bool parse_queue_probe_command (const std::string &line,
                                       size_t *msg_size_out)
{
    if (!msg_size_out)
        return false;

    static const char k_prefix[] = "QUEUE,";
    if (line.compare (0, sizeof (k_prefix) - 1, k_prefix) != 0)
        return false;

    const char *value = line.c_str () + (sizeof (k_prefix) - 1);
    if (!value || *value == '\0')
        return false;

    errno = 0;
    char *end = NULL;
    const unsigned long parsed = std::strtoul (value, &end, 10);
    if (errno != 0 || end == value || !end || *end != '\0' || parsed == 0)
        return false;

    *msg_size_out = static_cast<size_t> (parsed);
    return true;
}

inline void request_queue_probe (size_t msg_size)
{
    if (msg_size == 0)
        return;
    g_queue_probe_size.store (msg_size, std::memory_order_release);
    g_queue_probe_pending.store (true, std::memory_order_release);
}

inline void emit_requested_queue_probe (const std::string &transport)
{
    if (!g_queue_probe_pending.exchange (false, std::memory_order_acq_rel))
        return;

    const size_t msg_size = g_queue_probe_size.load (std::memory_order_acquire);
    if (msg_size == 0)
        return;

    perf::multi::print_server_queue_metrics (
      "current", k_pattern, transport, msg_size,
      perf::multi::server_queue_stats_t ());
}

inline bool is_stream_event_payload (const unsigned char *data, size_t size)
{
    if (size == 0)
        return true;
    return data && size == 1 && (data[0] == 0x00 || data[0] == 0x01);
}

// ---------- pending message queue (uses raw zlink_msg_t for zero-copy) ------

enum send_status_t
{
    send_done = 0,
    send_blocked = 1,
    send_fatal = 2
};

struct pending_stream_message_t
{
    zlink_routing_id_t rid;
    zlink_msg_t payload;
    bool has_payload;

    pending_stream_message_t () : has_payload (false)
    {
        rid.size = 0;
        zlink_msg_init (&payload);
    }

    ~pending_stream_message_t ()
    {
        reset ();
        zlink_msg_close (&payload);
    }

    void reset ()
    {
        if (has_payload) {
            zlink_msg_close (&payload);
            zlink_msg_init (&payload);
            has_payload = false;
        }
        rid.size = 0;
    }

    bool move_from (pending_stream_message_t *other)
    {
        if (!other)
            return false;
        reset ();
        rid = other->rid;
        if (other->has_payload) {
            if (zlink_msg_move (&payload, &other->payload) != 0) {
                rid.size = 0;
                return false;
            }
            has_payload = true;
        }
        other->rid.size = 0;
        other->has_payload = false;
        return true;
    }

  private:
    pending_stream_message_t (const pending_stream_message_t &);
    pending_stream_message_t &operator= (const pending_stream_message_t &);
};

inline send_status_t try_send_stream_message (pending_stream_message_t *message)
{
    if (!g_server_socket || !message || !message->has_payload
        || message->rid.size == 0)
        return send_fatal;

    zlink_msg_t part;
    if (zlink_msg_init (&part) != 0)
        return send_fatal;
    if (zlink_msg_move (&part, &message->payload) != 0) {
        (void) zlink_msg_close (&part);
        return send_fatal;
    }

    const int rc =
      zlink_send_rid (g_server_socket, &message->rid, &part, 1, ZLINK_DONTWAIT);
    if (rc == 0) {
        (void) zlink_msg_close (&part);
        message->has_payload = false;
        message->rid.size = 0;
        return send_done;
    }

    if (zlink_msg_move (&message->payload, &part) != 0) {
        (void) zlink_msg_close (&part);
        return send_fatal;
    }
    (void) zlink_msg_close (&part);

    if (rc < 0) {
        const int err = zlink_errno ();
        if (err == EINTR || err == EAGAIN || err == ENOTCONN
            || err == EHOSTUNREACH || err == ETIMEDOUT)
            return send_blocked;
    }
    return send_fatal;
}

inline bool enqueue_pending_stream_message (pending_stream_message_t *pending,
                                            size_t capacity,
                                            size_t *count_io,
                                            pending_stream_message_t *message)
{
    if (!pending || !count_io || !message || *count_io >= capacity)
        return false;
    if (!pending[*count_io].move_from (message))
        return false;
    ++(*count_io);
    return true;
}

inline void erase_pending_stream_message (pending_stream_message_t *pending,
                                           size_t *count_io,
                                           size_t idx)
{
    if (!pending || !count_io || idx >= *count_io)
        return;
    const size_t last = *count_io - 1;
    pending[idx].reset ();
    if (idx != last)
        (void) pending[idx].move_from (&pending[last]);
    pending[last].reset ();
    --(*count_io);
}

inline bool flush_pending_stream_messages (pending_stream_message_t *pending,
                                            size_t *count_io)
{
    if (!pending || !count_io)
        return false;
    size_t idx = 0;
    while (idx < *count_io) {
        const send_status_t send_rc = try_send_stream_message (&pending[idx]);
        if (send_rc == send_done) {
            erase_pending_stream_message (pending, count_io, idx);
            continue;
        }
        if (send_rc == send_fatal)
            return false;
        ++idx;
    }
    return true;
}

// ---------- relay helpers ---------------------------------------------------

inline bool extract_stream_routing_id (const zlink_msg_t *id_frame,
                                        zlink_routing_id_t *rid_out)
{
    if (!id_frame || !rid_out)
        return false;

    const size_t id_size =
      zlink_msg_size (const_cast<zlink_msg_t *> (id_frame));
    if (id_size == 0 || id_size > sizeof (rid_out->data))
        return false;

    const unsigned char *id_data = static_cast<const unsigned char *> (
      zlink_msg_data (const_cast<zlink_msg_t *> (id_frame)));
    if (!id_data)
        return false;

    rid_out->size = static_cast<uint8_t> (id_size);
    std::memcpy (rid_out->data, id_data, id_size);
    return true;
}

enum relay_status_t
{
    relay_idle = 0,
    relay_progress = 1,
    relay_error = 2
};

inline relay_status_t relay_stream_message_non_blocking (
  void *server,
  pending_stream_message_t *pending,
  size_t pending_capacity,
  size_t *pending_count)
{
    if (!server)
        return relay_error;

    zlink_routing_id_t rid;
    rid.size = 0;
    zlink_msg_t *parts = NULL;
    size_t part_count = 0;

    int recv_rc = zlink_recv (server, &rid, &parts, &part_count, ZLINK_DONTWAIT);
    while (recv_rc < 0 && zlink_errno () == EINTR)
        recv_rc = zlink_recv (server, &rid, &parts, &part_count, ZLINK_DONTWAIT);
    if (recv_rc < 0) {
        const int err = zlink_errno ();
        if (err == EAGAIN || err == EINTR)
            return relay_idle;
        return relay_error;
    }
    if (rid.size == 0 || !parts || part_count != 1) {
        if (parts)
            zlink_multipart_close (parts, part_count);
        return relay_error;
    }

    bool ok = true;
    const unsigned char *payload = static_cast<const unsigned char *> (
      zlink_msg_data (&parts[0]));
    const size_t payload_size = zlink_msg_size (&parts[0]);
    if (!is_stream_event_payload (payload, payload_size)) {
        pending_stream_message_t request;
        request.rid = rid;
        if (zlink_msg_move (&request.payload, &parts[0]) != 0) {
            ok = false;
        } else {
            request.has_payload = true;
            const send_status_t send_rc = try_send_stream_message (&request);
            if (send_rc == send_blocked) {
                ok = enqueue_pending_stream_message (
                  pending, pending_capacity, pending_count, &request);
            } else if (send_rc == send_fatal) {
                ok = false;
            }
        }
    }

    zlink_multipart_close (parts, part_count);
    return ok ? relay_progress : relay_error;
}

inline bool relay_stream_once (void *server,
                                pending_stream_message_t *pending,
                                size_t pending_capacity,
                                size_t *pending_count,
                                int poll_timeout_ms)
{
    if (!server)
        return false;

    zlink_pollitem_t item[] = {{server, 0,
                                static_cast<short> (
                                  ZLINK_POLLIN
                                  | ((pending_count && *pending_count > 0)
                                       ? ZLINK_POLLOUT
                                       : 0)),
                                0}};
    int prc = zlink_poll (item, 1, poll_timeout_ms);
    if (prc < 0)
        return zlink_errno () == EINTR;
    if ((item[0].revents & ZLINK_POLLOUT) != 0 && pending_count
        && *pending_count > 0
        && !flush_pending_stream_messages (pending, pending_count)) {
        return false;
    }
    if (prc == 0 || (item[0].revents & ZLINK_POLLIN) == 0)
        return true;

    while (!g_stop_requested.load (std::memory_order_acquire)) {
        const relay_status_t status = relay_stream_message_non_blocking (
          server, pending, pending_capacity, pending_count);
        if (status == relay_error)
            return false;
        if (status == relay_idle)
            break;
    }

    return true;
}

} // namespace

bool perf_stream_server (const std::string &transport, size_t)
{
    perf::multi::set_perf_pattern_env (k_pattern);

    if (!perf::multi::is_supported_transport (transport)) {
        std::cout << "UNSUPPORTED,current," << k_pattern << "," << transport
                  << std::endl;
        return true;
    }

    const perf::multi::multi_bench_settings_t settings =
      perf::multi::resolve_multi_bench_settings ();

    perf::multi::ctx_guard_t ctx;
    perf::multi::socket_guard_t server (ctx, zlink::socket_type::stream);
    if (!server.valid ())
        return false;

    perf::multi::apply_benchmark_socket_options (
      server.sock (), settings, transport);

    const int io_timeout_ms =
      perf::multi::parse_positive_env ("PERF_STREAM_TIMEOUT_MS", 5000);
    (void) server.sock ().set_option (zlink::socket_options::sndtimeo,
                                      io_timeout_ms);
    (void) server.sock ().set_option (zlink::socket_options::rcvtimeo,
                                      io_timeout_ms);
    (void) server.sock ().set_option (zlink::socket_options::tcp_nodelay, 1);

    if (!perf::multi::setup_tls_server (server.sock (), transport))
        return false;

    const std::string endpoint = perf::multi::bind_and_resolve_endpoint (
      server.sock (), transport, "cpp_multi_stream",
      settings.server_bind_port);
    if (endpoint.empty ())
        return false;

    g_stop_requested.store (false, std::memory_order_release);
    g_queue_probe_pending.store (false, std::memory_order_release);
    g_queue_probe_size.store (0, std::memory_order_release);
    g_server_socket = server.sock ().handle ();
    install_signal_handlers ();

    std::thread stdin_watcher ([&transport] () {
        std::string line;
        while (std::getline (std::cin, line)) {
            size_t queue_size = 0;
            if (parse_queue_probe_command (line, &queue_size)) {
                request_queue_probe (queue_size);
                continue;
            }
            if (line == "STOP" || line == "QUIT") {
                g_stop_requested.store (true, std::memory_order_release);
                return;
            }
        }
        g_stop_requested.store (true, std::memory_order_release);
    });
    stdin_watcher.detach ();

    perf::multi::print_ready (endpoint);

    int rc = 0;
    const size_t pending_capacity = std::max<size_t> (
      64,
      std::max<size_t> (
        settings.clients,
        static_cast<size_t> (settings.hwm > 0 ? settings.hwm : 1))
        * 2);
    pending_stream_message_t *pending =
      new pending_stream_message_t[pending_capacity];
    size_t pending_count = 0;
    while (!g_stop_requested.load (std::memory_order_acquire)) {
        emit_requested_queue_probe (transport);
        if (!relay_stream_once (server.sock ().handle (), pending,
                                pending_capacity, &pending_count, 50)) {
            rc = 1;
            break;
        }
    }

    g_server_socket = NULL;
    delete[] pending;

    perf::multi::print_server_queue_metrics (
      "current", k_pattern, transport, 0,
      perf::multi::server_queue_stats_t ());
    return rc == 0;
}

int main (int argc, char **argv)
{
    if (argc < 2) {
        std::cerr << "usage: <transport> [size]" << std::endl;
        return 1;
    }

    const std::string transport = argv[1];
    const size_t size =
      argc >= 3
        ? static_cast<size_t> (std::strtoull (argv[2], NULL, 10))
        : 0;

    return perf_stream_server (transport, size) ? 0 : 1;
}
