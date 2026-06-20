/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <zlink/framework/contracts/dispatch/execution.hpp>
#include <zlink/framework/contracts/errors/error.hpp>

#include "runtime/dispatch/offload_executor.hpp"

#include <atomic>
#include <iostream>
#include <cstdint>
#include <memory>
#include <string_view>
#include <utility>

namespace zlink::framework::detail
{

class dispatch_error_reporter_t
{
  public:
    explicit dispatch_error_reporter_t (dispatch_options_t options) : _options (std::move (options)) {}

    void report (message_dispatch_error_event_t event) const noexcept
    {
        reported_count ().fetch_add (1, std::memory_order_relaxed);
        log_default (event);
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
    static void log_default (const message_dispatch_error_event_t &event) noexcept
    {
        try {
            std::clog << "zlink framework dispatch error: surface="
                      << enum_name (event.surface) << " kind=" << enum_name (event.message_kind)
                      << " reason=" << enum_name (event.reason)
                      << " action=" << enum_name (event.action);
            if (event.packet_name) {
                std::clog << " packet=" << *event.packet_name;
            }
            if (event.channel_name) {
                std::clog << " channel=" << *event.channel_name;
            }
            if (event.spot_rid) {
                std::clog << " spot=" << *event.spot_rid;
            }
            std::clog << '\n';
        }
        catch (...) {
            observer_failure_count ().fetch_add (1, std::memory_order_relaxed);
        }
    }

    static std::string_view enum_name (dispatch_error_surface_t value) noexcept
    {
        switch (value) {
            case dispatch_error_surface_t::channel:
                return "channel";
            case dispatch_error_surface_t::dealer_mesh_channel:
                return "dealer_mesh_channel";
            case dispatch_error_surface_t::route_mesh_channel:
                return "route_mesh_channel";
            case dispatch_error_surface_t::spot_route:
                return "spot_route";
            case dispatch_error_surface_t::spot_subscription:
                return "spot_subscription";
            case dispatch_error_surface_t::spot_actor:
                return "spot_actor";
            case dispatch_error_surface_t::stream_session:
                return "stream_session";
        }
        return "unknown";
    }

    static std::string_view enum_name (dispatch_message_kind_t value) noexcept
    {
        switch (value) {
            case dispatch_message_kind_t::send:
                return "send";
            case dispatch_message_kind_t::request:
                return "request";
            case dispatch_message_kind_t::publish:
                return "publish";
            case dispatch_message_kind_t::response:
                return "response";
            case dispatch_message_kind_t::error:
                return "error";
            case dispatch_message_kind_t::actor_send:
                return "actor_send";
            case dispatch_message_kind_t::actor_request:
                return "actor_request";
        }
        return "unknown";
    }

    static std::string_view enum_name (dispatch_error_reason_t value) noexcept
    {
        switch (value) {
            case dispatch_error_reason_t::handler_missing:
                return "handler_missing";
            case dispatch_error_reason_t::payload_decode_failed:
                return "payload_decode_failed";
            case dispatch_error_reason_t::handler_exception:
                return "handler_exception";
            case dispatch_error_reason_t::invalid_frame:
                return "invalid_frame";
            case dispatch_error_reason_t::reply_path_missing:
                return "reply_path_missing";
            case dispatch_error_reason_t::unexpected_reply:
                return "unexpected_reply";
        }
        return "unknown";
    }

    static std::string_view enum_name (dispatch_error_action_t value) noexcept
    {
        switch (value) {
            case dispatch_error_action_t::drop:
                return "drop";
            case dispatch_error_action_t::reply_error:
                return "reply_error";
        }
        return "unknown";
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
