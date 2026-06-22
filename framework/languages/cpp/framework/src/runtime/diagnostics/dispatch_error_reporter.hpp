/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <zlink/framework/contracts/dispatch/execution.hpp>
#include <zlink/framework/contracts/errors/error.hpp>

#include "runtime/diagnostics/dispatch_diagnostics_names.hpp"
#include "runtime/dispatch/offload_executor.hpp"

#include <atomic>
#include <iostream>
#include <cstdint>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace zlink::framework::detail
{

class dispatch_error_reporter_t
{
  public:
    explicit dispatch_error_reporter_t (dispatch_options_t options) : _options (std::move (options)) {}

    void report (message_dispatch_error_event_t event) const noexcept
    {
        reported_count ().fetch_add (1, std::memory_order_relaxed);
        // off silences the default error log; every other mode keeps reporting
        // errors (errors_only is the default). A registered observer still fires
        // regardless of mode — it is an explicit, separate subscription.
        if (_options.diagnostics.effective_message_flow () != message_flow_log_mode_t::off) {
            log_default (event);
        }
        auto observer = _options.message_dispatch_error_observer;
        auto callback = _options.message_dispatch_error_callback;
        if (!observer && !callback) {
            return;
        }
        if (!observer_executor ().try_submit (
              [observer = std::move (observer), callback = std::move (callback),
               event = std::move (event)] () mutable {
                  try {
                      if (observer) {
                          observer->on_dispatch_error (event);
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

    static std::uint64_t reported () noexcept
    {
        return reported_count ().load (std::memory_order_relaxed);
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
    void log_default (const message_dispatch_error_event_t &event) const noexcept
    {
        try {
            std::vector<log_field_t> fields;
            fields.reserve (11);
            auto add = [&fields] (const char *key, std::string value) {
                fields.push_back (log_field_t{key, std::move (value)});
            };
            add ("surface", std::string (enum_name (event.surface)));
            add ("kind", std::string (enum_name (event.message_kind)));
            add ("reason", std::string (enum_name (event.reason)));
            add ("action", std::string (enum_name (event.action)));
            if (_options.diagnostics.node_id ()) {
                add ("node", *_options.diagnostics.node_id ());
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
            if (_options.diagnostics_logger) {
                _options.diagnostics_logger->log_with_fields (log_level_t::error, "dispatch error",
                                                              std::move (fields));
            }
            else {
                std::ostringstream body;
                body << "zlink framework dispatch error:";
                for (const auto &field : fields) {
                    body << ' ' << field.key << '=' << field.value;
                }
                std::clog << body.str () << '\n';
            }
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

    static std::atomic<std::uint64_t> &reported_count () noexcept
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
