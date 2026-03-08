#include "../common/perf_entry.hpp"
#include "../common/perf_common.hpp"
#include "../common/perf_common_multi.hpp"
#include "../common/perf_metric_header.hpp"
#include "../../../bench/with_zmq/multi/common/bench_resource.hpp"
#include "../../../src/services/spot/spot_node.hpp"

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

#if !defined(_WIN32)
#include <unistd.h>
#else
#include <process.h>
#endif

namespace {

static const char *k_pattern = "SPOT";
static const char *k_service_name = "perf-spot";
static const char *k_topic = "bench";
static const uint32_t k_metric_run_id = 1U;

enum publish_status_t
{
    publish_ok = 0,
    publish_blocked = 1,
    publish_error = 2
};

static std::atomic<bool> g_stop_requested(false);
static std::atomic<bool> g_queue_probe_pending(false);
static std::atomic<size_t> g_queue_probe_size(0);

typedef int (*spot_set_tls_server_fn)(void *, const char *, const char *);
typedef int (*spot_set_tls_client_fn)(void *, const char *, const char *, int);

inline void debug_stage(const char *stage)
{
    if (bench_debug_enabled() && stage)
        std::cerr << "[multi-spot-server] " << stage << std::endl;
}

inline void debug_error(const char *stage)
{
    if (!bench_debug_enabled() || !stage)
        return;
    std::cerr << "[multi-spot-server] " << stage << " failed: "
              << zlink_strerror(zlink_errno()) << std::endl;
}

inline void debug_timing_ms(
  const char *stage,
  const std::chrono::steady_clock::time_point &startup_begin)
{
    if (!bench_debug_enabled() || !stage)
        return;
    const long long elapsed_ms =
      static_cast<long long>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now() - startup_begin)
          .count());
    std::cerr << "[multi-spot-server] t+" << elapsed_ms << "ms " << stage
              << std::endl;
}

inline void on_signal(int)
{
    g_stop_requested.store(true, std::memory_order_release);
}

inline void install_signal_handlers()
{
    std::signal(SIGINT, on_signal);
#if defined(SIGTERM)
    std::signal(SIGTERM, on_signal);
#endif
}

inline void request_queue_probe(size_t msg_size)
{
    if (msg_size == 0)
        return;
    g_queue_probe_size.store(msg_size, std::memory_order_release);
    g_queue_probe_pending.store(true, std::memory_order_release);
}

inline void emit_requested_queue_probe(const std::string &lib_name,
                                       const std::string &transport,
                                       void *spot_pub)
{
    if (!g_queue_probe_pending.exchange(false, std::memory_order_acq_rel))
        return;

    const size_t msg_size = g_queue_probe_size.load(std::memory_order_acquire);
    if (msg_size == 0 || !spot_pub)
        return;

    const server_queue_stats_t queue_stats =
      sample_service_queue_stats(zlink_spot_pub_peers, spot_pub, NULL, NULL);
    print_server_queue_metrics(lib_name, k_pattern, transport, msg_size, queue_stats);
}

inline void *spot_pub_socket_for_stats(void *node)
{
    if (!node)
        return NULL;
    zlink::spot_node_t *spot_node = static_cast<zlink::spot_node_t *>(node);
    if (!spot_node->check_tag()) {
        errno = EFAULT;
        return NULL;
    }
    return spot_node->pub_socket_for_poller();
}

inline bool is_supported_transport(const std::string &transport)
{
    return transport == "tcp" || transport == "tls" || transport == "ws"
           || transport == "wss";
}

inline int current_process_id()
{
#if !defined(_WIN32)
    return static_cast<int>(getpid());
#else
    return static_cast<int>(_getpid());
#endif
}

inline std::string replace_any_host_with_localhost(const std::string &endpoint)
{
    std::string normalized = endpoint;
    const std::string any_v4 = "://0.0.0.0:";
    const std::string any_v6 = "://[::]:";
    size_t pos = normalized.find(any_v4);
    if (pos != std::string::npos)
        normalized.replace(pos, any_v4.size(), "://127.0.0.1:");
    pos = normalized.find(any_v6);
    if (pos != std::string::npos)
        normalized.replace(pos, any_v6.size(), "://127.0.0.1:");
    return normalized;
}

