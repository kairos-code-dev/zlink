#include "../common/perf_multi_entry.hpp"
#include "../common/perf_common.hpp"
#include "../common/perf_common_multi.hpp"
#include "../common/perf_multi_metric_header.hpp"
#include "../../../bench/with_zmq/multi/common/bench_multi_resource.hpp"
#include "../../../src/core/monitor_dispatch_internal.hpp"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <csignal>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <map>
#include <mutex>
#include <new>
#include <string>
#include <thread>
#include <vector>

#if !defined(_WIN32)
#include <unistd.h>
#endif

namespace {

static const char *k_pattern = "SPOT";
static const char *k_service_name = "perf-spot";
static const char *k_topic = "bench";
static const uint32_t k_metric_run_id = 1U;

static std::atomic<bool> g_stop_requested(false);
static std::atomic<bool> g_queue_probe_pending(false);
static std::atomic<size_t> g_queue_probe_size(0);

struct spot_server_ready_monitor_state_t
{
    spot_server_ready_monitor_state_t() :
        ready_count(0),
        error_code(0)
    {
    }

    std::mutex mutex;
    std::condition_variable cv;
    size_t ready_count;
    int error_code;
};

struct spot_server_ready_monitor_t
{
    spot_server_ready_monitor_t() : monitor(NULL), state(NULL) {}

    void *monitor;
    spot_server_ready_monitor_state_t *state;
};

struct spot_server_ready_monitor_registry_t
{
    std::mutex mutex;
    std::map<void *, spot_server_ready_monitor_state_t *> states;
};

spot_server_ready_monitor_registry_t &spot_server_ready_monitor_registry()
{
    static spot_server_ready_monitor_registry_t registry;
    return registry;
}

void register_spot_server_ready_monitor(
  void *monitor,
  spot_server_ready_monitor_state_t *state)
{
    if (!monitor || !state)
        return;

    spot_server_ready_monitor_registry_t &registry =
      spot_server_ready_monitor_registry();
    std::lock_guard<std::mutex> lock(registry.mutex);
    registry.states[monitor] = state;
}

void unregister_spot_server_ready_monitor(void *monitor)
{
    if (!monitor)
        return;

    spot_server_ready_monitor_registry_t &registry =
      spot_server_ready_monitor_registry();
    std::lock_guard<std::mutex> lock(registry.mutex);
    registry.states.erase(monitor);
}

spot_server_ready_monitor_state_t *find_spot_server_ready_monitor_state()
{
    void *monitor = zlink::current_monitor_dispatch_handle();
    if (!monitor)
        return NULL;

    spot_server_ready_monitor_registry_t &registry =
      spot_server_ready_monitor_registry();
    std::lock_guard<std::mutex> lock(registry.mutex);
    std::map<void *, spot_server_ready_monitor_state_t *>::iterator it =
      registry.states.find(monitor);
    return it != registry.states.end() ? it->second : NULL;
}

bool spot_event_matches_subject(const zlink_service_event_t *event,
                                const char *expected_subject)
{
    if (!event || !expected_subject || expected_subject[0] == '\0')
        return false;
    if ((event->detail_flags & ZLINK_EVENT_DETAIL_SUBJECT) == 0)
        return false;
    return std::strcmp(event->subject, expected_subject) == 0;
}

void spot_server_ready_monitor_handler(const zlink_service_event_t *event)
{
    spot_server_ready_monitor_state_t *state =
      find_spot_server_ready_monitor_state();
    if (!state || !event)
        return;

    {
        std::lock_guard<std::mutex> lock(state->mutex);
        switch (event->event_type) {
            case ZLINK_SPOT_PUB_FIRST_DELIVERY_READY_CHANGED:
                if (spot_event_matches_subject(event, k_topic))
                    state->ready_count = static_cast<size_t>(event->value);
                break;

            case ZLINK_MONITOR_EVENT_ERROR:
                if (state->error_code == 0)
                    state->error_code =
                      event->error_code != 0 ? event->error_code : EIO;
                break;

            default:
                break;
        }
    }

    state->cv.notify_all();
}

bool open_spot_server_ready_monitor(void *pub,
                                    spot_server_ready_monitor_t *out)
{
    if (!pub || !out)
        return false;

    spot_server_ready_monitor_state_t *state =
      new (std::nothrow) spot_server_ready_monitor_state_t();
    if (!state)
        return false;

    void *monitor = zlink_spot_monitor_open(
      pub,
      ZLINK_SPOT_ROLE_PUB,
      ZLINK_SPOT_PUB_FIRST_DELIVERY_READY_CHANGED | ZLINK_MONITOR_EVENT_ERROR,
      &spot_server_ready_monitor_handler);
    if (!monitor) {
        delete state;
        return false;
    }

    const int monitor_hwm = bench_hwm_from_env("PERF_MONITOR_HWM", 1000);
    set_sockopt_int(monitor, ZLINK_LINGER, 0, "ZLINK_LINGER");
    if (monitor_hwm > 0) {
        set_sockopt_int(monitor, ZLINK_SNDHWM, monitor_hwm, "ZLINK_SNDHWM");
        set_sockopt_int(monitor, ZLINK_RCVHWM, monitor_hwm, "ZLINK_RCVHWM");
    }

    register_spot_server_ready_monitor(monitor, state);
    out->monitor = monitor;
    out->state = state;
    return true;
}

bool wait_for_spot_ready(spot_server_ready_monitor_t *monitor,
                         size_t expected_count,
                         int timeout_ms)
{
    if (expected_count == 0)
        return true;
    if (!monitor || !monitor->state)
        return false;

    std::unique_lock<std::mutex> lock(monitor->state->mutex);
    if (monitor->state->error_code != 0)
        return false;
    if (monitor->state->ready_count >= expected_count)
        return true;

    return monitor->state->cv.wait_for(
             lock,
             std::chrono::milliseconds(std::max(1, timeout_ms)),
             [monitor, expected_count]() {
                 return monitor->state->error_code != 0
                        || monitor->state->ready_count >= expected_count;
             })
           && monitor->state->error_code == 0
           && monitor->state->ready_count >= expected_count;
}

void close_spot_server_ready_monitor(spot_server_ready_monitor_t *monitor)
{
    if (!monitor)
        return;

    spot_server_ready_monitor_state_t *state = monitor->state;
    void *handle = monitor->monitor;
    void *handle_id = handle;
    monitor->state = NULL;
    monitor->monitor = NULL;

    if (!handle && !state)
        return;

    if (handle && zlink_service_monitor_close(&handle) == 0) {
        unregister_spot_server_ready_monitor(handle_id);
        delete state;
        return;
    }
}

struct spot_server_state_t
{
    spot_server_state_t() :
        node(NULL),
        pub(NULL),
        msg_size(0),
        phase(perf_multi_metric::phase_unknown),
        next_seq(1),
        send_enabled(false),
        send_pending(false),
        fatal_errno(0)
    {
    }

