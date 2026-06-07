#ifndef CPP_PERF_SPOT_PHASE_HPP
#define CPP_PERF_SPOT_PHASE_HPP

#include "perf_metric_header.hpp"
#include "perf_common_multi.hpp"

namespace perf
{
namespace multi
{

template <typename WaitStartFn, typename ReadyWaitFn, typename PublishStartFn, typename RunPhaseFn>
inline bool run_spot_server_cases (const multi_bench_settings_t &settings,
                                   const std::vector<size_t> &msg_sizes,
                                   WaitStartFn wait_start_fn,
                                   ReadyWaitFn wait_ready_fn,
                                   PublishStartFn publish_start_fn,
                                   RunPhaseFn run_phase_fn)
{
    const int start_timeout_ms = std::max (settings.connect_ready_timeout_ms,
                                           std::max (1000, settings.connect_ready_timeout_ms * 6));

    for (size_t i = 0; i < msg_sizes.size (); ++i) {
        const size_t msg_size = msg_sizes[i];
        if (!wait_start_fn (msg_size, start_timeout_ms) || !wait_ready_fn (msg_size)
            || !publish_start_fn (msg_size)
            || !run_phase_fn (msg_size, perf_metric::phase_active,
                              std::chrono::seconds (std::max (1, settings.duration_seconds)))) {
            return false;
        }
    }

    return true;
}

template <typename PrepareFn, typename WaitControlStartFn, typename RunActiveFn, typename FinishFn>
inline bool run_spot_client_case (size_t msg_size,
                                  int phase_timeout_ms,
                                  PrepareFn prepare_fn,
                                  WaitControlStartFn wait_control_start_fn,
                                  RunActiveFn run_active_fn,
                                  FinishFn finish_fn)
{
    if (!prepare_fn (msg_size, phase_timeout_ms) || !wait_control_start_fn (phase_timeout_ms)
        || !run_active_fn () || !finish_fn (msg_size)) {
        return false;
    }
    return true;
}

} // namespace multi
} // namespace perf

#endif
