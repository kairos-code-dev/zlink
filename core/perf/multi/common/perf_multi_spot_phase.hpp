#ifndef PERF_MULTI_SPOT_PHASE_HPP
#define PERF_MULTI_SPOT_PHASE_HPP

#include "perf_common.hpp"
#include "perf_multi_metric_header.hpp"

namespace perf_multi_spot_phase {

struct server_hooks_t
{
    bool (*wait_for_size_start)(void *, size_t, int);
    bool (*ensure_control_peers_connected)(void *);
    bool (*wait_for_ready_slots)(void *, size_t, int);
    bool (*publish_control_start)(void *, size_t);
    bool (*run_phase)(void *,
                      const std::string &,
                      const std::string &,
                      size_t,
                      perf_multi_metric::phase_t,
                      double,
                      bool);
    int (*fatal_errno)(void *);
};

inline bool run_server_cases(void *state,
                             const char *debug_tag,
                             const multi_bench_settings_t &settings,
                             const std::string &lib_name,
                             const std::string &transport,
                             const std::vector<size_t> &msg_sizes,
                             const server_hooks_t &hooks)
{
    const double active_seconds =
      static_cast<double>(std::max(1, settings.duration_seconds));
    const int start_timeout_ms =
      std::max(settings.connect_ready_timeout_ms,
               std::max(1000, settings.connect_ready_timeout_ms * 6));

    for (size_t i = 0; i < msg_sizes.size(); ++i) {
        const size_t msg_size = msg_sizes[i];
        if (perf_stop_requested().load(std::memory_order_acquire)) {
            return !hooks.fatal_errno || hooks.fatal_errno(state) == 0;
        }

        if (bench_transition_debug_enabled()) {
            std::cerr << "[" << debug_tag << "] ready wait begin ts_ns="
                      << perf_multi_metric::now_ns()
                      << " size=" << msg_size
                      << " timeout_ms=" << start_timeout_ms << std::endl;
        }
        if (!hooks.wait_for_size_start(state, msg_size, start_timeout_ms)
            || !hooks.ensure_control_peers_connected(state)
            || !hooks.wait_for_ready_slots(state, msg_size, start_timeout_ms)
            || !hooks.publish_control_start(state, msg_size)
            || !hooks.run_phase(state,
                                lib_name,
                                transport,
                                msg_size,
                                perf_multi_metric::phase_active,
                                active_seconds,
                                true)) {
            return false;
        }
    }

    return true;
}

struct client_hooks_t
{
    bool (*wait_for_control_link_ready)(void *, int);
    bool (*wait_for_ready_settle)(void *);
    void (*reset_metrics)(void *, size_t);
    bool (*ensure_control_connected)(void *);
    size_t (*ready_units)(void *);
    bool (*publish_ready_count)(void *, size_t, size_t);
    bool (*wait_for_msg_size_start)(void *, size_t, int);
    void (*set_collect_active)(void *, bool);
    bool (*wait_for_active_phase)(void *, size_t, int);
    bool (*wait_for_phase_duration)(void *, double);
};

inline bool run_client_case(void *state,
                            const multi_bench_settings_t &settings,
                            const std::string &lib_name,
                            const std::string &transport,
                            size_t msg_size,
                            int phase_timeout_ms,
                            const client_hooks_t &hooks)
{
    if (!hooks.wait_for_control_link_ready(
          state, settings.connect_ready_timeout_ms)
        || !hooks.wait_for_ready_settle(state)) {
        return false;
    }

    std::cout << "CLIENT_READY," << msg_size << std::endl;
    hooks.reset_metrics(state, msg_size);

    const size_t ready_units = hooks.ready_units ? hooks.ready_units(state) : 0;
    if (!hooks.ensure_control_connected(state)
        || !hooks.publish_ready_count(state, msg_size, ready_units)
        || !hooks.wait_for_msg_size_start(state, msg_size, phase_timeout_ms)) {
        return false;
    }

    hooks.set_collect_active(state, true);
    if (!hooks.wait_for_active_phase(state, msg_size, phase_timeout_ms)) {
        hooks.set_collect_active(state, false);
        return false;
    }

    if (!hooks.wait_for_phase_duration(
          state, static_cast<double>(std::max(1, settings.duration_seconds)))) {
        hooks.set_collect_active(state, false);
        return false;
    }
    hooks.set_collect_active(state, false);
    return true;
}

} // namespace perf_multi_spot_phase

#endif
