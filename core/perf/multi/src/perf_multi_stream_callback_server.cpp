#include "../common/perf_common.hpp"
#include "../common/perf_common_multi.hpp"
#include "../../../bench/with_zmq/multi/common/bench_resource.hpp"
#include "../../../external/moodycamel/concurrentqueue.h"

#include <atomic>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#ifndef ZLINK_SOCKET_STREAM
#define ZLINK_SOCKET_STREAM ((zlink_socket_type_t) 0x1008)
#endif

namespace {

#ifndef PERF_MULTI_STREAM_PATTERN_NAME
#define PERF_MULTI_STREAM_PATTERN_NAME "STREAM"
#endif

static const char *k_pattern = PERF_MULTI_STREAM_PATTERN_NAME;
static const char k_stop_token[] = "__zlink_perf_stop__";
static const bool k_use_callback_mode =
  std::string (PERF_MULTI_STREAM_PATTERN_NAME) == "STREAM_CALLBACK";

// Uses perf_stop_requested() from perf_common.hpp
static std::atomic<bool> g_callback_failed (false);
static void *g_server_socket = NULL;
static std::atomic<bool> g_queue_probe_pending (false);
static std::atomic<size_t> g_queue_probe_size (0);
static std::atomic<bool> g_sender_stop_requested (false);
static std::atomic<size_t> g_pending_send_count (0);
static std::atomic<int> g_send_poll_timeout_ms (5000);

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
        if (zlink_msg_move (&msg, &other.msg) != 0)
            std::abort ();
        return *this;
    }

    bool assign (const zlink_routing_id_t *rid_, zlink_msg_t *msg_)
    {
        if (!rid_ || !msg_)
            return false;

        routing_id = *rid_;
        return zlink_msg_move (&msg, msg_) == 0;
    }

  private:
    queued_stream_message_t (const queued_stream_message_t &);
    queued_stream_message_t &operator= (const queued_stream_message_t &);
};

typedef moodycamel::ConcurrentQueue<queued_stream_message_t> send_queue_t;

static send_queue_t *g_send_queue = NULL;

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
    if (size == 0)
        return true;
    return data && size == 1 && (data[0] == 0x00 || data[0] == 0x01);
}

inline bool is_stream_control_payload (const unsigned char *data, size_t size)
{
    (void) data;
    // WS/WSS peer teardown can surface short control payloads that are not
    // benchmark data frames. Real len32be benchmark payloads are at least 4
    // bytes of frame prefix plus payload body.
    return size > 0 && size < 4;
}