    void *node;
    void *pub;
    std::mutex mutex;
    std::vector<char> payload;
    size_t msg_size;
    perf_multi_metric::phase_t phase;
    uint64_t next_seq;
    bool send_enabled;
    bool send_pending;
    int fatal_errno;
};

spot_server_state_t *g_server_state = NULL;

void discard_spot_parts(const zlink_routing_id_t *,
                        const char *,
                        size_t,
                        zlink_msg_t *parts,
                        size_t part_count)
{
    if (!parts)
        return;
    for (size_t i = 0; i < part_count; ++i)
        zlink_msg_close(&parts[i]);
}

bool is_supported_transport(const std::string &transport)
{
    return transport == "tcp" || transport == "tls" || transport == "ws"
           || transport == "wss";
}

void on_signal(int)
{
    g_stop_requested.store(true, std::memory_order_release);
}

void install_signal_handlers()
{
    std::signal(SIGINT, on_signal);
#if defined(SIGTERM)
    std::signal(SIGTERM, on_signal);
#endif
}

void request_queue_probe(size_t msg_size)
{
    if (msg_size == 0)
        return;
    g_queue_probe_size.store(msg_size, std::memory_order_release);
    g_queue_probe_pending.store(true, std::memory_order_release);
}

bool configure_spot_tls_server(void *node, const std::string &transport)
{
    if (transport != "tls" && transport != "wss")
        return true;

    static const std::string cert_path =
      write_temp_cert(test_certs::server_cert_pem, "multi_spot_srv_cert");
    static const std::string key_path =
      write_temp_cert(test_certs::server_key_pem, "multi_spot_srv_key");
    return zlink_spot_node_set_tls_server(node, cert_path.c_str(),
                                          key_path.c_str())
           == 0;
}

bool apply_spot_server_options(void *pub,
                               const multi_bench_settings_t &settings)
{
    const int linger_ms = 0;
    const int sndhwm = bench_hwm_from_env("PERF_MULTI_SNDHWM", settings.hwm);
    const int sndtimeo_ms =
      bench_timeout_ms_from_env("PERF_MULTI_SNDTIMEO_MS", 200);
    const int nodrop =
      resolve_multi_int_env("PERF_MULTI_SPOT_XPUB_NODROP", 1, 0);

    return zlink_spot_set_pub_option(pub, ZLINK_SPOT_PUB_OPT_LINGER,
                                     &linger_ms, sizeof(linger_ms))
             == 0
           && zlink_spot_set_pub_option(pub, ZLINK_SPOT_PUB_OPT_SNDHWM,
                                        &sndhwm, sizeof(sndhwm))
                == 0
           && zlink_spot_set_pub_option(pub, ZLINK_SPOT_PUB_OPT_SNDTIMEO,
                                        &sndtimeo_ms, sizeof(sndtimeo_ms))
                == 0
           && zlink_spot_set_pub_option(pub, ZLINK_SPOT_PUB_OPT_NODROP,
                                        &nodrop, sizeof(nodrop))
                == 0;
}

std::string replace_any_host_with_localhost(const std::string &endpoint)
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

std::string bind_spot_endpoint(void *node,
                               const std::string &transport,
                               const std::string &token)
{
    const int bind_port =
      resolve_multi_int_env("PERF_MULTI_SERVER_BIND_PORT", 0, 0);
    if (bind_port > 0) {
        const std::string endpoint = make_fixed_endpoint(transport, bind_port);
        if (!endpoint.empty() && zlink_spot_node_bind(node, endpoint.c_str()) == 0)
            return endpoint;

        std::cerr << "spot bind failed for " << endpoint << ": "
                  << zlink_strerror(zlink_errno()) << std::endl;
        return std::string();
    }

    int base_port = 32000;
#if !defined(_WIN32)
    base_port += static_cast<int>(::getpid() % 1000) * 8;
#endif
    (void) token;
    for (int attempt = 0; attempt < 64; ++attempt) {
        const std::string endpoint =
          make_fixed_endpoint(transport, base_port + attempt);
        if (!endpoint.empty()
            && zlink_spot_node_bind(node, endpoint.c_str()) == 0) {
            return replace_any_host_with_localhost(endpoint);
        }
    }

    return std::string();
}

server_queue_stats_t sample_spot_queue_stats(void *pub, bool send_pending)
{
    server_queue_stats_t stats;
    if (!pub)
        return stats;

    zlink_monitor_snapshot_t snapshot;
    if (read_spot_snapshot_once(pub, ZLINK_SPOT_ROLE_PUB, &snapshot)
        && (snapshot.detail_flags
            & ZLINK_MONITOR_SNAPSHOT_DETAIL_SND_PENDING_MSGS)) {
        stats.snd_pending_max = static_cast<double>(
          std::max<unsigned long long>(snapshot.snd_pending_msgs,
                                       send_pending ? 1ULL : 0ULL));
        return stats;
    }

    if (send_pending) {
        stats.snd_pending_max = 1.0;
    }
    return stats;
}

void emit_requested_queue_probe(const std::string &lib_name,
                                const std::string &transport)
{
    if (!g_queue_probe_pending.exchange(false, std::memory_order_acq_rel))
        return;

    const size_t msg_size = g_queue_probe_size.load(std::memory_order_acquire);
    spot_server_state_t *state = g_server_state;
    if (msg_size == 0 || !state || !state->pub)
        return;

    bool send_pending = false;
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        send_pending = state->send_pending;
    }
    const server_queue_stats_t queue_stats =
      sample_spot_queue_stats(state->pub, send_pending);
    print_server_queue_metrics(lib_name, k_pattern, transport, msg_size,
                               queue_stats);
}