inline bool setup_registry(void *ctx,
                           int base_port,
                           int broadcast_interval_ms,
                           void **registry_out,
                           std::string *pub_endpoint_out,
                           std::string *router_endpoint_out)
{
    if (!ctx || !registry_out || !pub_endpoint_out || !router_endpoint_out)
        return false;

    *registry_out = NULL;
    pub_endpoint_out->clear();
    router_endpoint_out->clear();

    for (int attempt = 0; attempt < 32; ++attempt) {
        const int port_base = base_port + attempt * 3;
        const std::string pub_ep = make_fixed_endpoint("tcp", port_base + 1);
        const std::string router_ep = make_fixed_endpoint("tcp", port_base + 2);

        void *registry = zlink_registry_new(ctx);
        if (!registry)
            return false;

        if (broadcast_interval_ms > 0) {
            (void) zlink_registry_set_broadcast_interval(
              registry, static_cast<uint32_t>(broadcast_interval_ms));
        }

        if (zlink_registry_set_endpoints(registry, pub_ep.c_str(), router_ep.c_str())
              == 0
            && zlink_registry_start(registry) == 0) {
            *registry_out = registry;
            *pub_endpoint_out = pub_ep;
            *router_endpoint_out = router_ep;
            return true;
        }

        zlink_registry_destroy(&registry);
    }

    return false;
}

inline bool configure_spot_tls_server(void *node, const std::string &transport)
{
    if (transport != "tls" && transport != "wss")
        return true;

    spot_set_tls_server_fn fn = reinterpret_cast<spot_set_tls_server_fn>(
      resolve_symbol("zlink_spot_node_set_tls_server"));
    if (!fn)
        return false;

    static const std::string cert_path =
      write_temp_cert(test_certs::server_cert_pem, "spot_srv_cert");
    static const std::string key_path =
      write_temp_cert(test_certs::server_key_pem, "spot_srv_key");
    return fn(node, cert_path.c_str(), key_path.c_str()) == 0;
}

inline bool configure_spot_tls_client(void *node, const std::string &transport)
{
    if (transport != "tls" && transport != "wss")
        return true;

    spot_set_tls_client_fn fn = reinterpret_cast<spot_set_tls_client_fn>(
      resolve_symbol("zlink_spot_node_set_tls_client"));
    if (!fn)
        return false;

    static const std::string ca_path =
      write_temp_cert(test_certs::ca_cert_pem, "spot_ca");
    return fn(node, ca_path.c_str(), "localhost", 0) == 0;
}

inline void apply_spot_pub_options(void *spot_pub,
                                   const bench_settings_t &settings)
{
    if (!spot_pub)
        return;

    const int sndhwm = bench_hwm_from_env("PERF_SNDHWM", settings.hwm);
    const int sndtimeo_ms =
      bench_timeout_ms_from_env("PERF_SNDTIMEO_MS", 200);
    const int linger_ms = 0;
    const int xpub_nodrop = resolve_int_env("PERF_SPOT_XPUB_NODROP", 1, 0);

    (void) zlink_spot_pub_set_option(
      spot_pub, ZLINK_SPOT_PUB_OPT_SNDHWM, &sndhwm, sizeof(sndhwm));
    (void) zlink_spot_pub_set_option(
      spot_pub, ZLINK_SPOT_PUB_OPT_SNDTIMEO, &sndtimeo_ms,
      sizeof(sndtimeo_ms));
    (void) zlink_spot_pub_set_option(
      spot_pub, ZLINK_SPOT_PUB_OPT_LINGER, &linger_ms, sizeof(linger_ms));
    (void) zlink_spot_pub_set_option(
      spot_pub, ZLINK_SPOT_PUB_OPT_NODROP, &xpub_nodrop, sizeof(xpub_nodrop));
}

