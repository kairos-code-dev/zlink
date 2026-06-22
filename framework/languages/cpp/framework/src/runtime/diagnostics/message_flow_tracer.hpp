/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <zlink/framework/contracts/dispatch/execution.hpp>

#include "runtime/diagnostics/dispatch_diagnostics_names.hpp"
#include "runtime/dispatch/offload_executor.hpp"

#include <atomic>
#include <cstdint>
#include <iostream>
#include <memory>
#include <utility>

namespace zlink::framework::detail
{

// Emits success-path message-flow transitions, gated by
// dispatch_diagnostics_options_t::message_flow. It is the success-path twin of
// dispatch_error_reporter_t: same construction shape (constructed per dispatch
// from a copy of dispatch_options_t), same clog + offloaded-observer fan-out, so
// errors and healthy transitions read as one correlation-id-keyed stream.
//
// Mode gating (modes are ordered off < errors_only < key_transitions < verbose
// < diagnostic):
//   * received / dispatched / replied require key_transitions or higher.
//   * dropped requires errors_only or higher (it mirrors an error decision).
//   * message sizes are appended only at verbose+ when include_message_sizes.
class message_flow_tracer_t
{
  public:
    explicit message_flow_tracer_t (dispatch_options_t options) : _options (std::move (options)) {}

    bool enabled (message_flow_log_mode_t min_mode) const noexcept
    {
        return rank (_options.diagnostics.message_flow) >= rank (min_mode);
    }

    void trace (message_flow_event_t event) const noexcept
    {
        const auto required = event.phase == message_flow_phase_t::dropped
                                ? message_flow_log_mode_t::errors_only
                                : message_flow_log_mode_t::key_transitions;
        if (!enabled (required)) {
            return;
        }
        // Sampling thins healthy traffic on the hot path; dropped transitions are
        // diagnostically important and always pass through.
        if (event.phase != message_flow_phase_t::dropped
            && !sample (_options.diagnostics.sample_rate)) {
            return;
        }
        traced_count ().fetch_add (1, std::memory_order_relaxed);
        log_default (event);

        auto observer = _options.message_flow_observer;
        auto callback = _options.message_flow_callback;
        if (!observer && !callback) {
            return;
        }
        if (!observer_executor ().try_submit (
              [observer = std::move (observer), callback = std::move (callback),
               event = std::move (event)] () mutable {
                  try {
                      if (observer) {
                          observer->on_message_flow (event);
                          return;
                      }
                      if (callback) {
                          callback (event);
                      }
                  }
                  catch (...) {
                      observer_failure_count ().fetch_add (1, std::memory_order_relaxed);
                  }
              })) {
            observer_dropped_count ().fetch_add (1, std::memory_order_relaxed);
        }
    }

    static std::uint64_t traced () noexcept
    {
        return traced_count ().load (std::memory_order_relaxed);
    }

    static std::uint64_t observer_failures () noexcept
    {
        return observer_failure_count ().load (std::memory_order_relaxed);
    }

    static std::uint64_t observer_dropped () noexcept
    {
        return observer_dropped_count ().load (std::memory_order_relaxed);
    }

  private:
    static int rank (message_flow_log_mode_t mode) noexcept { return static_cast<int> (mode); }

    bool sample (double rate) const noexcept
    {
        if (rate >= 1.0) {
            return true;
        }
        if (rate <= 0.0) {
            return false;
        }
        auto stride = static_cast<std::uint64_t> (1.0 / rate + 0.5);
        if (stride == 0) {
            stride = 1;
        }
        return (sample_counter ().fetch_add (1, std::memory_order_relaxed) % stride) == 0;
    }

    void log_default (const message_flow_event_t &event) const noexcept
    {
        try {
            std::clog << "zlink flow: phase=" << enum_name (event.phase)
                      << " surface=" << enum_name (event.surface)
                      << " kind=" << enum_name (event.message_kind);
            if (event.packet_name) {
                std::clog << " packet=" << *event.packet_name;
            }
            if (event.channel_name) {
                std::clog << " channel=" << *event.channel_name;
            }
            if (event.topic) {
                std::clog << " topic=" << *event.topic;
            }
            if (event.correlation_id) {
                std::clog << " corr=" << *event.correlation_id;
            }
            if (event.source_rid) {
                std::clog << " src=" << *event.source_rid;
            }
            if (event.spot_rid) {
                std::clog << " spot=" << *event.spot_rid;
            }
            if (event.actor_id) {
                std::clog << " actor=" << *event.actor_id;
            }
            if (event.message_size && enabled (message_flow_log_mode_t::verbose)
                && _options.diagnostics.include_message_sizes) {
                std::clog << " size=" << *event.message_size;
            }
            std::clog << '\n';
        }
        catch (...) {
            observer_failure_count ().fetch_add (1, std::memory_order_relaxed);
        }
    }

    static runtime::offload_executor_t &observer_executor ()
    {
        static auto executor = std::make_unique<runtime::offload_executor_t> (1, 1024);
        return *executor;
    }

    static std::atomic<std::uint64_t> &sample_counter () noexcept
    {
        static std::atomic<std::uint64_t> value{0};
        return value;
    }

    static std::atomic<std::uint64_t> &traced_count () noexcept
    {
        static std::atomic<std::uint64_t> value{0};
        return value;
    }

    static std::atomic<std::uint64_t> &observer_failure_count () noexcept
    {
        static std::atomic<std::uint64_t> value{0};
        return value;
    }

    static std::atomic<std::uint64_t> &observer_dropped_count () noexcept
    {
        static std::atomic<std::uint64_t> value{0};
        return value;
    }

    dispatch_options_t _options;
};

} // namespace zlink::framework::detail