enum send_status_t
{
    send_status_ok = 0,
    send_status_blocked = 1,
    send_status_fatal = 2
};

send_status_t try_publish_locked(spot_server_state_t *state)
{
    if (!state || !state->pub || state->msg_size == 0 || !state->send_enabled)
        return send_status_fatal;

    const size_t payload_size =
      std::max(state->msg_size, perf_multi_metric::header_size());
    if (state->payload.size() < payload_size)
        return send_status_fatal;

    if (!perf_multi_metric::stamp_payload(
          state->payload.data(),
          payload_size,
          k_metric_run_id,
          state->phase,
          state->msg_size,
          state->next_seq,
          perf_multi_metric::now_us())) {
        return send_status_fatal;
    }

    zlink_msg_t part;
    if (zlink_msg_init_data(
          &part,
          payload_size > 0
            ? static_cast<void *>(state->payload.data())
            : static_cast<void *>(NULL),
          payload_size,
          NULL,
          NULL)
        != 0) {
        return send_status_fatal;
    }

    const int rc =
      zlink_spot_publish(state->pub, k_topic, &part, 1, ZLINK_DONTWAIT);
    const int saved_errno = rc == 0 ? 0 : errno;
    (void) zlink_msg_close(&part);

    if (rc == 0) {
        state->send_pending = false;
        ++state->next_seq;
        return send_status_ok;
    }
    if (saved_errno == EAGAIN || saved_errno == EHOSTUNREACH
        || saved_errno == ENOTCONN || saved_errno == ETIMEDOUT) {
        state->send_pending = true;
        errno = saved_errno;
        return send_status_blocked;
    }

    errno = saved_errno;
    return send_status_fatal;
}

