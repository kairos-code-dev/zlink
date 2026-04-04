#include "perf_common.hpp"
#include "perf_common_multi.hpp"
#include "bench_resource.hpp"

#include <cerrno>
#include <csignal>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <deque>
#include <condition_variable>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#ifndef ZLINK_SOCKET_STREAM
#define ZLINK_SOCKET_STREAM ((zlink_socket_type_t) 0x1008)
#endif

namespace {

#ifndef PERF_MULTI_STREAM_PATTERN_NAME
#define PERF_MULTI_STREAM_PATTERN_NAME "MULTI_STREAM"
#endif

static const char *k_pattern = PERF_MULTI_STREAM_PATTERN_NAME;
static const char k_stop_token[] = "__zlink_perf_stop__";

// Uses perf_stop_requested() from perf_common.hpp
static void *g_server_socket = NULL;
static std::atomic<bool> g_queue_probe_pending (false);
static std::atomic<size_t> g_queue_probe_size (0);
static std::atomic<unsigned long long> g_stream_recv_count (0);
static std::atomic<unsigned long long> g_stream_send_count (0);
static std::atomic<unsigned long long> g_stream_pending_count (0);
static std::mutex g_pending_send_queue_sync;

struct queued_stream_message_t
{
    zlink_routing_id_t routing_id;
    zlink_msg_t msg;

    queued_stream_message_t ()
    {
        memset (&routing_id, 0, sizeof (routing_id));
        if (zlink_msg_init (&msg) != 0)
            std::abort ();
    }

    ~queued_stream_message_t () { (void) zlink_msg_close (&msg); }

    queued_stream_message_t (queued_stream_message_t &&other) noexcept
    {
        memset (&routing_id, 0, sizeof (routing_id));
        if (zlink_msg_init (&msg) != 0)
            std::abort ();
        routing_id = other.routing_id;
        if (zlink_msg_move (&msg, &other.msg) != 0)
            std::abort ();
    }

    queued_stream_message_t &operator= (queued_stream_message_t &&other) noexcept
    {
        if (this == &other)
            return *this;

        routing_id = other.routing_id;
        (void) zlink_msg_close (&msg);
        if (zlink_msg_init (&msg) != 0)
            std::abort ();
        if (zlink_msg_move (&msg, &other.msg) != 0)
            std::abort ();
        return *this;
    }

    bool assign (const zlink_routing_id_t *rid_, zlink_msg_t *msg_)
    {
        if (!rid_ || !msg_)
            return false;

        routing_id = *rid_;
        (void) zlink_msg_close (&msg);
        if (zlink_msg_init (&msg) != 0)
            return false;
        return zlink_msg_move (&msg, msg_) == 0;
    }

