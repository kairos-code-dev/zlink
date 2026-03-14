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
#include <cstdlib>
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

namespace zlink {
class spot_node_t;
class spot_pub_t;
class spot_sub_t;
}

namespace {

static const char *k_pattern = "SPOT";
static const char *k_service_name = "perf-spot";
static const char *k_topic = "bench";
static const uint32_t k_metric_run_id = 1U;
static const uint32_t k_spot_handle_tag = 0x1e6700dcU;

struct perf_spot_handle_t
{
    uint32_t tag;
    zlink::spot_node_t *node;
    zlink::spot_pub_t *pub;
    zlink::spot_sub_t *sub;
    zlink_spot_handler_fn handler;
};

struct perf_spot_pub_layout_t
{
    zlink::spot_node_t *node;
    void *socket;
};

void *spot_pub_socket(void *spot)
{
    perf_spot_handle_t *handle = static_cast<perf_spot_handle_t *>(spot);
    if (!handle || handle->tag != k_spot_handle_tag || !handle->pub)
        return NULL;
    return reinterpret_cast<perf_spot_pub_layout_t *>(handle->pub)->socket;
}

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
            case ZLINK_MONITOR_EVENT_PEER_UP:
            case ZLINK_MONITOR_EVENT_READY: {
                zlink_monitor_snapshot_t snapshot;
                std::memset(&snapshot, 0, sizeof(snapshot));
                void *monitor = zlink::current_monitor_dispatch_handle();
                if (monitor && zlink_monitor_snapshot(monitor, &snapshot) == 0)
                    state->ready_count = snapshot.ready_peer_count;
                break;
            }

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
      ZLINK_MONITOR_EVENT_PEER_UP | ZLINK_MONITOR_EVENT_READY
        | ZLINK_MONITOR_EVENT_ERROR,
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
    zlink_monitor_snapshot_t snapshot;
    std::memset(&snapshot, 0, sizeof(snapshot));
    if (zlink_monitor_snapshot(monitor, &snapshot) == 0)
        state->ready_count = snapshot.ready_peer_count;
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

    auto snapshot_ready = [monitor, expected_count]() {
        if (!monitor->monitor)
            return false;
        zlink_monitor_snapshot_t snapshot;
        std::memset(&snapshot, 0, sizeof(snapshot));
        return zlink_monitor_snapshot(monitor->monitor, &snapshot) == 0
               && snapshot.ready_peer_count >= expected_count;
    };

    std::unique_lock<std::mutex> lock(monitor->state->mutex);
    if (monitor->state->error_code != 0)
        return false;
    if (monitor->state->ready_count >= expected_count || snapshot_ready()) {
        monitor->state->ready_count = expected_count;
        return true;
    }

    const auto deadline =
      std::chrono::steady_clock::now()
      + std::chrono::milliseconds(std::max(1, timeout_ms));
    while (monitor->state->error_code == 0) {
        if (monitor->state->ready_count >= expected_count || snapshot_ready()) {
            monitor->state->ready_count = expected_count;
            return true;
        }
        if (monitor->state->cv.wait_until(lock, deadline)
            == std::cv_status::timeout) {
            break;
        }
    }
    return monitor->state->error_code == 0
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
        fatal_errno(0),
        send_wake_epoch(0)
    {
    }

