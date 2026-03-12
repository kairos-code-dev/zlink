// STREAM_LEN32BE multi server benchmark: batched callback echo relay.
// Topology: client STREAM(connect, N) -> server STREAM(bind, 1)
// Measurement role: echo incoming payloads back via LEN32BE stream callback.

#include "../common/perf_common.hpp"
#include "../common/perf_entry.hpp"

#include <atomic>
#include <cerrno>
#include <csignal>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>

namespace {

static const char *k_pattern = "STREAM_LEN32BE";

static std::atomic<bool> g_stop_requested (false);
static std::atomic<bool> g_callback_failed (false);
static void *g_server_socket = NULL;
static std::atomic<bool> g_queue_probe_pending (false);
static std::atomic<size_t> g_queue_probe_size (0);
static std::mutex g_pending_mutex;

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

inline bool is_stream_event_payload (const unsigned char *, size_t size)
{
    return size == 0;
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

static pending_stream_message_t *g_pending_messages = NULL;
static size_t g_pending_capacity = 0;
static size_t g_pending_count = 0;

inline send_status_t try_send_stream_message (pending_stream_message_t *message)
{
    if (!g_server_socket || !message || !message->has_payload
        || message->rid.size == 0)
        return send_fatal;

    const size_t payload_size = zlink_msg_size (&message->payload);
    const int rc = zlink_stream_send_msg (
      g_server_socket, &message->rid, &message->payload, ZLINK_DONTWAIT);
    if (rc == static_cast<int> (payload_size)) {
        message->has_payload = false;
        message->rid.size = 0;
        return send_done;
    }
    if (rc >= 0)
        return send_fatal;

    const int err = zlink_errno ();
    if (err == EAGAIN || err == EINTR)
        return send_blocked;
    return send_fatal;
}

inline bool enqueue_pending_stream_message_locked (
  pending_stream_message_t *message)
{
    if (!message || !g_pending_messages || g_pending_count >= g_pending_capacity)
        return false;
    if (!g_pending_messages[g_pending_count].move_from (message))
        return false;
    ++g_pending_count;
    return true;
}

inline void erase_pending_stream_message_locked (size_t idx)
{
    if (!g_pending_messages || idx >= g_pending_count)
        return;
    const size_t last = g_pending_count - 1;
    g_pending_messages[idx].reset ();
    if (idx != last)
        (void) g_pending_messages[idx].move_from (&g_pending_messages[last]);
    g_pending_messages[last].reset ();
    --g_pending_count;
}

inline bool flush_pending_stream_messages_locked ()
{
    size_t idx = 0;
    while (idx < g_pending_count) {
        const send_status_t send_rc =
          try_send_stream_message (&g_pending_messages[idx]);
        if (send_rc == send_done) {
            erase_pending_stream_message_locked (idx);
            continue;
        }
        if (send_rc == send_fatal)
            return false;
        ++idx;
    }
    return true;
}

// ---------- LEN32BE batched stream callback ---------------------------------

int on_stream_packets (const zlink_routing_id_t *rid,
                        zlink_msg_t *msgs,
                        size_t msg_count)
{
    if (!rid || !msgs || msg_count == 0 || !g_server_socket)
        return 0;

    for (size_t i = 0; i < msg_count; ++i) {
        zlink_msg_t *msg = &msgs[i];
        const unsigned char *payload =
          static_cast<const unsigned char *> (zlink_msg_data (msg));
        const size_t payload_size = zlink_msg_size (msg);
        if (is_stream_event_payload (payload, payload_size)) {
            (void) zlink_msg_close (msg);
            continue;
        }
        pending_stream_message_t request;
        request.rid = *rid;
        if (zlink_msg_move (&request.payload, msg) != 0) {
            g_callback_failed.store (true, std::memory_order_release);
            (void) zlink_msg_close (msg);
            for (size_t j = i + 1; j < msg_count; ++j)
                (void) zlink_msg_close (&msgs[j]);
            return 1;
        }
        request.has_payload = true;
        const send_status_t send_rc = try_send_stream_message (&request);
        if (send_rc == send_done) {
            (void) zlink_msg_close (msg);
            continue;
        }
        if (send_rc == send_blocked) {
            std::lock_guard<std::mutex> guard (g_pending_mutex);
            if (!enqueue_pending_stream_message_locked (&request)) {
                g_callback_failed.store (true, std::memory_order_release);
                (void) zlink_msg_close (msg);
                for (size_t j = i + 1; j < msg_count; ++j)
                    (void) zlink_msg_close (&msgs[j]);
                return 1;
            }
            (void) zlink_msg_close (msg);
            continue;
        }

        g_callback_failed.store (true, std::memory_order_release);
        (void) zlink_msg_close (msg);
        for (size_t j = i + 1; j < msg_count; ++j)
            (void) zlink_msg_close (&msgs[j]);
        return 1;
    }

    return 0;
}

} // namespace

bool perf_stream_len32be_server (const std::string &transport, size_t)
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
    (void) server.sock ().set (zlink::socket_options::sndtimeo, io_timeout_ms);
    (void) server.sock ().set (zlink::socket_options::rcvtimeo, io_timeout_ms);
    (void) server.sock ().set (zlink::socket_options::tcp_nodelay, 1);