  private:
    queued_stream_message_t (const queued_stream_message_t &);
    queued_stream_message_t &operator= (const queued_stream_message_t &);
};

inline uint32_t load_u32_be (const unsigned char *src)
{
    return (static_cast<uint32_t> (src[0]) << 24)
           | (static_cast<uint32_t> (src[1]) << 16)
           | (static_cast<uint32_t> (src[2]) << 8)
           | static_cast<uint32_t> (src[3]);
}

inline std::string routing_id_key (const zlink_routing_id_t *rid)
{
    if (!rid || rid->size == 0)
        return std::string ();
    return std::string (reinterpret_cast<const char *> (rid->data), rid->size);
}

static std::deque<queued_stream_message_t> g_pending_send_queue;

inline void request_queue_probe (size_t msg_size)
{
    if (msg_size == 0)
        return;
    g_queue_probe_size.store (msg_size, std::memory_order_release);
    g_queue_probe_pending.store (true, std::memory_order_release);
}

inline void emit_requested_queue_probe (const std::string &lib_name,
                                        const std::string &transport,
                                        void *send_socket,
                                        void *recv_socket)
{
    if (!g_queue_probe_pending.exchange (false, std::memory_order_acq_rel))
        return;

    const size_t msg_size = g_queue_probe_size.load (std::memory_order_acquire);
    if (msg_size == 0 || !send_socket || !recv_socket)
        return;

    const server_queue_stats_t queue_stats =
      sample_server_queue_stats (send_socket, recv_socket);
    print_server_queue_metrics (
      lib_name, k_pattern, transport, msg_size, queue_stats);
}

inline bool is_stream_event_payload (const unsigned char *data, size_t size)
{
    (void) data;
    return size == 0;
}

inline bool is_stop_token_payload (const unsigned char *data, size_t size)
{
    return data && size == (sizeof (k_stop_token) - 1)
           && std::memcmp (data, k_stop_token, sizeof (k_stop_token) - 1) == 0;
}

inline void print_server_metrics (
  const std::string &lib_name,
  const std::string &transport,
  const std::vector<size_t> &sizes,
  const bench_resource_metrics_t &metrics,
  const server_queue_stats_t &queue_stats)
{
    for (size_t i = 0; i < sizes.size (); ++i) {
        if (metrics.has_cpu_pct) {
            std::cout << "RESULT," << lib_name << "," << k_pattern << ","
                      << transport << "," << sizes[i]
                      << ",server_cpu_pct," << std::fixed
                      << std::setprecision (2) << metrics.cpu_pct << std::endl;
        }
        if (metrics.has_mem_mb) {
            std::cout << "RESULT," << lib_name << "," << k_pattern << ","
                      << transport << "," << sizes[i]
                      << ",server_mem_mb," << std::fixed
                      << std::setprecision (2) << metrics.mem_mb << std::endl;
        }
        print_server_queue_metrics (
          lib_name,
          k_pattern,
          transport,
          sizes[i],
          queue_stats);
    }
}

enum stream_send_result_t
{
    stream_send_sent = 0,
    stream_send_pending = 1,
    stream_send_failed = 2
};

inline stream_send_result_t
try_send_stream_message (queued_stream_message_t &queued_)
{
    if (!g_server_socket)
        return stream_send_failed;

    const int send_rc = zlink_send_rid (
      g_server_socket, &queued_.routing_id, &queued_.msg, 1, ZLINK_DONTWAIT);
    if (send_rc == 0) {
        return stream_send_sent;
    }

    const int err = zlink_errno ();
    if (err == EAGAIN)
        return stream_send_pending;
    if (bench_debug_enabled ()) {
        std::cerr << "[multi-stream-server] send_rid failed err=" << err
                  << " rid_size=" << queued_.routing_id.size << std::endl;
    }
    return stream_send_failed;
}

bool enqueue_stream_message (const zlink_routing_id_t *rid_, zlink_msg_t *msg_)
{
    if (!rid_ || !msg_)
        return false;

    queued_stream_message_t queued;
    if (!queued.assign (rid_, msg_)) {
        return false;
    }

    g_stream_pending_count.fetch_add (1, std::memory_order_relaxed);

    {
        std::lock_guard<std::mutex> lock (g_pending_send_queue_sync);
        g_pending_send_queue.push_back (std::move (queued));
    }
    return true;
}

void drain_stream_pending_queue ()
{
    while (true) {
        queued_stream_message_t queued;
        {
            std::lock_guard<std::mutex> lock (g_pending_send_queue_sync);
            if (g_pending_send_queue.empty ())
                return;
            queued = std::move (g_pending_send_queue.front ());
            g_pending_send_queue.pop_front ();
        }

        stream_send_result_t send_result = try_send_stream_message (queued);
        if (send_result == stream_send_sent) {
            g_stream_send_count.fetch_add (1, std::memory_order_relaxed);
            const unsigned long long pending_before =
              g_stream_pending_count.load (std::memory_order_relaxed);
            if (pending_before > 0) {
                g_stream_pending_count.fetch_sub (
                  1, std::memory_order_relaxed);
            }
            continue;
        }
        if (send_result == stream_send_pending) {
            std::lock_guard<std::mutex> lock (g_pending_send_queue_sync);
            g_pending_send_queue.push_front (std::move (queued));
            return;
        }
        perf_stop_requested ().store (true, std::memory_order_release);
        return;
    }
}

size_t pending_stream_send_count ()
{
    std::lock_guard<std::mutex> lock (g_pending_send_queue_sync);
    return g_pending_send_queue.size ();
}

bool process_stream_chunk (const zlink_routing_id_t *rid, zlink_msg_t *msg)
{
    if (!rid || !msg || !g_server_socket)
        return false;

    const unsigned char *payload =
      static_cast<const unsigned char *> (zlink_msg_data (msg));
    const size_t payload_size = zlink_msg_size (msg);
    if (is_stream_event_payload (payload, payload_size))
        return true;

    g_stream_recv_count.fetch_add (1, std::memory_order_relaxed);
    if (is_stop_token_payload (payload, payload_size)) {
        perf_stop_requested ().store (true, std::memory_order_release);
        return true;
    }

    return enqueue_stream_message (rid, msg);
}

bool process_stream_recv_parts (const zlink_routing_id_t *rid,
                                zlink_msg_t *parts,
                                size_t part_count)
{
    for (size_t i = 0; i < part_count; ++i) {
        const bool ok = process_stream_chunk (rid, &parts[i]);
        (void) zlink_msg_close (&parts[i]);
        if (!ok) {
            return false;
        }
    }
    return true;
}

bool drain_stream_recv_socket_once (void *server)
{
    if (!server)
        return false;

    while (!perf_stop_requested ().load (std::memory_order_acquire)) {
        zlink_routing_id_t source_rid;
        std::memset (&source_rid, 0, sizeof (source_rid));
        zlink_msg_t *parts = NULL;
        size_t part_count = 0;
        const int recv_rc =
          zlink_recv (server, &source_rid, &parts, &part_count, ZLINK_DONTWAIT);
        if (recv_rc == 0) {
            if (!process_stream_recv_parts (&source_rid, parts, part_count))
                return false;
            continue;
        }

        const int err = zlink_errno ();
        if (err == EAGAIN || err == EINTR)
            return true;
        return false;
    }

    return true;
}

} // namespace