inline void enforce_spot_socket_options(void *pub_socket,
                                        void *sub_socket,
                                        const bench_settings_t &settings,
                                        const std::string &transport)
{
    if (!pub_socket || !sub_socket)
        return;

    const int sndhwm = bench_hwm_from_env("PERF_SNDHWM", settings.hwm);
    const int rcvhwm = bench_hwm_from_env("PERF_RCVHWM", settings.hwm);
    const int sndtimeo_ms =
      bench_timeout_ms_from_env("PERF_SNDTIMEO_MS", 200);
    const int rcvtimeo_ms =
      bench_timeout_ms_from_env("PERF_RCVTIMEO_MS", 200);
    const int linger_ms = 0;
    const int xpub_nodrop = resolve_int_env("PERF_SPOT_XPUB_NODROP", 1, 0);

    set_sockopt_int(pub_socket, ZLINK_SNDHWM, sndhwm, "ZLINK_SNDHWM");
    set_sockopt_int(pub_socket, ZLINK_RCVHWM, rcvhwm, "ZLINK_RCVHWM");
    set_sockopt_int(pub_socket, ZLINK_LINGER, linger_ms, "ZLINK_LINGER");
    set_sockopt_int(pub_socket, ZLINK_SNDTIMEO, sndtimeo_ms, "ZLINK_SNDTIMEO");
    set_sockopt_int(pub_socket, ZLINK_RCVTIMEO, rcvtimeo_ms, "ZLINK_RCVTIMEO");
    set_sockopt_int(pub_socket, ZLINK_XPUB_NODROP, xpub_nodrop, "ZLINK_XPUB_NODROP");

    set_sockopt_int(sub_socket, ZLINK_SNDHWM, sndhwm, "ZLINK_SNDHWM");
    set_sockopt_int(sub_socket, ZLINK_RCVHWM, rcvhwm, "ZLINK_RCVHWM");
    set_sockopt_int(sub_socket, ZLINK_LINGER, linger_ms, "ZLINK_LINGER");
    set_sockopt_int(sub_socket, ZLINK_SNDTIMEO, sndtimeo_ms, "ZLINK_SNDTIMEO");
    set_sockopt_int(sub_socket, ZLINK_RCVTIMEO, rcvtimeo_ms, "ZLINK_RCVTIMEO");

    apply_debug_timeouts(pub_socket, transport);
    apply_debug_timeouts(sub_socket, transport);
}

inline std::string bind_spot_endpoint(void *node,
                                      const std::string &transport,
                                      const std::string &token)
{
    if (!node)
        return std::string();

    const int bind_port = resolve_int_env("PERF_SERVER_BIND_PORT", 0, 0);
    std::string endpoint =
      bind_port > 0 ? make_fixed_endpoint(transport, bind_port)
                    : make_endpoint(transport, token);
    if (endpoint.empty()) {
        std::cerr << "No endpoint available for transport " << transport
                  << std::endl;
        return std::string();
    }

    if (zlink_spot_node_bind(node, endpoint.c_str()) != 0) {
        std::cerr << "spot node bind failed for " << endpoint << ": "
                  << zlink_strerror(zlink_errno()) << std::endl;
        return std::string();
    }

    void *pub_socket = spot_pub_socket_for_stats(node);
    if (!pub_socket)
        return std::string();

    char last_endpoint[MAX_SOCKET_STRING] = "";
    size_t size = sizeof(last_endpoint);
    if (zlink_getsockopt(pub_socket, ZLINK_LAST_ENDPOINT, last_endpoint, &size) == 0)
        endpoint.assign(last_endpoint);

    endpoint = replace_any_host_with_localhost(endpoint);
    return endpoint;
}

