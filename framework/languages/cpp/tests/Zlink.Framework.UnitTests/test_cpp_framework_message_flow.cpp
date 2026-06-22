/* SPDX-License-Identifier: MPL-2.0 */

// Verifies the message-flow tracing feature: dispatch_diagnostics_options_t::
// message_flow now actually gates a structured, correlation-id-keyed log stream
// (success transitions via message_flow_tracer_t) and silences the error log in
// the off mode (dispatch_error_reporter_t). The tracer's default sink is
// std::clog, so the tests capture that buffer and assert on the emitted lines.

#include "runtime/diagnostics/dispatch_error_reporter.hpp"
#include "runtime/diagnostics/message_flow_tracer.hpp"

#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <utility>

namespace
{

using namespace zlink::framework;
using zlink::framework::detail::dispatch_error_reporter_t;
using zlink::framework::detail::message_flow_tracer_t;

// Redirects std::clog for the duration of a block and returns what was written.
template <typename Fn> std::string capture_clog (Fn &&fn)
{
    std::ostringstream captured;
    auto *previous = std::clog.rdbuf (captured.rdbuf ());
    std::forward<Fn> (fn) ();
    std::clog.rdbuf (previous);
    return captured.str ();
}

dispatch_options_t options_with_mode (message_flow_log_mode_t mode)
{
    dispatch_options_t options;
    options.diagnostics.message_flow = mode;
    return options;
}

message_flow_event_t flow_event (message_flow_phase_t phase)
{
    return message_flow_event_t{phase,
                                dispatch_error_surface_t::channel,
                                dispatch_message_kind_t::request,
                                std::string ("PlaceOrder"),
                                std::string ("orders"),
                                std::nullopt,
                                std::string ("corr-123"),
                                std::nullopt,
                                std::nullopt,
                                std::nullopt,
                                std::optional<std::size_t> (42)};
}

message_dispatch_error_event_t error_event ()
{
    return message_dispatch_error_event_t{dispatch_error_surface_t::channel,
                                          dispatch_message_kind_t::request,
                                          dispatch_error_reason_t::handler_missing,
                                          dispatch_error_action_t::reply_error,
                                          std::string ("PlaceOrder"),
                                          std::string ("orders"),
                                          std::nullopt,
                                          std::nullopt,
                                          std::nullopt,
                                          std::nullopt,
                                          std::string ("corr-123"),
                                          nullptr};
}

bool contains (const std::string &haystack, const std::string &needle)
{
    return haystack.find (needle) != std::string::npos;
}

} // namespace

int main ()
{
    // off silences every success transition, including drops.
    {
        const auto out = capture_clog ([] {
            message_flow_tracer_t tracer (options_with_mode (message_flow_log_mode_t::off));
            tracer.trace (flow_event (message_flow_phase_t::received));
            tracer.trace (flow_event (message_flow_phase_t::dropped));
        });
        if (!out.empty ()) {
            return 1;
        }
    }

    // errors_only emits the drop decision but not healthy transitions.
    {
        const auto out = capture_clog ([] {
            message_flow_tracer_t tracer (options_with_mode (message_flow_log_mode_t::errors_only));
            tracer.trace (flow_event (message_flow_phase_t::received));
            tracer.trace (flow_event (message_flow_phase_t::dropped));
        });
        if (contains (out, "phase=received")) {
            return 2;
        }
        if (!contains (out, "phase=dropped")) {
            return 3;
        }
    }

    // key_transitions emits the lifecycle, keyed by correlation id; no sizes yet.
    {
        const auto out = capture_clog ([] {
            message_flow_tracer_t tracer (
              options_with_mode (message_flow_log_mode_t::key_transitions));
            tracer.trace (flow_event (message_flow_phase_t::received));
            tracer.trace (flow_event (message_flow_phase_t::replied));
        });
        if (!contains (out, "zlink flow: phase=received")) {
            return 4;
        }
        if (!contains (out, "phase=replied")) {
            return 5;
        }
        if (!contains (out, "corr=corr-123")) {
            return 6;
        }
        if (!contains (out, "packet=PlaceOrder")) {
            return 7;
        }
        if (contains (out, "size=42")) {
            return 8;
        }
    }

    // verbose appends the message size...
    {
        const auto out = capture_clog ([] {
            message_flow_tracer_t (options_with_mode (message_flow_log_mode_t::verbose))
              .trace (flow_event (message_flow_phase_t::received));
        });
        if (!contains (out, "size=42")) {
            return 9;
        }
    }

    // ...unless include_message_sizes opts out.
    {
        auto options = options_with_mode (message_flow_log_mode_t::verbose);
        options.diagnostics.include_message_sizes = false;
        const auto out = capture_clog ([&] {
            message_flow_tracer_t (options).trace (flow_event (message_flow_phase_t::received));
        });
        if (contains (out, "size=")) {
            return 10;
        }
    }

    // off also silences the dispatch error log...
    {
        const auto out = capture_clog ([] {
            dispatch_error_reporter_t (options_with_mode (message_flow_log_mode_t::off))
              .report (error_event ());
        });
        if (!out.empty ()) {
            return 11;
        }
    }

    // ...while errors_only (the default) keeps reporting errors.
    {
        const auto out = capture_clog ([] {
            dispatch_error_reporter_t (options_with_mode (message_flow_log_mode_t::errors_only))
              .report (error_event ());
        });
        if (!contains (out, "dispatch error")) {
            return 12;
        }
        if (!contains (out, "reason=handler_missing")) {
            return 13;
        }
    }

    return 0;
}
