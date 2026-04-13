#include "../common/perf_common.hpp"
#include "../common/perf_common_multi.hpp"
#include "../common/perf_multi_handshake.hpp"
#include "../common/perf_multi_metric_header.hpp"
#include "../common/perf_multi_spot_control.hpp"
#include "../common/perf_multi_spot_handle.hpp"
#include "../common/perf_multi_spot_handshake.hpp"
#include "../../common/perf_tls_setup.hpp"
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
#include <map>
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
        run_id(1U),
        next_seq(1),
        expected_ready_count(1),
        send_enabled(false),
        send_pending(false),
        fatal_errno(0),
        start_gate(),
        ready_state(),
        control_peers()
    {
    }

    void *node;
    void *pub;
    void *control_node;
    void *control_pub;
    void *control_sub;
    size_t msg_size;
    perf_multi_metric::phase_t phase;
    uint32_t run_id;
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

bool apply_spot_server_options(void *pub,
                               const multi_bench_settings_t &settings)
{
    const int sndtimeo_ms =
      bench_timeout_ms_from_env("PERF_MULTI_SNDTIMEO_MS", 200);
    const int nodrop =
      resolve_multi_int_env("PERF_MULTI_SPOT_XPUB_NODROP", 1, 0);
    apply_benchmark_socket_options(pub, settings.hwm, "tcp");

    if (zlink_set_option(pub, ZLINK_OPT_SNDTIMEO, &sndtimeo_ms,
                                sizeof(sndtimeo_ms))
             != 0
        || zlink_set_pub_option(pub, ZLINK_PUB_OPT_NODROP, &nodrop,
                                sizeof(nodrop))
             != 0) {
        return false;
    }

    return true;
}

bool ensure_control_peers_connected(spot_server_state_t *state)
{
    return state
           && perf_multi_spot_control::ensure_connected_peers(
             state->control_node, state->control_peers);
}