    void *node;
    void *pub;
    size_t msg_size;
    perf_multi_metric::phase_t phase;
    uint64_t next_seq;
    std::mutex send_wait_mutex;
    std::condition_variable send_wait_cv;
    std::atomic<bool> send_enabled;
    std::atomic<bool> send_pending;
    std::atomic<int> fatal_errno;
    std::atomic<uint64_t> send_wake_epoch;
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

void notify_spot_server_send_progress(spot_server_state_t *state)
{
    if (!state)
        return;

    state->send_wake_epoch.fetch_add(1, std::memory_order_acq_rel);
    state->send_wait_cv.notify_all();
}

void spot_server_send_ready(void *subject)
{
    spot_server_state_t *state = g_server_state;
    if (!state || subject != state->pub)
        return;

    notify_spot_server_send_progress(state);
}

bool is_supported_transport(const std::string &transport)
{
    return transport == "tcp" || transport == "tls" || transport == "ws"
           || transport == "wss";
}

void fast_exit_process(int exit_code)
{
    std::cout.flush();
    std::cerr.flush();
    std::_Exit(exit_code);
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

void emit_requested_queue_probe(const std::string &lib_name,
                                const std::string &transport);

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
    const int sndbuf = bench_socket_buffer_bytes_from_env("PERF_SNDBUF", -1);
    const int rcvbuf = bench_socket_buffer_bytes_from_env("PERF_RCVBUF", -1);

    if (zlink_spot_set_pub_option(pub, ZLINK_SPOT_PUB_OPT_LINGER,
                                  &linger_ms, sizeof(linger_ms))
          != 0
        || zlink_spot_set_pub_option(pub, ZLINK_SPOT_PUB_OPT_SNDHWM,
                                     &sndhwm, sizeof(sndhwm))
             != 0
        || zlink_spot_set_pub_option(pub, ZLINK_SPOT_PUB_OPT_SNDTIMEO,
                                     &sndtimeo_ms, sizeof(sndtimeo_ms))
             != 0
        || zlink_spot_set_pub_option(pub, ZLINK_SPOT_PUB_OPT_NODROP,
                                     &nodrop, sizeof(nodrop))
             != 0) {
        return false;
    }

    if (sndbuf > 0
        && zlink_spot_set_pub_option(pub, ZLINK_SPOT_PUB_OPT_SNDBUF,
                                     &sndbuf, sizeof(sndbuf))
             != 0) {
        return false;
    }

    if (rcvbuf > 0
        && zlink_spot_set_pub_option(pub, ZLINK_SPOT_PUB_OPT_RCVBUF,
                                     &rcvbuf, sizeof(rcvbuf))
             != 0) {
        return false;
    }

    return true;
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

int resolve_spot_queue_drain_timeout_ms(const multi_bench_settings_t &settings,
                                        size_t msg_size)
{
    int timeout_ms =
      std::max(settings.connect_ready_timeout_ms,
               std::max(1, settings.duration_seconds) * 5000);
    if (msg_size >= 131072) {
        timeout_ms =
          std::max(timeout_ms,
                   std::max(30000, settings.connect_ready_timeout_ms * 6));
    }

    return resolve_multi_int_env("PERF_MULTI_SPOT_QUEUE_DRAIN_TIMEOUT_MS",
                                 timeout_ms,
                                 1);
}

bool wait_for_spot_queue_drain(spot_server_state_t *state,
                               const multi_bench_settings_t &settings,
                               const std::string &lib_name,
                               const std::string &transport,
                               size_t msg_size,
                               const char *phase_name)
{
    if (!state || !state->pub)
        return false;

    const auto deadline =
      std::chrono::steady_clock::now()
      + std::chrono::milliseconds(
          resolve_spot_queue_drain_timeout_ms(settings, msg_size));

    while (!g_stop_requested.load(std::memory_order_acquire)) {
        emit_requested_queue_probe(lib_name, transport);

        if (state->fatal_errno.load(std::memory_order_acquire) != 0)
            return false;
        const bool send_pending =
          state->send_pending.load(std::memory_order_acquire);

        const server_queue_stats_t queue_stats =
          sample_spot_queue_stats(state->pub, send_pending);
        if (!send_pending && queue_stats.snd_pending_max <= 0.0)
            return true;

        if (std::chrono::steady_clock::now() >= deadline) {
            if (bench_debug_enabled()) {
                std::cerr << "[multi-spot-server] queue drain timeout phase="
                          << (phase_name ? phase_name : "?")
                          << " size=" << msg_size
                          << " snd_pending_max=" << queue_stats.snd_pending_max
                          << " send_pending=" << send_pending << std::endl;
            }
            return false;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    return false;
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

    const bool send_pending =
      state->send_pending.load(std::memory_order_acquire);
    const server_queue_stats_t queue_stats =
      sample_spot_queue_stats(state->pub, send_pending);
    print_server_queue_metrics(lib_name, k_pattern, transport, msg_size,
                               queue_stats);
}


bool wait_for_spot_send_progress(spot_server_state_t *state,
                                 uint64_t observed_epoch,
                                 bool send_enabled)
{
    if (!state)
        return false;

    std::unique_lock<std::mutex> lock(state->send_wait_mutex);
    state->send_wait_cv.wait_for(
      lock,
      std::chrono::milliseconds(send_enabled ? 2 : 1),
      [state, observed_epoch]() {
          return g_stop_requested.load(std::memory_order_acquire)
                 || state->fatal_errno.load(std::memory_order_acquire) != 0
                 || state->send_wake_epoch.load(std::memory_order_acquire)
                      != observed_epoch;
      });
    return true;
}

enum send_status_t
{
    send_status_ok = 0,
    send_status_blocked = 1,
    send_status_fatal = 2
};

send_status_t try_publish_locked(spot_server_state_t *state)
{
    if (!state || !state->pub || state->msg_size == 0
        || !state->send_enabled.load(std::memory_order_acquire))
        return send_status_fatal;

    const size_t payload_size =
      std::max(state->msg_size, perf_multi_metric::header_size());
    zlink_msg_t part;
    if (zlink_msg_init_size(&part, payload_size) != 0) {
        return send_status_fatal;
    }
    if (!perf_multi_metric::stamp_payload(
          zlink_msg_data(&part),
          payload_size,
          k_metric_run_id,
          state->phase,
          state->msg_size,
          state->next_seq,
          perf_multi_metric::now_us())) {
        zlink_msg_close(&part);
        return send_status_fatal;
    }

    const int rc =
      zlink_spot_publish(state->pub, k_topic, &part, 1, ZLINK_DONTWAIT);
    const int saved_errno = rc == 0 ? 0 : errno;
    (void) zlink_msg_close(&part);

    if (rc == 0) {
        if (bench_debug_enabled () && state->next_seq == 1) {
            std::cerr << "[multi-spot-server] first send ok size="
                      << state->msg_size << " phase="
                      << static_cast<int> (state->phase) << std::endl;
        }
        state->send_pending.store(false, std::memory_order_release);
        ++state->next_seq;
        return send_status_ok;
    }
    if (saved_errno == EAGAIN || saved_errno == EHOSTUNREACH
        || saved_errno == ENOTCONN || saved_errno == ETIMEDOUT) {
        if (bench_debug_enabled () && state->next_seq == 1) {
            std::cerr << "[multi-spot-server] first send blocked size="
                      << state->msg_size << " phase="
                      << static_cast<int> (state->phase)
                      << " errno=" << saved_errno << std::endl;
        }
        state->send_pending.store(true, std::memory_order_release);
        errno = saved_errno;
        return send_status_blocked;
    }

    errno = saved_errno;
    return send_status_fatal;
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

    state->msg_size = msg_size;
    state->phase = phase;
    state->next_seq = 1;
    state->send_enabled.store(send_enabled, std::memory_order_release);
    state->send_pending.store(false, std::memory_order_release);

    const auto deadline =
      std::chrono::steady_clock::now()
      + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
          std::chrono::duration<double>(duration_seconds));

    while (!g_stop_requested.load(std::memory_order_acquire)
           && std::chrono::steady_clock::now() < deadline) {
        emit_requested_queue_probe(lib_name, transport);

        bool progressed = false;
        if (state->fatal_errno.load(std::memory_order_acquire) != 0)
            return false;
        if (state->send_enabled.load(std::memory_order_acquire)) {
            const send_status_t rc = try_publish_locked(state);
            if (rc == send_status_fatal) {
                if (bench_debug_enabled ()) {
                    std::cerr << "[multi-spot-server] publish fatal errno="
                              << errno << std::endl;
                }
                state->fatal_errno.store(errno != 0 ? errno : EIO,
                                         std::memory_order_release);
                return false;
            }
            progressed = rc == send_status_ok;
        }

        if (!progressed) {
            const uint64_t wake_epoch =
              state->send_wake_epoch.load(std::memory_order_acquire);
            wait_for_spot_send_progress(state, wake_epoch, send_enabled);
        }
    }

    state->send_enabled.store(false, std::memory_order_release);
    state->send_pending.store(false, std::memory_order_release);
    if (bench_debug_enabled ()) {
        std::cerr << "[multi-spot-server] phase done size=" << msg_size
                  << " phase=" << static_cast<int> (phase)
                  << " sent=" << (state->next_seq > 0 ? state->next_seq - 1 : 0)
                  << " fatal_errno="
                  << state->fatal_errno.load(std::memory_order_acquire)
                  << std::endl;
    }
    if (state->fatal_errno.load(std::memory_order_acquire) != 0)
        return false;

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
                              false))
            || !wait_for_spot_queue_drain(state,
                                          settings,
                                          lib_name,
                                          transport,
                                          msg_sizes[i],
                                          "warmup")
            || !run_phase(state, lib_name, transport, msg_sizes[i],
                          perf_multi_metric::phase_active, active_seconds, true)
            || !wait_for_spot_queue_drain(state,
                                          settings,
                                          lib_name,
                                          transport,
                                          msg_sizes[i],
                                          "active")) {
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

    void *node = zlink_spot_node_new(ctx.get(), k_service_name, NULL);
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
        || zlink_spot_set_send_ready_handler(pub, &spot_server_send_ready) != 0
        || !open_spot_server_ready_monitor(pub, &pub_monitor)) {
        close_spot_server_ready_monitor(&pub_monitor);
        if (pub)
            zlink_spot_destroy(&pub);
        zlink_spot_node_destroy(&node);
        return 1;
    }