inline bool wait_for_pub_peers(void *spot_pub, size_t target_count, int timeout_ms)
{
    if (!spot_pub)
        return false;

    const size_t target = std::max<size_t>(1, target_count);
    const auto deadline =
      std::chrono::steady_clock::now()
      + std::chrono::milliseconds(std::max(1, timeout_ms));
    auto next_debug_log = std::chrono::steady_clock::now();

    while (!g_stop_requested.load(std::memory_order_acquire)
           && std::chrono::steady_clock::now() < deadline) {
        size_t count = 0;
        if (zlink_spot_pub_peers(spot_pub, NULL, &count) == 0 && count >= target)
            return true;
        if (bench_debug_enabled()
            && std::chrono::steady_clock::now() >= next_debug_log) {
            std::cerr << "[multi-spot-server] pub peers " << count << "/"
                      << target << std::endl;
            next_debug_log = std::chrono::steady_clock::now()
                             + std::chrono::milliseconds(500);
        }
        if (zlink_poll(NULL, 0, 5) < 0 && zlink_errno() != EINTR)
            return false;
    }

    size_t count = 0;
    const bool ok =
      zlink_spot_pub_peers(spot_pub, NULL, &count) == 0 && count >= target;
    if (!ok && bench_debug_enabled()) {
        std::cerr << "[multi-spot-server] pub peer ready timeout " << count
                  << "/" << target << std::endl;
    }
    return ok;
}

inline publish_status_t publish_once(void *spot_pub,
                                     const std::string &topic,
                                     std::vector<char> &payload,
                                     size_t current_msg_size,
                                     perf_metric::phase_t phase,
                                     uint64_t seq)
{
    if (!spot_pub || current_msg_size == 0 || payload.empty())
        return publish_ok;

    const size_t metric_header_size = perf_metric::header_size();
    const size_t send_size =
      std::min(payload.size(), std::max<size_t>(static_cast<size_t>(1), current_msg_size));
    if (send_size < metric_header_size)
        return publish_error;
    if (!perf_metric::stamp_payload(payload.data(),
                                    send_size,
                                    k_metric_run_id,
                                    phase,
                                    current_msg_size,
                                    seq,
                                    perf_metric::now_us())) {
        return publish_error;
    }

    if (zlink_spot_pub_publish_bytes(spot_pub,
                                     topic.c_str(),
                                     payload.data(),
                                     send_size,
                                     ZLINK_DONTWAIT)
        == 0) {
        return publish_ok;
    }

    const int err = zlink_errno();
    if (err == EINTR || err == EAGAIN)
        return publish_blocked;
    return publish_error;
}

inline size_t resolve_max_size(const std::vector<size_t> &sizes)
{
    size_t max_size = 64;
    for (size_t i = 0; i < sizes.size(); ++i) {
        if (sizes[i] > max_size)
            max_size = sizes[i];
    }
    return max_size;
}

struct one_way_phase_t
{
    one_way_phase_t(size_t msg_size_,
                    perf_metric::phase_t phase_,
                    std::chrono::steady_clock::duration duration_,
                    bool send_active_) :
        msg_size(msg_size_),
        phase(phase_),
        duration(duration_),
        send_active(send_active_)
    {
    }

    size_t msg_size;
    perf_metric::phase_t phase;
    std::chrono::steady_clock::duration duration;
    bool send_active;
};

inline void append_one_way_phase(std::vector<one_way_phase_t> *phases,
                                 size_t msg_size,
                                 perf_metric::phase_t phase,
                                 double seconds,
                                 bool send_active)
{
    if (!phases || seconds <= 0.0)
        return;
    phases->push_back(one_way_phase_t(
      msg_size,
      phase,
      std::chrono::duration_cast<std::chrono::steady_clock::duration>(
        std::chrono::duration<double>(seconds)),
      send_active));
}

