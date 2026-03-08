#include "../common/perf_entry.hpp"
#include "../common/perf_common.hpp"
#include "../common/perf_common_multi.hpp"
#include "../common/perf_metric_header.hpp"
#include "../../../bench/with_zmq/multi/common/bench_resource.hpp"

#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <csignal>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace {

typedef std::chrono::steady_clock steady_clock_t;

static const char *k_pattern = "PUBSUB";
static const char *k_token = "pubsub";
static const int k_server_socket_type = ZLINK_PUB;
static const bool k_server_has_routing_id = false;
static const char *k_server_routing_id = "SERVER";
static const uint32_t k_metric_run_id = 1U;

enum publish_status_t
{
    publish_ok = 0,
    publish_blocked = 1,
    publish_error = 2
};

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
    if (transport == "tcp" || transport == "tls" || transport == "ws"
        || transport == "wss")
        return true;
#if !defined(_WIN32)
    if (transport == "ipc")
        return true;
#endif
    return false;
}

inline std::string bind_server_endpoint (void *server,
                                         const std::string &transport,
                                         const std::string &token)
{
    const int bind_port =
      resolve_int_env ("PERF_SERVER_BIND_PORT", 0, 0);
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

inline publish_status_t publish_once (void *server,
                                      std::vector<char> &payload,
                                      size_t current_msg_size,
                                      perf_metric::phase_t phase,
                                      uint64_t seq)
{
    if (current_msg_size == 0)
        return publish_ok;

    const size_t send_size =
      std::min (payload.size (), std::max<size_t> (static_cast<size_t> (1), current_msg_size));
    if (send_size < perf_metric::header_size ())
        return publish_error;
    if (!perf_metric::stamp_payload (
          payload.data (),
          send_size,
          k_metric_run_id,
          phase,
          current_msg_size,
          seq,
          perf_metric::now_us ())) {
        return publish_error;
    }

    if (zlink_send (server, payload.data (), send_size, ZLINK_DONTWAIT) >= 0)
        return publish_ok;

    const int err = zlink_errno ();
    if (err == EINTR || err == EAGAIN)
        return publish_blocked;
    return publish_error;
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
                     perf_metric::phase_t phase_,
                     steady_clock_t::duration duration_,
                     bool send_active_) :
        msg_size (msg_size_),
        phase (phase_),
        duration (duration_),
        send_active (send_active_)
    {
    }

    size_t msg_size;
    perf_metric::phase_t phase;
    steady_clock_t::duration duration;
    bool send_active;
};

inline void append_one_way_phase (std::vector<one_way_phase_t> *phases,
                                  size_t msg_size,
                                  perf_metric::phase_t phase,
                                  double seconds,
                                  bool send_active)
{
    if (!phases || seconds <= 0.0)
        return;
    phases->push_back (one_way_phase_t (
      msg_size,
      phase,
      to_clock_duration (seconds),
      send_active));
}