void spot_server_send_ready(void *subject)
{
    spot_server_state_t *state = g_server_state;
    if (!state || subject != state->pub)
        return;

    std::lock_guard<std::mutex> lock(state->mutex);
    if (!state->send_pending) {
        return;
    }
    if (!state->send_enabled) {
        state->send_pending = false;
        return;
    }

    const send_status_t rc = try_publish_locked(state);
    if (rc == send_status_fatal) {
        if (bench_debug_enabled ()) {
            std::cerr << "[multi-spot-server] send-ready fatal errno=" << errno
                      << std::endl;
        }
        state->fatal_errno = errno != 0 ? errno : EIO;
    }
}

void print_server_metrics(const std::string &lib_name,
                          const std::string &transport,
                          const std::vector<size_t> &sizes,
                          const bench_multi_resource_metrics_t &metrics,
                          const server_queue_stats_t &queue_stats)
{
    for (size_t i = 0; i < sizes.size(); ++i) {
        if (metrics.has_cpu_pct) {
            std::cout << "RESULT," << lib_name << "," << k_pattern << ","
                      << transport << "," << sizes[i]
                      << ",server_cpu_pct," << std::fixed
                      << std::setprecision(2) << metrics.cpu_pct << std::endl;
        }
        if (metrics.has_mem_mb) {
            std::cout << "RESULT," << lib_name << "," << k_pattern << ","
                      << transport << "," << sizes[i]
                      << ",server_mem_mb," << std::fixed
                      << std::setprecision(2) << metrics.mem_mb << std::endl;
        }
        print_server_queue_metrics(lib_name, k_pattern, transport, sizes[i],
                                   queue_stats);
    }
}

bool run_phase(spot_server_state_t *state,
               const std::string &lib_name,
               const std::string &transport,
               size_t msg_size,
               perf_multi_metric::phase_t phase,
               double duration_seconds,
               bool send_enabled)
{
    if (duration_seconds <= 0.0)
        return true;

    {
        std::lock_guard<std::mutex> lock(state->mutex);
        state->msg_size = msg_size;
        state->phase = phase;
        state->next_seq = 1;
        state->send_enabled = send_enabled;
        state->send_pending = false;
    }

    const auto deadline =
      std::chrono::steady_clock::now()
      + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
          std::chrono::duration<double>(duration_seconds));

    while (!g_stop_requested.load(std::memory_order_acquire)
           && std::chrono::steady_clock::now() < deadline) {
        emit_requested_queue_probe(lib_name, transport);

        bool progressed = false;
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            if (state->fatal_errno != 0)
                return false;
            if (state->send_enabled) {
                const send_status_t rc = try_publish_locked(state);
                if (rc == send_status_fatal) {
                    if (bench_debug_enabled ()) {
                        std::cerr << "[multi-spot-server] publish fatal errno="
                                  << errno << std::endl;
                    }
                    state->fatal_errno = errno != 0 ? errno : EIO;
                    return false;
                }
                progressed = rc == send_status_ok;
            }
        }

        if (!progressed)
            std::this_thread::yield();
    }

    {
        std::lock_guard<std::mutex> lock(state->mutex);
        state->send_enabled = false;
        state->send_pending = false;
        if (state->fatal_errno != 0)
            return false;
    }

    return !g_stop_requested.load(std::memory_order_acquire);
}