inline std::vector<one_way_phase_t>
build_one_way_phases(const bench_settings_t &settings,
                     const std::vector<size_t> &msg_sizes)
{
    std::vector<one_way_phase_t> phases;
    if (msg_sizes.empty())
        return phases;

    const double warmup_s = static_cast<double>(std::max(0, settings.warmup_seconds));
    const double settle_s =
      static_cast<double>(std::max(0, settings.settle_ms)) / 1000.0;
    const double active_s =
      static_cast<double>(std::max(1, settings.duration_seconds));

    for (size_t i = 0; i < msg_sizes.size(); ++i) {
        const size_t msg_size = msg_sizes[i];
        append_one_way_phase(
          &phases, msg_size, perf_metric::phase_warmup, warmup_s, true);
        append_one_way_phase(
          &phases, msg_size, perf_metric::phase_drain, settle_s, false);
        append_one_way_phase(
          &phases, msg_size, perf_metric::phase_active, active_s, true);
    }

    return phases;
}

inline void print_server_metrics(const std::string &lib_name,
                                 const std::string &transport,
                                 const std::vector<size_t> &sizes,
                                 const bench_resource_metrics_t &metrics,
                                 const server_queue_stats_t &queue_stats)
{
    for (size_t i = 0; i < sizes.size(); ++i) {
        if (metrics.has_cpu_pct) {
            std::cout << "RESULT," << lib_name << "," << k_pattern << ","
                      << transport << "," << sizes[i]
                      << ",server_cpu_pct," << std::fixed
                      << std::setprecision(3) << metrics.cpu_pct << std::endl;
        }
        if (metrics.has_mem_mb) {
            std::cout << "RESULT," << lib_name << "," << k_pattern << ","
                      << transport << "," << sizes[i]
                      << ",server_mem_mb," << std::fixed
                      << std::setprecision(3) << metrics.mem_mb << std::endl;
        }
        print_server_queue_metrics(lib_name,
                                   k_pattern,
                                   transport,
                                   sizes[i],
                                   queue_stats);
    }
}