bool wait_for_ready_slots(spot_server_state_t *state,
                          size_t msg_size,
                          int timeout_ms)
{
    return state
           && perf_multi_spot_control::wait_for_ready_units(
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
    return state
           && perf_multi_spot_control::publish_start(
             state->control_pub, k_topic, msg_size);
}

bool wait_for_size_start(spot_server_state_t *state,
                         size_t msg_size,
                         int timeout_ms)
{
    if (!state)
        return false;
    return perf_multi_handshake::wait_for_start(
      &state->start_gate, msg_size, timeout_ms);
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

send_status_t try_publish_locked(spot_server_state_t *state,
                                 unsigned long long *publish_ok_count,
                                 unsigned long long *publish_blocked_count)
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
          state->run_id,
          state->phase,
          state->msg_size,
          state->next_seq,
          perf_multi_metric::now_ns())) {
        zlink_msg_close(&part);
        return send_status_fatal;
    }

    const int rc = zlink_publish(
      state->pub, k_topic, &part, 1, ZLINK_SEND_FLAGS_DONTWAIT);
    const int saved_errno = rc == 0 ? 0 : errno;
    (void) zlink_msg_close(&part);

    if (rc == 0) {
        if (publish_ok_count)
            ++(*publish_ok_count);
        state->send_pending.store(false, std::memory_order_release);
        ++state->next_seq;
        return send_status_ok;
    }
    if (saved_errno == EAGAIN) {
        if (publish_blocked_count)
            ++(*publish_blocked_count);
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
               uint32_t run_id,
               perf_multi_metric::phase_t phase,
               double duration_seconds,
               bool send_enabled)
{
    if (duration_seconds <= 0.0)
        return true;

    state->msg_size = msg_size;
    state->phase = phase;
    state->run_id = run_id;
    state->next_seq = 1;
    state->send_enabled.store(send_enabled, std::memory_order_release);
    state->send_pending.store(false, std::memory_order_release);
    unsigned long long publish_ok_count = 0;
    unsigned long long publish_blocked_count = 0;
    unsigned long long publish_wait_count = 0;
    if (bench_transition_debug_enabled()) {
        std::cerr << "[multi-spot-server] phase start ts_ns="
                  << perf_multi_metric::now_ns()
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
        bool progressed = false;
        if (state->fatal_errno.load(std::memory_order_acquire) != 0)
            return false;
        if (state->send_enabled.load(std::memory_order_acquire)) {
            const send_status_t rc =
              try_publish_locked(state, &publish_ok_count,
                                 &publish_blocked_count);
            if (rc == send_status_fatal) {
                state->fatal_errno.store(errno != 0 ? errno : EIO,
                                         std::memory_order_release);
                return false;
            }
            progressed = rc == send_status_ok;
        }

        if (!progressed) {
            ++publish_wait_count;
            wait_for_spot_send_progress(send_enabled);
        }
    }

    state->send_enabled.store(false, std::memory_order_release);
    state->send_pending.store(false, std::memory_order_release);
    if (bench_transition_debug_enabled()) {
        std::cerr << "[multi-spot-server] phase done ts_ns="
                  << perf_multi_metric::now_ns()
                  << " size=" << msg_size
                  << " phase=" << static_cast<int>(phase)
                  << " ok=" << publish_ok_count
                  << " blocked=" << publish_blocked_count
                  << " wait=" << publish_wait_count
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
                          const std::vector<size_t> &sizes)
{
    (void) lib_name;
    (void) transport;
    (void) sizes;
}

bool run_server_loop(spot_server_state_t *state,
                     const multi_bench_settings_t &settings,
                     const std::string &lib_name,
                     const std::string &transport,
                     const std::vector<size_t> &msg_sizes)
{
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
            std::cerr << "[multi-spot-server] ready wait begin ts_ns="
                      << perf_multi_metric::now_ns()
                      << " size=" << msg_sizes[i]
                      << " timeout_ms=" << start_timeout_ms << std::endl;
        }
        if (!wait_for_size_start (state, msg_sizes[i], start_timeout_ms)) {
            if (bench_transition_debug_enabled()) {
                std::cerr << "[multi-spot-server] runner start timeout ts_ns="
                          << perf_multi_metric::now_ns()
                          << " size=" << msg_sizes[i] << std::endl;
            }
            return false;
        }
        if (!ensure_control_peers_connected(state)) {
            if (bench_debug_enabled()) {
                std::cerr << "[multi-spot-server] ensure control peers failed"
                          << " size=" << msg_sizes[i]
                          << " err=" << zlink_errno() << std::endl;
            }
            return false;
        }
        if (!wait_for_ready_slots(state, msg_sizes[i], start_timeout_ms)) {
            if (bench_transition_debug_enabled()) {
                std::cerr << "[multi-spot-server] ready wait timeout ts_ns="
                          << perf_multi_metric::now_ns()
                          << " size=" << msg_sizes[i] << std::endl;
            }
            return false;
        }
        if (bench_transition_debug_enabled()) {
            std::cerr << "[multi-spot-server] ready wait done ts_ns="
                      << perf_multi_metric::now_ns()
                      << " size=" << msg_sizes[i] << std::endl;
        }
        if (!publish_control_start(state, msg_sizes[i])) {
            if (bench_debug_enabled()) {
                std::cerr << "[multi-spot-server] start publish failed size="
                          << msg_sizes[i] << " err=" << zlink_errno()
                          << std::endl;
            }
            return false;
        }
        if (bench_transition_debug_enabled()) {
            std::cerr << "[multi-spot-server] start publish done ts_ns="
                      << perf_multi_metric::now_ns()
                      << " size=" << msg_sizes[i] << std::endl;
        }
        if (!run_phase(state,
                       lib_name,
                       transport,
                       msg_sizes[i],
                       static_cast<uint32_t>(i + 1),
                       perf_multi_metric::phase_active, active_seconds, true)) {
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
    sync_spot_internal_mesh_pub_hwm(
      bench_hwm_from_env("PERF_MULTI_SNDHWM", settings.hwm));
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
          apply_spot_server_options,
          &session)) {
        if (bench_debug_enabled())
            std::cerr << "[multi-spot-server] server session init failed err="
                      << zlink_errno() << std::endl;
        return 1;
    }
    spot_server_state_t state;
    state.expected_ready_count = std::max<size_t>(1, settings.clients);
    g_server_state = &state;
    perf_multi_spot_control::prepare_server_runtime(
      &state,
      session,
      state.expected_ready_count,
      settings.connect_ready_timeout_ms,
      [](spot_server_state_t *server_state, size_t start_size) {
          perf_multi_handshake::signal_start(
            &server_state->start_gate, start_size);
      });
    if (bench_transition_debug_enabled()) {
        std::cerr << "[multi-spot-server] phase gate open ts_ns="
                  << perf_multi_metric::now_ns()
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

    print_server_metrics(lib_name, transport, msg_sizes);
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
