#include "../common/perf_common.hpp"
#include "../common/perf_common_multi.hpp"
#include "../common/perf_multi_handshake.hpp"
#include "../common/perf_multi_metric_header.hpp"
#include "../common/perf_multi_spot_control.hpp"
#include "../common/perf_multi_spot_handshake.hpp"
#include "../common/perf_multi_spot_phase.hpp"
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

static const int k_spot_role_pub = 1;

static const char *k_pattern = "MULTI_SPOT";
static const char *k_service_name = "perf-spot";
static const char *k_topic = "bench";
static const uint32_t k_metric_run_id = 1U;

static std::atomic<bool> g_queue_probe_pending(false);
static std::atomic<size_t> g_queue_probe_size(0);

void ensure_multi_spot_mesh_pub_budget_default()
{
    // Keep perf aligned with the core default unless the caller overrides it.
}

struct spot_server_state_t
{
    spot_server_state_t() :
        node(NULL),
        pub(NULL),
        control_node(NULL),
        control_pub(NULL),
        control_sub(NULL),
        msg_size(0),
        phase(perf_multi_metric::phase_unknown),
        next_seq(1),
        expected_ready_count(1),
        send_enabled(false),
        send_pending(false),
        fatal_errno(0)
    {
    }

    void *node;
    void *pub;
    void *control_node;
    void *control_pub;
    void *control_sub;
    size_t msg_size;
    perf_multi_metric::phase_t phase;
    uint64_t next_seq;
    size_t expected_ready_count;
    std::atomic<bool> send_enabled;
    std::atomic<bool> send_pending;
    std::atomic<int> fatal_errno;
    perf_multi_handshake::start_signal_state_t start_gate;
    perf_multi_spot_handshake::ready_state_t ready_state;
    perf_multi_spot_handshake::control_peer_registry_t control_peers;
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

bool parse_connect_command(const std::string &line, std::string *endpoint_out)
{
    return perf_multi_handshake::parse_endpoint_command_line(
      line, "CONNECT_CONTROL,", endpoint_out);
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
    perf_multi_handshake::signal_start(&state->start_gate, msg_size);
}

bool ensure_control_peers_connected(spot_server_state_t *state)
{
    return state
             ? perf_multi_spot_control::ensure_connected_peers(
                 state->control_node, state->control_peers)
             : false;
}

void disconnect_control_peers(spot_server_state_t *state)
{
    if (!state)
        return;
    perf_multi_spot_control::disconnect_peers (
      state->control_node, state->control_peers);
}

void record_ready_slot(spot_server_state_t *state,
                       size_t msg_size,
                       size_t slot_index)
{
    if (!state || msg_size == 0)
        return;

    perf_multi_spot_handshake::record_ready_slot(
      &state->ready_state, msg_size, slot_index);
    if (bench_transition_debug_enabled()) {
        std::cerr << "[multi-spot-server] ready slot ts_us="
                  << perf_multi_metric::now_us()
                  << " size=" << msg_size
                  << " slot=" << slot_index << std::endl;
    }
}

void record_ready_count(spot_server_state_t *state,
                        size_t msg_size,
                        size_t ready_count)
{
    if (!state || msg_size == 0 || ready_count == 0)
        return;

    perf_multi_spot_handshake::record_ready_count(
      &state->ready_state, msg_size, ready_count);
}

bool wait_for_ready_slots(spot_server_state_t *state,
                          size_t msg_size,
                          int timeout_ms)
{
    if (!state || msg_size == 0)
        return false;
    return perf_multi_spot_control::wait_for_ready_units(
      state->control_sub,
      k_topic,
      &state->ready_state,
      msg_size,
      state->expected_ready_count,
      timeout_ms,
      &state->fatal_errno);
}

bool publish_control_start(spot_server_state_t *state, size_t msg_size)
{
    if (!state || !state->control_pub || msg_size == 0)
        return false;

    const bool ok =
      perf_multi_spot_control::publish_start (
        state->control_pub, k_topic, msg_size);
    const int saved_errno = ok ? 0 : errno;
    if (ok)
        return true;
    errno = saved_errno;
    return false;
}

bool wait_for_size_start(spot_server_state_t *state,
                         size_t msg_size,
                         int timeout_ms)
{
    if (!state || msg_size == 0)
        return false;
    return perf_multi_handshake::wait_for_start(
      &state->start_gate, msg_size, timeout_ms);
}

bool run_phase(spot_server_state_t *state,
               const std::string &lib_name,
               const std::string &transport,
               size_t msg_size,
               perf_multi_metric::phase_t phase,
               double duration_seconds,
               bool send_enabled);

bool wait_for_size_start_hook(void *state, size_t msg_size, int timeout_ms)
{
    return wait_for_size_start(
      static_cast<spot_server_state_t *>(state), msg_size, timeout_ms);
}

bool ensure_control_peers_connected_hook(void *state)
{
    return ensure_control_peers_connected(static_cast<spot_server_state_t *>(state));
}

bool wait_for_ready_slots_hook(void *state, size_t msg_size, int timeout_ms)
{
    return wait_for_ready_slots(
      static_cast<spot_server_state_t *>(state), msg_size, timeout_ms);
}

bool publish_control_start_hook(void *state, size_t msg_size)
{
    return publish_control_start(
      static_cast<spot_server_state_t *>(state), msg_size);
}

bool run_phase_hook(void *state,
                    const std::string &lib_name,
                    const std::string &transport,
                    size_t msg_size,
                    perf_multi_metric::phase_t phase,
                    double duration_seconds,
                    bool send_enabled)
{
    return run_phase(static_cast<spot_server_state_t *>(state),
                     lib_name,
                     transport,
                     msg_size,
                     phase,
                     duration_seconds,
                     send_enabled);
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
    if (saved_errno == EAGAIN) {
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
    print_server_metrics_for_sizes(
      lib_name, k_pattern, transport, sizes, metrics, &queue_stats);
}

bool run_server_loop(spot_server_state_t *state,
                     const multi_bench_settings_t &settings,
                     const std::string &lib_name,
                     const std::string &transport,
                     const std::vector<size_t> &msg_sizes)
{
    perf_multi_spot_phase::server_hooks_t hooks;
    hooks.wait_for_size_start = &wait_for_size_start_hook;
    hooks.ensure_control_peers_connected = &ensure_control_peers_connected_hook;
    hooks.wait_for_ready_slots = &wait_for_ready_slots_hook;
    hooks.publish_control_start = &publish_control_start_hook;
    hooks.run_phase = &run_phase_hook;
    hooks.fatal_errno = NULL;
    return perf_multi_spot_phase::run_server_cases(
      state,
      "multi-spot-server",
      settings,
      lib_name,
      transport,
      msg_sizes,
      hooks);
}

int run_server_benchmark(const std::string &lib_name,
                         const std::string &transport)
{
    set_perf_multi_pattern_env(k_pattern);
    ensure_multi_spot_mesh_pub_budget_default();

    if (!is_supported_transport(transport)) {
        std::cout << "UNSUPPORTED," << lib_name << "," << k_pattern << ","
                  << transport << std::endl;
        return 0;
    }
    if (!transport_available(transport)) {
        std::cerr << "transport unavailable: " << transport << std::endl;
        return 1;
    }

    const multi_bench_settings_t settings = resolve_multi_bench_settings();
    ctx_guard_t ctx;
    if (!ctx.valid())
        return 1;
    std::vector<size_t> msg_sizes = resolve_bench_msg_sizes(64);
    if (msg_sizes.empty())
        msg_sizes.push_back(64);

    size_t max_msg_size = 64;
    for (size_t i = 0; i < msg_sizes.size(); ++i) {
        if (msg_sizes[i] > max_msg_size)
            max_msg_size = msg_sizes[i];
    }

    perf_multi_spot_control::server_session_t session;
    if (!perf_multi_spot_control::initialize_server_session(
          ctx,
          transport,
          lib_name + std::string("_spot_server"),
          k_topic,
          settings,
          &apply_spot_server_options,
          &session)) {
        if (bench_debug_enabled())
            std::cerr << "[multi-spot-server] server session init failed err="
                      << zlink_errno() << std::endl;
        return 1;
    }

    spot_server_state_t state;
    g_server_state = &state;
    const int connect_ready_timeout_ms = settings.connect_ready_timeout_ms;

    g_queue_probe_pending.store(false, std::memory_order_release);
    g_queue_probe_size.store(0, std::memory_order_release);
    perf_multi_spot_control::prepare_server_runtime(
      &state,
      session,
      std::max<size_t>(1, settings.clients),
      connect_ready_timeout_ms,
      [](size_t queue_size) { request_queue_probe(queue_size); },
      [](spot_server_state_t *server_state, size_t start_size) {
          notify_size_start(server_state, start_size);
      });

    const bench_multi_cpu_sample_t sample_start =
      bench_multi_capture_cpu_sample();
    if (bench_transition_debug_enabled()) {
        std::cerr << "[multi-spot-server] phase gate open ts_us="
                  << perf_multi_metric::now_us()
                  << std::endl;
    }

    const bool ok =
      run_server_loop(&state, settings, lib_name, transport,
                      msg_sizes);
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
      sample_spot_queue_stats(session.pub, send_pending);
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