inline bool run_server_loop(void *spot_pub,
                            void *node,
                            const bench_settings_t &settings,
                            const std::vector<size_t> &msg_sizes,
                            std::vector<char> *payload,
                            const std::string &lib_name,
                            const std::string &transport)
{
    if (!spot_pub || !node || !payload)
        return false;

    const std::vector<one_way_phase_t> phases =
      build_one_way_phases(settings, msg_sizes);
    size_t phase_index = 0;
    auto phase_deadline = std::chrono::steady_clock::now();
    size_t current_phase_msg_size = 0;
    perf_metric::phase_t current_phase = perf_metric::phase_warmup;
    uint64_t phase_seq = 1;
    bool send_pending = false;
    if (!phases.empty())
        phase_deadline += phases[0].duration;

    void *poller = zlink_poller_new();
    if (!poller)
        return false;
    if (zlink_poller_add_spot_pub(poller, spot_pub, NULL, 0) != 0) {
        zlink_poller_destroy(&poller);
        return false;
    }
    zlink_poller_event_t event;

    while (!g_stop_requested.load(std::memory_order_acquire)) {
        emit_requested_queue_probe(lib_name,
                                   transport,
                                   spot_pub);

        if (!phases.empty()) {
            auto now = std::chrono::steady_clock::now();
            while (phase_index < phases.size() && now >= phase_deadline) {
                ++phase_index;
                if (phase_index < phases.size())
                    phase_deadline += phases[phase_index].duration;
                send_pending = false;
                now = std::chrono::steady_clock::now();
            }

            if (phase_index >= phases.size()) {
                if (zlink_poll(NULL, 0, 50) < 0 && zlink_errno() != EINTR) {
                    zlink_poller_destroy(&poller);
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
                                      || (event.events & ZLINK_POLLOUT) != 0;
                if (try_send) {
                    const publish_status_t send_rc = publish_once(
                      spot_pub,
                      k_topic,
                      *payload,
                      phases[phase_index].msg_size,
                      phases[phase_index].phase,
                      phase_seq);
                    if (send_rc == publish_ok) {
                        ++phase_seq;
                        send_pending = false;
                        event.events = 0;
                        continue;
                    }
                    if (send_rc == publish_error) {
                        zlink_poller_destroy(&poller);
                        return false;
                    }
                    send_pending = true;
                }
            } else {
                send_pending = false;
            }

            if (zlink_poller_modify_spot_pub(
                  poller, spot_pub, send_pending ? ZLINK_POLLOUT : 0)
                != 0) {
                zlink_poller_destroy(&poller);
                return false;
            }

            int timeout_ms = 50;
            const auto now_for_timeout = std::chrono::steady_clock::now();
            if (phase_index < phases.size() && phase_deadline > now_for_timeout) {
                const long remain_ms =
                  std::chrono::duration_cast<std::chrono::milliseconds>(
                    phase_deadline - now_for_timeout)
                    .count();
                if (remain_ms >= 0)
                    timeout_ms = std::min(timeout_ms, static_cast<int>(remain_ms));
            } else if (phase_index < phases.size()) {
                timeout_ms = 0;
            }

            const int prc = zlink_poller_wait(
              poller,
              &event,
              send_pending ? timeout_ms : 0);
            if (prc < 0 && zlink_errno() != EINTR) {
                zlink_poller_destroy(&poller);
                return false;
            }
            if (prc <= 0)
                event.events = 0;
            continue;
        }

        current_phase = perf_metric::phase_active;
        current_phase_msg_size = payload->size();
        const bool try_send = !send_pending || (event.events & ZLINK_POLLOUT) != 0;
        if (try_send) {
            const publish_status_t send_rc = publish_once(spot_pub,
                                                          k_topic,
                                                          *payload,
                                                          payload->size(),
                                                          perf_metric::phase_active,
                                                          phase_seq);
            if (send_rc == publish_ok) {
                ++phase_seq;
                send_pending = false;
                event.events = 0;
                continue;
            }
            if (send_rc == publish_error) {
                zlink_poller_destroy(&poller);
                return false;
            }
            send_pending = true;
        }

        if (zlink_poller_modify_spot_pub(
              poller, spot_pub, send_pending ? ZLINK_POLLOUT : 0)
            != 0) {
            zlink_poller_destroy(&poller);
            return false;
        }
        const int prc = zlink_poller_wait(
          poller,
          &event,
          send_pending ? 50 : 0);
        if (prc < 0 && zlink_errno() != EINTR) {
            zlink_poller_destroy(&poller);
            return false;
        }
        if (prc <= 0)
            event.events = 0;
    }

    zlink_poller_destroy(&poller);
    return true;
}

inline int run_server_benchmark(const std::string &lib_name,
                                const std::string &transport)
{
    set_perf_pattern_env(k_pattern);
    const std::chrono::steady_clock::time_point startup_begin =
      std::chrono::steady_clock::now();
    debug_timing_ms("startup begin", startup_begin);

    if (!is_supported_transport(transport)) {
        std::cout << "UNSUPPORTED," << lib_name << "," << k_pattern << ","
                  << transport << std::endl;
        return 0;
    }

    if (!transport_available(transport)) {
        std::cerr << "transport unavailable: " << transport << std::endl;
        return 1;
    }

    ctx_guard_t ctx;
    if (!ctx.valid())
        return 1;
    debug_timing_ms("ctx ready", startup_begin);

    const bench_settings_t settings = resolve_bench_settings();

    void *registry = NULL;
    void *node = zlink_spot_node_new(ctx.get());
    void *spot_pub = NULL;

    auto cleanup = [&]() {
        if (spot_pub)
            zlink_spot_pub_destroy(&spot_pub);
        if (node)
            zlink_spot_node_destroy(&node);
        if (registry)
            zlink_registry_destroy(&registry);
    };

    if (!node)
        return 1;
    debug_timing_ms("spot node created", startup_begin);

    std::string registry_pub_endpoint;
    std::string registry_router_endpoint;
    debug_stage("setup registry");
    if (!setup_registry(ctx.get(),
                        30000 + (current_process_id() % 20000),
                        std::max(100, settings.settle_ms),
                        &registry,
                        &registry_pub_endpoint,
                        &registry_router_endpoint)) {
        cleanup();
        return 1;
    }
    debug_timing_ms("registry ready", startup_begin);

    if (!configure_spot_tls_server(node, transport)
        || !configure_spot_tls_client(node, transport)) {
        cleanup();
        return 1;
    }
    debug_timing_ms("tls configured", startup_begin);

    const std::string endpoint =
      bind_spot_endpoint(node,
                         transport,
                         lib_name + std::string("_spot_server"));
    if (endpoint.empty()) {
        debug_error("spot bind");
        cleanup();
        return 1;
    }
    debug_timing_ms("spot bind ready", startup_begin);

    debug_stage("connect/register");
    if (zlink_spot_node_connect_registry(node, registry_router_endpoint.c_str()) != 0
        || zlink_spot_node_register(node, k_service_name, endpoint.c_str()) != 0) {
        debug_error("spot register");
        cleanup();
        return 1;
    }
    debug_timing_ms("spot registry connected", startup_begin);

    spot_pub = zlink_spot_pub_new(node);
    if (!spot_pub) {
        cleanup();
        return 1;
    }
    apply_spot_pub_options(spot_pub, settings);

    debug_timing_ms("pub/sub sockets ready", startup_begin);

    g_stop_requested.store(false, std::memory_order_release);
    g_queue_probe_pending.store(false, std::memory_order_release);
    g_queue_probe_size.store(0, std::memory_order_release);
    install_signal_handlers();

    std::thread stdin_watcher([]() {
        std::string line;
        while (std::getline(std::cin, line)) {
            size_t queue_size = 0;
            if (parse_queue_probe_command(line, &queue_size)) {
                request_queue_probe(queue_size);
                continue;
            }
            if (line == "STOP" || line == "QUIT") {
                g_stop_requested.store(true, std::memory_order_release);
                return;
            }
        }
        g_stop_requested.store(true, std::memory_order_release);
    });
    stdin_watcher.detach();

    std::vector<size_t> sizes = resolve_bench_msg_sizes(64);
    if (sizes.empty())
        sizes.push_back(64);

    const size_t max_size = resolve_max_size(sizes);
    std::vector<char> payload(
      std::max<size_t>(static_cast<size_t>(1024), max_size),
      's');
    if (payload.size() < perf_metric::header_size())
        payload.resize(perf_metric::header_size(), 's');

    const bench_cpu_sample_t sample_start = bench_capture_cpu_sample();

    const std::string ready_payload =
      endpoint + "|" + registry_pub_endpoint + "|" + registry_router_endpoint;
    debug_timing_ms("emit READY", startup_begin);
    std::cout << "READY," << ready_payload << std::endl;

    const int peer_wait_ms = std::max(500, settings.connect_ready_timeout_ms);
    const size_t service_clients =
      resolve_service_clients(settings.clients);
    if (service_clients != settings.clients) {
        std::cerr << "spot server: service clients capped "
                  << service_clients << "/" << settings.clients << std::endl;
    }
    if (!wait_for_pub_peers(spot_pub, service_clients, peer_wait_ms)) {
        if (bench_debug_enabled())
            std::cerr << "[multi-spot-server] continue without full pub peer ready"
                      << std::endl;
    }

    const bool loop_ok = run_server_loop(spot_pub,
                                         node,
                                         settings,
                                         sizes,
                                         &payload,
                                         lib_name,
                                         transport);

    const bench_resource_metrics_t metrics =
      bench_finish_resource_probe(sample_start);
    const server_queue_stats_t queue_stats =
      sample_service_queue_stats(zlink_spot_pub_peers, spot_pub, NULL, NULL);
    print_server_metrics(lib_name, transport, sizes, metrics, queue_stats);

    cleanup();
    return loop_ok ? 0 : 1;
}

} // namespace

int main(int argc, char **argv)
{
    if (argc < 3)
        return 1;

    const std::string lib_name = argv[1];
    const std::string transport = argv[2];
    return run_server_benchmark(lib_name, transport);
}
