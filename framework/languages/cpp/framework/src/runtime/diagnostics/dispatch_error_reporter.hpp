/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <zlink/framework/contracts/dispatch/execution.hpp>
#include <zlink/framework/contracts/errors/error.hpp>

#include "runtime/diagnostics/diagnostic_event_sink.hpp"
#include "runtime/diagnostics/dispatch_diagnostics_names.hpp"
#include "runtime/diagnostics/message_flow_tracer.hpp"

#include <atomic>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace zlink::framework::detail
{

class dispatch_error_reporter_t
{
  public:
    explicit dispatch_error_reporter_t (dispatch_options_t options) : _options (std::move (options))
    {
    }

    void report (message_dispatch_error_event_t event) const noexcept
    {
        reported_count ().fetch_add (1, std::memory_order_relaxed);
        // off silences the default error log; every other mode keeps reporting
        // errors (errors_only is the default). A registered observer still fires
        // regardless of mode — it is an explicit, separate subscription.
        if (_options.diagnostics.effective_message_flow () != message_flow_log_mode_t::off) {
            log_default (event);
        }
        message_flow_tracer_t (_options).trace (message_flow_event_t{
          message_flow_outcome_t::error, event.surface, event.message_kind,
          std::move (event.packet_name), std::move (event.channel_name), std::move (event.topic),
          std::move (event.correlation_id), std::move (event.source_rid),
          std::move (event.spot_rid), std::move (event.actor_id), std::nullopt, event.reason,
          event.action, event.exception});
    }

    static std::uint64_t reported () noexcept
    {
        return reported_count ().load (std::memory_order_relaxed);
    }

    static std::uint64_t observer_failures () noexcept
    {
        return message_flow_tracer_t::observer_failures ();
    }

    static std::uint64_t observer_dropped () noexcept
    {
        return message_flow_tracer_t::observer_dropped ();
    }

  private:
    void log_default (const message_dispatch_error_event_t &event) const noexcept
    {
        try {
            std::vector<log_field_t> fields;
            fields.reserve (11);
            auto add = [&fields] (const char *key, std::string value) {
                diagnostic_event_sink_t::append_field (fields, key, std::move (value));
            };
            add ("surface", std::string (enum_name (event.surface)));
            add ("kind", std::string (enum_name (event.message_kind)));
            add ("reason", std::string (enum_name (event.reason)));
            add ("action", std::string (enum_name (event.action)));
            if (_options.diagnostics.label ()) {
                add ("label", *_options.diagnostics.label ());
            }
            if (event.packet_name) {
                add ("packet", *event.packet_name);
            }
            if (event.channel_name) {
                add ("channel", *event.channel_name);
            }
            if (event.topic) {
                add ("topic", *event.topic);
            }
            if (event.correlation_id) {
                add ("corr", *event.correlation_id);
            }
            if (event.source_rid) {
                add ("src", *event.source_rid);
            }
            if (event.spot_rid) {
                add ("spot", *event.spot_rid);
            }
            if (event.actor_id) {
                add ("actor", *event.actor_id);
            }
            // Structured fields through the framework logger (collector-friendly);
            // flat clog line when no logger is wired (tests, no-app usage).
            diagnostic_event_sink_t::log_or_clog (
              _options.diagnostics_logger, log_level_t::error, "dispatch error",
              "zlink framework dispatch error:", std::move (fields));
        }
        catch (...) {
            (void) observer_failures ();
        }
    }

    static std::atomic<std::uint64_t> &reported_count () noexcept
    {
        static std::atomic<std::uint64_t> value{0};
        return value;
    }

    dispatch_options_t _options;
};

inline dispatch_error_reason_t dispatch_reason_from_error (framework_error_kind_t kind) noexcept
{
    switch (kind) {
        case framework_error_kind_t::handler_not_found:
        case framework_error_kind_t::route_handler_not_found:
        case framework_error_kind_t::actor_dispatch_handler_not_found:
            return dispatch_error_reason_t::handler_missing;
        case framework_error_kind_t::payload_decode_failed:
            return dispatch_error_reason_t::payload_decode_failed;
        case framework_error_kind_t::request_protocol_error:
            return dispatch_error_reason_t::invalid_frame;
        default:
            return dispatch_error_reason_t::handler_exception;
    }
}

} // namespace zlink::framework::detail
