#include "../common/perf_common.hpp"
#include "../common/perf_common_multi.hpp"
#include "../common/perf_multi_metric_header.hpp"
#include "../../../bench/with_zmq/multi/common/bench_multi_resource.hpp"
#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <condition_variable>
#include <mutex>
#include <new>
#include <set>
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

static const int k_spot_role_pub = 1;

static const char *k_pattern = "MULTI_SPOT";
static const char *k_service_name = "perf-spot";
static const char *k_topic = "bench";
static const uint32_t k_metric_run_id = 1U;

static std::atomic<bool> g_queue_probe_pending(false);
static std::atomic<size_t> g_queue_probe_size(0);

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
    size_t msg_size;
    perf_multi_metric::phase_t phase;
    uint64_t next_seq;
    std::atomic<bool> send_enabled;
    std::atomic<bool> send_pending;
    std::atomic<int> fatal_errno;
    std::mutex start_wait_mutex;
    std::condition_variable start_wait_cv;
    std::set<size_t> pending_start_sizes;
};

spot_server_state_t *g_server_state = NULL;

void discard_spot_parts(const zlink_routing_id_t *,
                        const char *,
                        size_t,
                        zlink_msg_t *parts,
                        size_t part_count,
                        void *)
{
    if (!parts)
        return;
    for (size_t i = 0; i < part_count; ++i)
        zlink_msg_close(&parts[i]);
}

void fast_exit_process(int exit_code)
{
    std::cout.flush();
    std::cerr.flush();
    std::_Exit(exit_code);
}

void request_queue_probe(size_t msg_size)
{
    if (msg_size == 0)
        return;
    g_queue_probe_size.store(msg_size, std::memory_order_release);
    g_queue_probe_pending.store(true, std::memory_order_release);
}

bool parse_start_command(const std::string &line, size_t *msg_size_out)
{
    static const char prefix[] = "START,";
    if (!msg_size_out
        || line.compare(0, sizeof(prefix) - 1, prefix) != 0) {
        return false;
    }

    const char *value = line.c_str() + (sizeof(prefix) - 1);
    char *end = NULL;
    const unsigned long long parsed = std::strtoull(value, &end, 10);
    if (!end || *end != '\0' || parsed == 0)
        return false;

    *msg_size_out = static_cast<size_t>(parsed);
    return true;
}

void emit_requested_queue_probe(const std::string &lib_name,
                                const std::string &transport);

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

    if (zlink_set_option(pub, ZLINK_OPT_LINGER, &linger_ms,
                             sizeof(linger_ms))
          != 0
        || zlink_set_option(pub, ZLINK_OPT_SNDHWM, &sndhwm,
                                sizeof(sndhwm))
             != 0
        || zlink_set_option(pub, ZLINK_OPT_SNDTIMEO, &sndtimeo_ms,
                                sizeof(sndtimeo_ms))
             != 0
        || zlink_set_pub_option(pub, ZLINK_PUB_OPT_NODROP, &nodrop,
                                sizeof(nodrop))
             != 0) {
        return false;
    }

    if (sndbuf > 0
        && zlink_set_option(pub, ZLINK_OPT_SNDBUF, &sndbuf,
                                sizeof(sndbuf))
             != 0) {
        return false;
    }

    if (rcvbuf > 0
        && zlink_set_option(pub, ZLINK_OPT_RCVBUF, &rcvbuf,
                                sizeof(rcvbuf))
             != 0) {
        return false;
    }

    return true;
}

std::string bind_spot_endpoint(void *node,
                               const std::string &transport,
                               const std::string &token)
{
    const int bind_port =
      resolve_multi_int_env("PERF_MULTI_SERVER_BIND_PORT", 0, 0);
    if (bind_port > 0) {
        return perf_bind_endpoint_once(node,
                                       make_fixed_endpoint(transport, bind_port),
                                       transport,
                                       &perf_bind_spot_node_endpoint,
                                       false);
    }

    int base_port = 32000;
#if !defined(_WIN32)
    base_port += static_cast<int>(::getpid() % 1000) * 8;
#endif
    (void) token;
    return perf_bind_fixed_endpoint_range(
      node, transport, base_port, 64, &perf_bind_spot_node_endpoint);
}

server_queue_stats_t sample_spot_queue_stats(void *pub, bool send_pending)
{
    server_queue_stats_t stats;
    (void) pub;
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

    const bool send_pending =
      state->send_pending.load(std::memory_order_acquire);
    const server_queue_stats_t queue_stats =
      sample_spot_queue_stats(state->pub, send_pending);
    print_server_queue_metrics(lib_name, k_pattern, transport, msg_size,
                               queue_stats);
}