    spot_server_state_t state;
    state.node = node;
    state.pub = pub;
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
    });
    stdin_watcher.detach();

    const bench_multi_cpu_sample_t sample_start =
      bench_multi_capture_cpu_sample();

    const size_t service_clients =
      resolve_multi_service_clients(settings.clients);
    const int peer_wait_ms =
      std::max(5000, settings.connect_ready_timeout_ms * 3);
    std::cout << "READY," << endpoint << std::endl;
    if (!wait_for_spot_ready(&pub_monitor, service_clients, peer_wait_ms)
        && bench_debug_enabled()) {
        std::cerr << "[multi-spot-server] ready wait timeout transport="
                  << transport << " expected=" << service_clients
                  << std::endl;
    }
    close_spot_server_ready_monitor(&pub_monitor);

    const bool ok =
      run_server_loop(&state, settings, lib_name, transport, msg_sizes);

    const bool send_pending =
      state.send_pending.load(std::memory_order_acquire);
    const bench_multi_resource_metrics_t metrics =
      bench_multi_finish_resource_probe(sample_start);
    const server_queue_stats_t queue_stats =
      sample_spot_queue_stats(pub, send_pending);
    print_server_metrics(lib_name, transport, msg_sizes, metrics, queue_stats);
    fast_exit_process(ok ? 0 : 1);
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
