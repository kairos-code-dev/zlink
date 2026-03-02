#include "../common/perf_multi_entry.hpp"
#include "../common/perf_common.hpp"
#include "../common/perf_common_multi.hpp"
#include "../../../bench/with_zmq/multi/common/bench_multi_resource.hpp"

#include <atomic>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace {

static const char *k_pattern = "MULTI_DEALER_DEALER";
static const char *k_token = "dealer_dealer";
static const int k_server_socket_type = ZLINK_DEALER;
static const bool k_server_has_routing_id = false;
static const char *k_server_routing_id = "SERVER";

static std::atomic<bool> g_stop_requested (false);
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

inline bool is_supported_transport (const std::string &transport)
{
    return transport == "tcp" || transport == "tls" || transport == "ws"
           || transport == "wss";
}

inline std::string bind_server_endpoint (void *server,
                                         const std::string &transport,
                                         const std::string &token)
{
    const int bind_port =
      resolve_multi_int_env ("PERF_MULTI_SERVER_BIND_PORT", 0, 0);
    if (bind_port <= 0) {
        std::string endpoint_any = make_endpoint (transport, token);
        if (endpoint_any.empty ()) {
            std::cerr << "No endpoint available for transport " << transport
                      << std::endl;
            return std::string ();
        }
        if (zlink_bind (server, endpoint_any.c_str ()) != 0) {
            std::cerr << "bind failed for " << endpoint_any << ": "
                      << zlink_strerror (zlink_errno ()) << std::endl;
            return std::string ();
        }

        char last_endpoint[MAX_SOCKET_STRING] = "";
        size_t size = sizeof (last_endpoint);
        if (zlink_getsockopt (server, ZLINK_LAST_ENDPOINT, last_endpoint, &size)
            == 0) {
            endpoint_any.assign (last_endpoint);
            const std::string any_v4 = "://0.0.0.0:";
            const std::string any_v6 = "://[::]:";
            size_t pos = endpoint_any.find (any_v4);
            if (pos != std::string::npos) {
                endpoint_any.replace (pos, any_v4.size (), "://127.0.0.1:");
            } else {
                pos = endpoint_any.find (any_v6);
                if (pos != std::string::npos)
                    endpoint_any.replace (pos, any_v6.size (), "://127.0.0.1:");
            }
        }

        apply_debug_timeouts (server, transport);
        return endpoint_any;
    }

    std::string endpoint = make_fixed_endpoint (transport, bind_port);
    if (zlink_bind (server, endpoint.c_str ()) != 0) {
        std::cerr << "bind failed for " << endpoint << ": "
                  << zlink_strerror (zlink_errno ()) << std::endl;
        return std::string ();
    }

    char last_endpoint[MAX_SOCKET_STRING] = "";
    size_t size = sizeof (last_endpoint);
    if (zlink_getsockopt (server, ZLINK_LAST_ENDPOINT, last_endpoint, &size) == 0)
        endpoint.assign (last_endpoint);
    apply_debug_timeouts (server, transport);
    return endpoint;
}

inline unsigned long long wallclock_now_us ()
{
    return static_cast<unsigned long long> (
      std::chrono::duration_cast<std::chrono::microseconds> (
        std::chrono::system_clock::now ().time_since_epoch ())
        .count ());
}

inline bool publish_once (void *server,
                          std::vector<char> &payload,
                          size_t current_msg_size)
{
    if (current_msg_size == 0)
        return true;

    const size_t send_size =
      std::min (payload.size (), std::max<size_t> (static_cast<size_t> (1), current_msg_size));

    if (send_size >= sizeof (unsigned long long)) {
        const unsigned long long now_us = wallclock_now_us ();
        std::memcpy (payload.data (), &now_us, sizeof (now_us));
    }

    if (zlink_send (server, payload.data (), send_size, 0) >= 0)
        return true;

    const int err = zlink_errno ();
    if (err == EINTR)
        return true;
    if (err == EAGAIN || err == ETIMEDOUT) {
        std::this_thread::yield ();
        return true;
    }

    return g_stop_requested.load (std::memory_order_acquire);
}

inline size_t resolve_max_size (const std::vector<size_t> &sizes)
{
    size_t max_size = 64;
    for (size_t i = 0; i < sizes.size (); ++i) {
        if (sizes[i] > max_size)
            max_size = sizes[i];
    }
    return max_size;
}

struct one_way_phase_t
{
    one_way_phase_t (size_t msg_size_,
                     std::chrono::steady_clock::duration duration_,
                     bool send_active_) :
        msg_size (msg_size_),
        duration (duration_),
        send_active (send_active_)
    {
    }

    size_t msg_size;
    std::chrono::steady_clock::duration duration;
    bool send_active;
};

inline void append_one_way_phase (std::vector<one_way_phase_t> *phases,
                                  size_t msg_size,
                                  double seconds,
                                  bool send_active)
{
    if (!phases || seconds <= 0.0)
        return;
    phases->push_back (one_way_phase_t (
      msg_size,
      std::chrono::duration_cast<std::chrono::steady_clock::duration> (
        std::chrono::duration<double> (seconds)),
      send_active));
}