void notify_size_start(spot_server_state_t *state, size_t msg_size)
{
    if (!state || msg_size == 0)
        return;

    {
        std::lock_guard<std::mutex> lock(state->start_wait_mutex);
        state->pending_start_sizes.insert(msg_size);
    }
    state->start_wait_cv.notify_all();
}

bool wait_for_size_start(spot_server_state_t *state,
                         size_t msg_size,
                         int timeout_ms)
{
    if (!state || msg_size == 0)
        return false;

    std::unique_lock<std::mutex> lock(state->start_wait_mutex);
    if (state->pending_start_sizes.erase(msg_size) != 0)
        return true;

    const bool signaled = state->start_wait_cv.wait_for(
      lock,
      std::chrono::milliseconds(std::max(1, timeout_ms)),
      [state, msg_size]() {
          return perf_stop_requested ().load(std::memory_order_acquire)
                 || state->fatal_errno.load(std::memory_order_acquire) != 0
                 || state->pending_start_sizes.count(msg_size) != 0;
      });
    return signaled && state->pending_start_sizes.erase(msg_size) != 0;
}


bool wait_for_spot_send_progress(bool send_enabled)
{
    return perf_socket_poll(NULL, 0, send_enabled ? 2 : 1) >= 0;
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
      zlink_publish(state->pub, k_topic, &part, 1, ZLINK_DONTWAIT);
    const int saved_errno = rc == 0 ? 0 : errno;
    (void) zlink_msg_close(&part);

    if (rc == 0) {
        state->send_pending.store(false, std::memory_order_release);
        ++state->next_seq;
        return send_status_ok;
    }
    if (saved_errno == EAGAIN || saved_errno == EHOSTUNREACH
        || saved_errno == ENOTCONN || saved_errno == ETIMEDOUT) {
        state->send_pending.store(true, std::memory_order_release);
        errno = saved_errno;
        return send_status_blocked;
    }

    errno = saved_errno;
    return send_status_fatal;
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
    if (bench_transition_debug_enabled()) {
        std::cerr << "[multi-spot-server] phase start ts_us="
                  << perf_multi_metric::now_us()
                  << " size=" << msg_size
                  << " phase=" << static_cast<int>(phase)
                  << " send=" << (send_enabled ? 1 : 0) << std::endl;
    }

    const auto deadline =
      std::chrono::steady_clock::now()
      + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
          std::chrono::duration<double>(duration_seconds));

    while (!perf_stop_requested ().load(std::memory_order_acquire)
           && std::chrono::steady_clock::now() < deadline) {
        emit_requested_queue_probe(lib_name, transport);

        bool progressed = false;
        if (state->fatal_errno.load(std::memory_order_acquire) != 0)
            return false;
        if (state->send_enabled.load(std::memory_order_acquire)) {
            const send_status_t rc = try_publish_locked(state);
            if (rc == send_status_fatal) {
                state->fatal_errno.store(errno != 0 ? errno : EIO,
                                         std::memory_order_release);
                return false;
            }
            progressed = rc == send_status_ok;
        }

        if (!progressed) {
            wait_for_spot_send_progress(send_enabled);
        }
    }

    state->send_enabled.store(false, std::memory_order_release);
    state->send_pending.store(false, std::memory_order_release);
    if (bench_transition_debug_enabled()) {
        std::cerr << "[multi-spot-server] phase done ts_us="
                  << perf_multi_metric::now_us()
                  << " size=" << msg_size
                  << " phase=" << static_cast<int>(phase)
                  << " sent="
                  << (state->next_seq > 0 ? state->next_seq - 1 : 0)
                  << " stop="
                  << (perf_stop_requested ().load(std::memory_order_acquire) ? 1 : 0)
                  << " fatal_errno="
                  << state->fatal_errno.load(std::memory_order_acquire)
                  << std::endl;
    }
    if (state->fatal_errno.load(std::memory_order_acquire) != 0)
        return false;

    // The multi runner stops the server after the client completes a size case.
    // Treat that external stop as graceful so the next size can run.
    return true;
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
    const int start_timeout_ms =
      std::max(settings.connect_ready_timeout_ms,
               std::max(1000, settings.connect_ready_timeout_ms * 6));

    for (size_t i = 0; i < msg_sizes.size(); ++i) {
        if (perf_stop_requested ().load(std::memory_order_acquire)) {
            if (bench_transition_debug_enabled()) {
                std::cerr << "[multi-spot-server] loop stop before size="
                          << msg_sizes[i] << std::endl;
            }
            return state->fatal_errno.load(std::memory_order_acquire) == 0;
        }

        if (bench_transition_debug_enabled()) {
            std::cerr << "[multi-spot-server] start wait begin ts_us="
                      << perf_multi_metric::now_us()
                      << " size=" << msg_sizes[i]
                      << " timeout_ms=" << start_timeout_ms << std::endl;
        }
        if (!wait_for_size_start(state, msg_sizes[i], start_timeout_ms)) {
            if (bench_transition_debug_enabled()) {
                std::cerr << "[multi-spot-server] start wait timeout ts_us="
                          << perf_multi_metric::now_us()
                          << " size=" << msg_sizes[i] << std::endl;
            }
            return false;
        }
        if (bench_transition_debug_enabled()) {
            std::cerr << "[multi-spot-server] start wait done ts_us="
                      << perf_multi_metric::now_us()
                      << " size=" << msg_sizes[i] << std::endl;
        }

        if (!run_phase(state, lib_name, transport, msg_sizes[i],
                       perf_multi_metric::phase_warmup, warmup_seconds, true)
            || (settle_seconds > 0.0
                && !run_phase(state, lib_name, transport, msg_sizes[i],
                              perf_multi_metric::phase_drain, settle_seconds,
                              false))
            || !run_phase(state, lib_name, transport, msg_sizes[i],
                          perf_multi_metric::phase_active, active_seconds,
                          true)) {
            if (bench_transition_debug_enabled()) {
                std::cerr << "[multi-spot-server] loop abort size="
                          << msg_sizes[i]
                          << " stop="
                          << (perf_stop_requested ().load(std::memory_order_acquire)
                                ? 1
                                : 0)
                          << " fatal_errno="
                          << state->fatal_errno.load(std::memory_order_acquire)
                          << std::endl;
            }
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

    void *node = zlink_spot_node_new(ctx.get(), k_service_name);
    if (!node) {
        if (bench_debug_enabled())
            std::cerr << "[multi-spot-server] node create failed err="
                      << zlink_errno() << std::endl;
        return 1;
    }

    if (!setup_tls_server(node, transport)) {
        if (bench_debug_enabled())
            std::cerr << "[multi-spot-server] tls configure failed err="
                      << zlink_errno() << std::endl;
        zlink_spot_node_destroy(&node);
        return 1;
    }

    if (!apply_spot_server_options(node, settings)) {
        if (bench_debug_enabled())
            std::cerr << "[multi-spot-server] pub init failed err="
                      << zlink_errno() << std::endl;
        zlink_spot_node_destroy(&node);
        return 1;
    }

    const std::string endpoint =
      bind_spot_endpoint(node, transport,
                         lib_name + std::string("_spot_server"));
    if (endpoint.empty()) {
        if (bench_debug_enabled())
            std::cerr << "[multi-spot-server] bind failed err="
                      << zlink_errno() << std::endl;
        zlink_spot_node_destroy(&node);
        return 1;
    }

    void *pub = node;

    spot_server_state_t state;
    state.node = node;
    state.pub = pub;
    g_server_state = &state;

    perf_stop_requested ().store(false, std::memory_order_release);
    g_queue_probe_pending.store(false, std::memory_order_release);
    g_queue_probe_size.store(0, std::memory_order_release);
    install_perf_signal_handlers();

    std::thread stdin_watcher([]() {
        std::string line;
        while (std::getline(std::cin, line)) {
            size_t queue_size = 0;
            size_t start_size = 0;
            if (parse_queue_probe_command(line, &queue_size)) {
                request_queue_probe(queue_size);
                continue;
            }
            if (parse_start_command(line, &start_size)) {
                notify_size_start(g_server_state, start_size);
                continue;
            }
            if (line == "STOP" || line == "QUIT") {
                perf_stop_requested ().store(true, std::memory_order_release);
                return;
            }
        }
    });
    stdin_watcher.detach();

    const bench_multi_cpu_sample_t sample_start =
      bench_multi_capture_cpu_sample();

    std::cout << "READY," << endpoint << std::endl;
    if (bench_transition_debug_enabled()) {
        std::cerr << "[multi-spot-server] phase gate open ts_us="
                  << perf_multi_metric::now_us()
                  << std::endl;
    }

    const bool ok =
      run_server_loop(&state, settings, lib_name, transport, msg_sizes);
    if (bench_transition_debug_enabled()) {
        std::cerr << "[multi-spot-server] benchmark done ok=" << (ok ? 1 : 0)
                  << " stop="
                  << (perf_stop_requested ().load(std::memory_order_acquire) ? 1 : 0)
                  << " fatal_errno="
                  << state.fatal_errno.load(std::memory_order_acquire)
                  << std::endl;
    }

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
    if (!multi_perf_validate_recv_mode_for_pattern (k_pattern))
        return 1;

    const std::string lib_name = argv[1];
    const std::string transport = argv[2];
    return run_server_benchmark(lib_name, transport);
}