bool run_server_loop(spot_server_state_t *state,
                     const multi_bench_settings_t &settings,
                     const std::string &lib_name,
                     const std::string &transport,
                     const std::vector<size_t> &msg_sizes)
{
    const double warmup_seconds =
      static_cast<double>(std::max(0, settings.warmup_seconds));
    const double settle_seconds =
      static_cast<double>(std::max(0, settings.settle_ms)) / 1000.0;
    const double active_seconds =
      static_cast<double>(std::max(1, settings.duration_seconds));

    for (size_t i = 0; i < msg_sizes.size(); ++i) {
        if (g_stop_requested.load(std::memory_order_acquire))
            return false;

        if (!run_phase(state, lib_name, transport, msg_sizes[i],
                       perf_multi_metric::phase_warmup, warmup_seconds, true)
            || (settle_seconds > 0.0
                && !run_phase(state, lib_name, transport, msg_sizes[i],
                              perf_multi_metric::phase_drain, settle_seconds,
                              true))
            || !run_phase(state, lib_name, transport, msg_sizes[i],
                          perf_multi_metric::phase_active, active_seconds,
                          true)) {
            return false;
        }
    }

    return true;
}

int run_server_benchmark(const std::string &lib_name,
                         const std::string &transport)
{
    set_perf_multi_pattern_env(k_pattern);

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

    const multi_bench_settings_t settings = resolve_multi_bench_settings();
    std::vector<size_t> msg_sizes = resolve_bench_msg_sizes(64);
    if (msg_sizes.empty())
        msg_sizes.push_back(64);

    size_t max_msg_size = 64;
    for (size_t i = 0; i < msg_sizes.size(); ++i) {
        if (msg_sizes[i] > max_msg_size)
            max_msg_size = msg_sizes[i];
    }

    void *node =
      zlink_spot_node_new(ctx.get(), k_service_name, &discard_spot_parts);
    if (!node)
        return 1;

    if (!configure_spot_tls_server(node, transport)) {
        zlink_spot_node_destroy(&node);
        return 1;
    }

    const std::string endpoint =
      bind_spot_endpoint(node, transport,
                         lib_name + std::string("_spot_server"));
    if (endpoint.empty()) {
        zlink_spot_node_destroy(&node);
        return 1;
    }

    void *pub = zlink_spot_new(node, &discard_spot_parts);
    spot_server_ready_monitor_t pub_monitor;
    if (!pub || !apply_spot_server_options(pub, settings)
        || !open_spot_server_ready_monitor(pub, &pub_monitor)
        || zlink_spot_set_send_ready_handler(pub, &spot_server_send_ready)
             != 0) {
        close_spot_server_ready_monitor(&pub_monitor);
        if (pub)
            zlink_spot_destroy(&pub);
        zlink_spot_node_destroy(&node);
        return 1;
    }

    spot_server_state_t state;
    state.node = node;
    state.pub = pub;
    state.payload.assign(std::max<size_t>(max_msg_size,
                                          perf_multi_metric::header_size()),
                         's');
    g_server_state = &state;

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

    const bench_multi_cpu_sample_t sample_start =
      bench_multi_capture_cpu_sample();

    std::cout << "READY," << endpoint << std::endl;

    const size_t target_clients =
      resolve_multi_service_clients(settings.clients);
    if (!wait_for_spot_ready(&pub_monitor,
                             target_clients,
                             settings.connect_ready_timeout_ms)) {
        close_spot_server_ready_monitor(&pub_monitor);
        g_server_state = NULL;
        zlink_spot_destroy(&pub);
        zlink_spot_node_destroy(&node);
        return 1;
    }

    const bool ok =
      run_server_loop(&state, settings, lib_name, transport, msg_sizes);

    bool send_pending = false;
    {
        std::lock_guard<std::mutex> lock(state.mutex);
        send_pending = state.send_pending;
    }
    const bench_multi_resource_metrics_t metrics =
      bench_multi_finish_resource_probe(sample_start);
    const server_queue_stats_t queue_stats =
      sample_spot_queue_stats(pub, send_pending);
    print_server_metrics(lib_name, transport, msg_sizes, metrics, queue_stats);

    close_spot_server_ready_monitor(&pub_monitor);
    g_server_state = NULL;
    zlink_spot_destroy(&pub);
    zlink_spot_node_destroy(&node);
    return ok ? 0 : 1;
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