    if (!perf::multi::setup_tls_server (server.sock (), transport))
        return false;

    const std::string endpoint = perf::multi::bind_and_resolve_endpoint (
      server.sock (), transport, "cpp_multi_stream_len32be",
      settings.server_bind_port);
    if (endpoint.empty ())
        return false;

    g_stop_requested.store (false, std::memory_order_release);
    g_callback_failed.store (false, std::memory_order_release);
    g_queue_probe_pending.store (false, std::memory_order_release);
    g_queue_probe_size.store (0, std::memory_order_release);
    g_server_socket = server.sock ().handle ();
    g_pending_capacity = std::max<size_t> (
      64,
      std::max<size_t> (
        settings.clients,
        static_cast<size_t> (settings.hwm > 0 ? settings.hwm : 1))
        * 2);
    g_pending_messages = new pending_stream_message_t[g_pending_capacity];
    g_pending_count = 0;
    install_signal_handlers ();

    if (server.sock ().stream_attach_len32be (on_stream_packets) != 0) {
        g_server_socket = NULL;
        delete[] g_pending_messages;
        g_pending_messages = NULL;
        g_pending_capacity = 0;
        return false;
    }

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
    while (!g_stop_requested.load (std::memory_order_acquire)) {
        emit_requested_queue_probe (transport);
        if (g_callback_failed.load (std::memory_order_acquire)) {
            rc = 1;
            break;
        }

        bool have_pending = false;
        {
            std::lock_guard<std::mutex> guard (g_pending_mutex);
            have_pending = g_pending_count > 0;
        }
        if (!have_pending) {
            if (zlink_poll (NULL, 0, 50) < 0 && zlink_errno () != EINTR) {
                rc = 1;
                break;
            }
            continue;
        }

        zlink_pollitem_t item = {server.sock ().handle (), 0, ZLINK_POLLOUT, 0};
        const int prc = zlink_poll (&item, 1, 50);
        if (prc < 0) {
            if (zlink_errno () == EINTR)
                continue;
            rc = 1;
            break;
        }
        if (prc > 0 && (item.revents & ZLINK_POLLOUT) != 0) {
            std::lock_guard<std::mutex> guard (g_pending_mutex);
            if (!flush_pending_stream_messages_locked ()) {
                rc = 1;
                break;
            }
        }
    }

    (void) server.sock ().stream_detach ();
    delete[] g_pending_messages;
    g_pending_messages = NULL;
    g_pending_capacity = 0;
    g_pending_count = 0;
    g_server_socket = NULL;

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

    return perf_stream_len32be_server (transport, size) ? 0 : 1;
}