inline std::vector<one_way_phase_t>
build_one_way_phases (const multi_bench_settings_t &settings,
                      const std::vector<size_t> &msg_sizes)
{
    std::vector<one_way_phase_t> phases;
    if (msg_sizes.empty ())
        return phases;

    const double warmup_s = static_cast<double> (std::max (0, settings.warmup_seconds));
    const double settle_s =
      static_cast<double> (std::max (0, settings.settle_ms)) / 1000.0;
    const double throughput_s =
      static_cast<double> (std::max (1, settings.duration_seconds));
    const double latency_s =
      static_cast<double> (std::max (1, settings.duration_seconds));
    const double drain_s = static_cast<double> (std::max (0, settings.drain_ms)) / 1000.0;
    const double transition_s =
      static_cast<double> (std::max (0, settings.size_transition_drain_ms)) / 1000.0;

    for (size_t i = 0; i < msg_sizes.size (); ++i) {
        const size_t msg_size = msg_sizes[i];
        append_one_way_phase (&phases, msg_size, warmup_s, true);
        append_one_way_phase (&phases, msg_size, settle_s, false);
        append_one_way_phase (&phases, msg_size, throughput_s, true);
        append_one_way_phase (&phases, msg_size, latency_s, true);
        append_one_way_phase (&phases, msg_size, drain_s, false);
        if ((i + 1) < msg_sizes.size ())
            append_one_way_phase (&phases, msg_size, transition_s, false);
    }

    return phases;
}

inline void print_server_metrics (
  const std::string &lib_name,
  const std::string &transport,
  const std::vector<size_t> &sizes,
  const bench_multi_resource_metrics_t &metrics,
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

inline bool run_server_loop (void *server,
                             const multi_bench_settings_t &settings,
                             const std::vector<size_t> &msg_sizes,
                             std::vector<char> *payload,
                             const std::string &lib_name,
                             const std::string &transport)
{
    if (!server || !payload)
        return false;

    const std::vector<one_way_phase_t> phases =
      build_one_way_phases (settings, msg_sizes);
    size_t phase_index = 0;
    auto phase_deadline = std::chrono::steady_clock::now ();
    if (!phases.empty ())
        phase_deadline += phases[0].duration;

    while (!g_stop_requested.load (std::memory_order_acquire)) {
        emit_requested_queue_probe (lib_name, transport, server, server);

        if (!phases.empty ()) {
            const auto now = std::chrono::steady_clock::now ();
            while (phase_index < phases.size () && now >= phase_deadline) {
                ++phase_index;
                if (phase_index < phases.size ())
                    phase_deadline += phases[phase_index].duration;
            }

            if (phase_index >= phases.size ()) {
                std::this_thread::sleep_for (std::chrono::milliseconds (1));
                continue;
            }

            if (!phases[phase_index].send_active) {
                std::this_thread::sleep_for (std::chrono::milliseconds (1));
                continue;
            }

            if (!publish_once (server, *payload, phases[phase_index].msg_size))
                return false;
            continue;
        }

        if (!publish_once (server, *payload, payload->size ()))
            return false;
    }

    return true;
}

inline int run_server_benchmark (const std::string &lib_name,
                                 const std::string &transport)
{
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

    void *server = zlink_socket (ctx.get (), k_server_socket_type);
    if (!server)
        return 1;

    const multi_bench_settings_t settings = resolve_multi_bench_settings ();
    const int linger_ms = 0;
    set_sockopt_int (server, ZLINK_LINGER, linger_ms, "ZLINK_LINGER");
    apply_benchmark_hwm (server, settings.hwm);
    if (k_server_has_routing_id) {
        zlink_setsockopt (
          server,
          ZLINK_ROUTING_ID,
          k_server_routing_id,
          std::strlen (k_server_routing_id));
    }

    if (!setup_tls_server (server, transport)) {
        zlink_close (server);
        return 1;
    }

    connect_monitor_t server_monitor;
    if (!open_connect_monitor (server, server_monitor)) {
        zlink_close (server);
        return 1;
    }

    const std::string endpoint = bind_server_endpoint (
      server,
      transport,
      lib_name + std::string ("_") + k_token + "_server");
    if (endpoint.empty ()) {
        close_connect_monitor (server_monitor);
        zlink_close (server);
        return 1;
    }

    g_stop_requested.store (false, std::memory_order_release);
    g_queue_probe_pending.store (false, std::memory_order_release);
    g_queue_probe_size.store (0, std::memory_order_release);
    install_signal_handlers ();

    std::thread stdin_watcher ([] () {
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

    std::vector<size_t> sizes = resolve_bench_msg_sizes (64);
    if (sizes.empty ())
        sizes.push_back (64);

    const size_t max_size = resolve_max_size (sizes);
    std::vector<char> payload (
      std::max<size_t> (
        static_cast<size_t> (1024),
        std::max<size_t> (max_size, sizeof (unsigned long long))),
      's');

    const bench_multi_cpu_sample_t sample_start = bench_multi_capture_cpu_sample ();

    std::cout << "READY," << endpoint << std::endl;

    if (!wait_connect_ready_count (
          server_monitor,
          settings.clients,
          settings.connect_ready_timeout_ms)) {
        close_connect_monitor (server_monitor);
        zlink_close (server);
        return 1;
    }

    const bool loop_ok = run_server_loop (
      server,
      settings,
      sizes,
      &payload,
      lib_name,
      transport);

    const bench_multi_resource_metrics_t metrics =
      bench_multi_finish_resource_probe (sample_start);
    const server_queue_stats_t queue_stats =
      sample_server_queue_stats (server, server);
    print_server_metrics (lib_name, transport, sizes, metrics, queue_stats);

    close_connect_monitor (server_monitor);
    zlink_close (server);

    return loop_ok ? 0 : 1;
}

} // namespace

int main (int argc, char **argv)
{
    if (argc < 3)
        return 1;

    const std::string lib_name = argv[1];
    const std::string transport = argv[2];
    return run_server_benchmark (lib_name, transport);
}