inline bool is_stop_token_payload (const unsigned char *data, size_t size)
{
    if (!data)
        return false;

    if (size == (sizeof (k_stop_token) - 1))
        return std::memcmp (data, k_stop_token, sizeof (k_stop_token) - 1) == 0;

    if (size != (sizeof (k_stop_token) - 1 + 4))
        return false;

    const uint32_t declared = (static_cast<uint32_t> (data[0]) << 24)
                              | (static_cast<uint32_t> (data[1]) << 16)
                              | (static_cast<uint32_t> (data[2]) << 8)
                              | static_cast<uint32_t> (data[3]);
    return declared == (sizeof (k_stop_token) - 1)
           && std::memcmp (data + 4, k_stop_token, sizeof (k_stop_token) - 1)
                == 0;
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

inline bool wait_for_stream_send_ready (int timeout_ms)
{
    if (!g_server_socket)
        return false;

    const auto deadline =
      std::chrono::steady_clock::now ()
      + std::chrono::milliseconds (timeout_ms > 0 ? timeout_ms : 1);

    while (!perf_stop_requested ().load (std::memory_order_acquire)) {
        const auto now = std::chrono::steady_clock::now ();
        const long remain_ms =
          std::chrono::duration_cast<std::chrono::milliseconds> (deadline - now)
            .count ();
        if (remain_ms < 0)
            return false;

        zlink_pollitem_t item;
        std::memset (&item, 0, sizeof (item));
        item.socket = g_server_socket;
        item.fd = 0;
        item.events = ZLINK_POLLOUT;
        item.revents = 0;

        const int rc = zlink_poll (&item, 1, remain_ms > 0 ? remain_ms : 0);
        if (rc > 0 && (item.revents & ZLINK_POLLOUT) != 0)
            return true;
        if (rc < 0 && zlink_errno () != EINTR)
            return false;
    }

    return false;
}

inline bool send_stream_message_nonblocking (queued_stream_message_t &queued_)
{
    if (!g_server_socket)
        return false;

    const size_t payload_size = zlink_msg_size (&queued_.msg);
    const int send_timeout_ms =
      g_send_poll_timeout_ms.load (std::memory_order_acquire);

    while (!perf_stop_requested ().load (std::memory_order_acquire)) {
        const int rc = zlink_stream_send_msg (
          g_server_socket, &queued_.routing_id, &queued_.msg, ZLINK_DONTWAIT);
        if (rc == static_cast<int> (payload_size))
            return true;

        const int err = zlink_errno ();
        if (err != EAGAIN && err != EINTR)
            return false;
        if (!wait_for_stream_send_ready (send_timeout_ms))
            return false;
    }

    return false;
}

void send_thread_main ()
{
    if (!g_send_queue)
        return;

    moodycamel::ConsumerToken consumer (*g_send_queue);
    queued_stream_message_t queued_msg;

    for (;;) {
        bool made_progress = false;
        while (g_send_queue->try_dequeue (consumer, queued_msg)) {
            made_progress = true;
            const size_t before =
              g_pending_send_count.fetch_sub (1, std::memory_order_acq_rel);
            if (before == 0)
                g_pending_send_count.store (0, std::memory_order_release);

            if (!send_stream_message_nonblocking (queued_msg)) {
                g_callback_failed.store (true, std::memory_order_release);
                perf_stop_requested ().store (true, std::memory_order_release);
                g_sender_stop_requested.store (true, std::memory_order_release);
                return;
            }
        }

        if (g_sender_stop_requested.load (std::memory_order_acquire)
            && g_pending_send_count.load (std::memory_order_acquire) == 0) {
            return;
        }

        if (!made_progress)
            std::this_thread::sleep_for (std::chrono::milliseconds (1));
    }
}

int on_stream_packet (const zlink_routing_id_t *rid, zlink_msg_t *msg, void *)
{
    if (!rid || !msg || !g_server_socket || !g_send_queue)
        return 0;

    const unsigned char *payload =
      static_cast<const unsigned char *> (zlink_msg_data (msg));
    const size_t payload_size = zlink_msg_size (msg);
    if (is_stream_event_payload (payload, payload_size)) {
        (void) zlink_msg_close (msg);
        return 0;
    }
    if (is_stream_control_payload (payload, payload_size)) {
        (void) zlink_msg_close (msg);
        return 0;
    }
    if (is_stop_token_payload (payload, payload_size)) {
        perf_stop_requested ().store (true, std::memory_order_release);
        (void) zlink_msg_close (msg);
        return 0;
    }

    queued_stream_message_t queued_msg;
    if (!queued_msg.assign (rid, msg)) {
        g_callback_failed.store (true, std::memory_order_release);
        (void) zlink_msg_close (msg);
        return 1;
    }

    g_pending_send_count.fetch_add (1, std::memory_order_acq_rel);
    if (!g_send_queue->enqueue (std::move (queued_msg))) {
        g_pending_send_count.fetch_sub (1, std::memory_order_acq_rel);
        g_callback_failed.store (true, std::memory_order_release);
        (void) zlink_msg_close (msg);
        return 1;
    }

    (void) zlink_msg_close (msg);
    return 0;
}

bool process_stream_recv_parts (const zlink_routing_id_t *rid,
                                zlink_msg_t *parts,
                                size_t part_count)
{
    if (part_count > 0)
        (void) on_stream_packet (rid, &parts[0], NULL);
    for (size_t i = 1; i < part_count; ++i)
        (void) zlink_msg_close (&parts[i]);
    free (parts);
    return !g_callback_failed.load (std::memory_order_acquire);
}

void on_stream_handler (const zlink_routing_id_t *rid,
                        zlink_msg_t *parts,
                        size_t part_count,
                        void *userdata)
{
    if (part_count > 0)
        (void) on_stream_packet (rid, &parts[0], userdata);
    for (size_t i = 1; i < part_count; ++i)
        (void) zlink_msg_close (&parts[i]);
}

} // namespace