int main (int argc, char **argv)
{
    if (argc < 3)
        return 1;
    if (!multi_perf_validate_recv_mode_for_pattern (k_pattern))
        return 1;

    const std::string lib_name = argv[1];
    const std::string transport = argv[2];
    set_perf_multi_pattern_env (k_pattern);

    if (!is_supported_transport (transport)) {
        std::cout << "UNSUPPORTED," << lib_name << "," << k_pattern << ","
                  << transport << std::endl;
        return 0;
    }

    if (!transport_available (transport)) {
        std::cerr << "transport unavailable: " << transport << std::endl;
        return 1;
    }

    ctx_guard_t ctx;
    if (!ctx.valid ()) {
        if (bench_debug_enabled ())
            std::cerr << "[multi-stream-server] ctx invalid" << std::endl;
        return 1;
    }

    void *server = zlink_socket (ctx.get (), ZLINK_SOCKET_STREAM);
    if (!server) {
        if (bench_debug_enabled ()) {
            std::cerr << "[multi-stream-server] socket create failed errno="
                      << zlink_errno () << std::endl;
        }
        return 1;
    }

    const bench_cpu_sample_t cpu_start = bench_capture_cpu_sample ();
    const bench_settings_t settings = resolve_bench_settings ();
    const std::vector<size_t> sizes = resolve_bench_msg_sizes (64);

    const int linger_ms = 0;
    set_sockopt_int (server, ZLINK_OPT_LINGER, linger_ms, "ZLINK_OPT_LINGER");
    apply_benchmark_hwm (server, settings.hwm);
    const int io_timeout_ms = resolve_bench_count ("PERF_STREAM_TIMEOUT_MS", 5000);
    set_sockopt_int (server, ZLINK_OPT_SNDTIMEO, io_timeout_ms,
                     "ZLINK_OPT_SNDTIMEO");
    set_sockopt_int (server, ZLINK_OPT_RCVTIMEO, io_timeout_ms,
                     "ZLINK_OPT_RCVTIMEO");
    const int nodelay = 1;
    set_sockopt_int (server, ZLINK_OPT_TCP_NODELAY, nodelay,
                     "ZLINK_OPT_TCP_NODELAY");

    if (!setup_tls_server (server, transport)) {
        if (bench_debug_enabled ()) {
            std::cerr << "[multi-stream-server] tls setup failed transport="
                      << transport << " errno=" << zlink_errno ()
                      << std::endl;
        }
        zlink_close (server);
        return 1;
    }

    const std::string endpoint = bind_server_endpoint (
      server, transport, lib_name + "_stream_server");
    if (endpoint.empty ()) {
        if (bench_debug_enabled ()) {
            std::cerr << "[multi-stream-server] bind endpoint failed errno="
                      << zlink_errno () << std::endl;
        }
        zlink_close (server);
        return 1;
    }

    perf_stop_requested ().store (false, std::memory_order_release);
    g_queue_probe_pending.store (false, std::memory_order_release);
    g_queue_probe_size.store (0, std::memory_order_release);
    g_stream_recv_count.store (0, std::memory_order_release);
    g_stream_send_count.store (0, std::memory_order_release);
    g_stream_pending_count.store (0, std::memory_order_release);
    g_server_socket = server;
    {
        std::lock_guard<std::mutex> queue_lock (g_pending_send_queue_sync);
        g_pending_send_queue.clear ();
    }
    install_perf_signal_handlers ();

    std::thread stdin_watcher ([] () {
        std::string line;
        while (std::getline (std::cin, line)) {
            size_t queue_size = 0;
            if (parse_queue_probe_command (line, &queue_size)) {
                request_queue_probe (queue_size);
                continue;
            }
            if (line == "STOP" || line == "QUIT") {
                perf_stop_requested ().store (true, std::memory_order_release);
                return;
            }
        }
        perf_stop_requested ().store (true, std::memory_order_release);
    });
    stdin_watcher.detach ();

    std::cout << "READY," << endpoint << std::endl;

    int rc = 0;
    while (!perf_stop_requested ().load (std::memory_order_acquire) && rc == 0) {
        emit_requested_queue_probe (lib_name, transport, server, server);
        zlink_pollitem_t item;
        std::memset (&item, 0, sizeof (item));
        item.socket = server;
        item.fd = 0;
        item.events = static_cast<short> (
          ZLINK_POLLIN
          | (pending_stream_send_count () > 0 ? ZLINK_POLLOUT : 0));
        item.revents = 0;

        const int poll_rc =
          perf_socket_poll (&item, 1, perf_aux_poll_wait_ms ());
        if (poll_rc < 0) {
            if (zlink_errno () == EINTR || zlink_errno () == EAGAIN)
                continue;
            rc = 1;
            break;
        }

        if ((item.revents & ZLINK_POLLIN) != 0
            && !drain_stream_recv_socket_once (server)) {
            rc = 1;
            break;
        }
        if (pending_stream_send_count () > 0
            && (((item.revents & ZLINK_POLLOUT) != 0)
                || ((item.revents & ZLINK_POLLIN) != 0))) {
            drain_stream_pending_queue ();
        }
    }

    g_server_socket = NULL;
    {
        std::lock_guard<std::mutex> queue_lock (g_pending_send_queue_sync);
        g_pending_send_queue.clear ();
    }

    const bench_resource_metrics_t metrics =
      bench_finish_resource_probe (cpu_start);
    const server_queue_stats_t queue_stats =
      sample_server_queue_stats (server, server);
    print_server_metrics (lib_name, transport, sizes, metrics, queue_stats);
    zlink_close (server);
    return rc;
}
