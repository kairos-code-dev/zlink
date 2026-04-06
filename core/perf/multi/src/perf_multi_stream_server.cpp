#include "../common/perf_common.hpp"
#include "../common/perf_common_multi.hpp"
#include "../common/perf_multi_stream_session.hpp"
#include "../../../bench/with_zmq/multi/common/bench_resource.hpp"

#include <cerrno>
#include <csignal>
#include <cstring>
#include <iomanip>
#include <iostream>
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
static std::atomic<bool> g_queue_probe_pending (false);
static std::atomic<size_t> g_queue_probe_size (0);
static perf_multi_stream::session_t g_stream_session;

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

struct stream_loop_tick_ctx_t
{
    stream_loop_tick_ctx_t(const std::string *lib_name_,
                           const std::string *transport_,
                           void *send_socket_,
                           void *recv_socket_) :
        lib_name(lib_name_),
        transport(transport_),
        send_socket(send_socket_),
        recv_socket(recv_socket_)
    {
    }

    const std::string *lib_name;
    const std::string *transport;
    void *send_socket;
    void *recv_socket;
};

inline void run_stream_loop_tick(void *ctx_)
{
    stream_loop_tick_ctx_t *ctx =
      static_cast<stream_loop_tick_ctx_t *>(ctx_);
    if (!ctx || !ctx->lib_name || !ctx->transport)
        return;
    emit_requested_queue_probe(
      *ctx->lib_name, *ctx->transport, ctx->send_socket, ctx->recv_socket);
}

inline void print_server_metrics (
  const std::string &lib_name,
  const std::string &transport,
  const std::vector<size_t> &sizes,
  const bench_resource_metrics_t &metrics,
  const server_queue_stats_t &queue_stats)
{
    print_server_metrics_for_sizes (
      lib_name, k_pattern, transport, sizes, metrics, &queue_stats);
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
    perf_multi_stream::reset_session (&g_stream_session, server);
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

    stream_loop_tick_ctx_t loop_tick_ctx(&lib_name, &transport, server, server);
    const int rc = perf_multi_stream::run_server_event_loop (
      &g_stream_session,
      server,
      k_stop_token,
      &run_stream_loop_tick,
      &loop_tick_ctx);

    perf_multi_stream::clear_session (&g_stream_session);

    const bench_resource_metrics_t metrics =
      bench_finish_resource_probe (cpu_start);
    const server_queue_stats_t queue_stats =
      sample_server_queue_stats (server, server);
    print_server_metrics (lib_name, transport, sizes, metrics, queue_stats);
    zlink_close (server);
    return rc;
}