int main (int argc, char **argv)
{
    if (argc < 3)
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
    if (!ctx.valid ())
        return 1;

    void *server = zlink_socket (ctx.get (), ZLINK_SOCKET_STREAM);
    if (!server)
        return 1;

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
        zlink_close (server);
        return 1;
    }

    const std::string endpoint = bind_server_endpoint (
      server, transport, lib_name + "_stream_server");
    if (endpoint.empty ()) {
        zlink_close (server);
        return 1;
    }

    perf_stop_requested ().store (false, std::memory_order_release);
    g_callback_failed.store (false, std::memory_order_release);
    g_queue_probe_pending.store (false, std::memory_order_release);
    g_queue_probe_size.store (0, std::memory_order_release);
    g_sender_stop_requested.store (false, std::memory_order_release);
    g_pending_send_count.store (0, std::memory_order_release);
    g_send_poll_timeout_ms.store (io_timeout_ms, std::memory_order_release);
    g_server_socket = server;
    install_perf_signal_handlers ();

    const size_t queue_capacity =
      std::max<size_t> (static_cast<size_t> (1024), settings.clients * 2);
    std::unique_ptr<send_queue_t> send_queue (new send_queue_t (queue_capacity));
    g_send_queue = send_queue.get ();

    std::thread send_thread (send_thread_main);

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

    if (k_use_callback_mode) {
        if (zlink_recv_handler (server, &on_stream_handler, NULL) != 0) {
            g_sender_stop_requested.store (true, std::memory_order_release);
            send_thread.join ();
            g_send_queue = NULL;
            g_server_socket = NULL;
            zlink_close (server);
            return 1;
        }
    }

    std::cout << "READY," << endpoint << std::endl;

    int rc = 0;
    while (!perf_stop_requested ().load (std::memory_order_acquire) && rc == 0) {
        emit_requested_queue_probe (lib_name, transport, server, server);
        if (g_callback_failed.load (std::memory_order_acquire)) {
            rc = 1;
            break;
        }

        if (!k_use_callback_mode) {
            zlink_routing_id_t source_rid;
            std::memset (&source_rid, 0, sizeof (source_rid));
            zlink_msg_t *parts = NULL;
            size_t part_count = 0;
            const int recv_rc =
              zlink_recv (server, &source_rid, &parts, &part_count, 0);
            if (recv_rc == 0) {
                if (!process_stream_recv_parts (&source_rid, parts, part_count))
                    rc = 1;
                continue;
            }

            const int err = zlink_errno ();
            if (err == EAGAIN || err == EINTR)
                continue;
            rc = 1;
            break;
        }
        std::this_thread::sleep_for (std::chrono::milliseconds (50));
    }

    if (g_callback_failed.load (std::memory_order_acquire))
        rc = 1;

    g_sender_stop_requested.store (true, std::memory_order_release);
    send_thread.join ();
    g_send_queue = NULL;
    g_server_socket = NULL;

    const bench_resource_metrics_t metrics =
      bench_finish_resource_probe (cpu_start);
    const server_queue_stats_t queue_stats =
      sample_server_queue_stats (server, server);
    print_server_metrics (lib_name, transport, sizes, metrics, queue_stats);
    zlink_close (server);
    return rc;
}