inline std::vector<one_way_phase_t>
build_one_way_phases (const bench_settings_t &settings,
                      const std::vector<size_t> &msg_sizes)
{
    std::vector<one_way_phase_t> phases;
    if (msg_sizes.empty ())
        return phases;

    const double warmup_s = static_cast<double> (std::max (0, settings.warmup_seconds));
    const double settle_s =
      static_cast<double> (std::max (0, settings.settle_ms)) / 1000.0;
    const double active_s =
      static_cast<double> (std::max (1, settings.duration_seconds));

    for (size_t i = 0; i < msg_sizes.size (); ++i) {
        const size_t msg_size = msg_sizes[i];
        append_one_way_phase (
          &phases, msg_size, perf_metric::phase_warmup, warmup_s, true);
        append_one_way_phase (
          &phases, msg_size, perf_metric::phase_drain, settle_s, false);
        append_one_way_phase (
          &phases, msg_size, perf_metric::phase_active, active_s, true);
    }

    return phases;
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

inline bool run_server_loop (void *server,
                             const bench_settings_t &settings,
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
    auto phase_deadline = steady_clock_t::now ();
    size_t current_phase_msg_size = 0;
    perf_metric::phase_t current_phase = perf_metric::phase_warmup;
    uint64_t phase_seq = 1;
    bool send_pending = false;
    if (!phases.empty ())
        phase_deadline += phases[0].duration;

    zlink_pollitem_t poll_item = {server, 0, 0, 0};

    while (!g_stop_requested.load (std::memory_order_acquire)) {
        emit_requested_queue_probe (lib_name, transport, server, server);

        if (!phases.empty ()) {
            auto now = steady_clock_t::now ();
            while (phase_index < phases.size () && now >= phase_deadline) {
                ++phase_index;
                if (phase_index < phases.size ())
                    phase_deadline += phases[phase_index].duration;
                send_pending = false;
                now = steady_clock_t::now ();
            }

            if (phase_index >= phases.size ()) {
                const int idle_timeout_ms = 50;
                if (zlink_poll (NULL, 0, idle_timeout_ms) < 0
                    && zlink_errno () != EINTR) {
                    return false;
                }
                continue;
            }

            if (phases[phase_index].msg_size != current_phase_msg_size
                || phases[phase_index].phase != current_phase) {
                current_phase_msg_size = phases[phase_index].msg_size;
                current_phase = phases[phase_index].phase;
                phase_seq = 1;
            }

            const bool send_active = phases[phase_index].send_active;
            if (send_active) {
                const bool try_send = !send_pending
                                      || (poll_item.revents & ZLINK_POLLOUT) != 0;
                if (try_send) {
                    const publish_status_t send_rc = publish_once (
                      server,
                      *payload,
                      phases[phase_index].msg_size,
                      phases[phase_index].phase,
                      phase_seq);
                    if (send_rc == publish_ok) {
                        ++phase_seq;
                        send_pending = false;
                        poll_item.revents = 0;
                        continue;
                    }
                    if (send_rc == publish_error)
                        return false;
                    send_pending = true;
                }
            } else {
                send_pending = false;
            }

            poll_item.events = send_pending ? ZLINK_POLLOUT : 0;
            poll_item.revents = 0;

            int timeout_ms = 50;
            const auto now_for_timeout = steady_clock_t::now ();
            if (phase_index < phases.size () && phase_deadline > now_for_timeout) {
                const long remain_ms =
                  remaining_milliseconds (phase_deadline, now_for_timeout);
                if (remain_ms >= 0)
                    timeout_ms = std::min (timeout_ms, static_cast<int> (remain_ms));
            } else if (phase_index < phases.size ()) {
                timeout_ms = 0;
            }

            if (zlink_poll (send_pending ? &poll_item : NULL,
                            send_pending ? 1 : 0,
                            timeout_ms) < 0
                && zlink_errno () != EINTR) {
                return false;
            }
            continue;
        }

        current_phase = perf_metric::phase_active;
        current_phase_msg_size = payload->size ();
        const bool try_send =
          !send_pending || (poll_item.revents & ZLINK_POLLOUT) != 0;
        if (try_send) {
            const publish_status_t send_rc = publish_once (
              server,
              *payload,
              payload->size (),
              perf_metric::phase_active,
              phase_seq);
            if (send_rc == publish_ok) {
                ++phase_seq;
                send_pending = false;
                poll_item.revents = 0;
                continue;
            }
            if (send_rc == publish_error)
                return false;
            send_pending = true;
        }

        poll_item.events = send_pending ? ZLINK_POLLOUT : 0;
        poll_item.revents = 0;
        if (zlink_poll (send_pending ? &poll_item : NULL,
                        send_pending ? 1 : 0,
                        send_pending ? 50 : 0) < 0
            && zlink_errno () != EINTR) {
            return false;
        }
    }

    return true;
}

inline int run_server_benchmark (const std::string &lib_name,
                                 const std::string &transport)
{
    set_perf_pattern_env (k_pattern);

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

    const bench_settings_t settings = resolve_bench_settings ();
    const int linger_ms = 0;
    const int xpub_nodrop =
      resolve_int_env ("PERF_PUBSUB_XPUB_NODROP", 1, 0);
    set_sockopt_int (server, ZLINK_LINGER, linger_ms, "ZLINK_LINGER");
    set_sockopt_int (
      server,
      ZLINK_XPUB_NODROP,
      xpub_nodrop,
      "ZLINK_XPUB_NODROP");
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
        std::max<size_t> (max_size, perf_metric::header_size ())),
      's');

    const bench_cpu_sample_t sample_start = bench_capture_cpu_sample ();

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

    const bench_resource_metrics_t metrics =
      bench_finish_resource_probe (sample_start);
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
