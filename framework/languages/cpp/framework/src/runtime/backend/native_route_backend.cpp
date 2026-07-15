/* SPDX-License-Identifier: FSL-1.1-ALv2 */

#include "runtime/backend/native_route_backend.hpp"

#include "runtime/diagnostics/dispatch_error_reporter.hpp"

#include <zlink/Contracts/Errors/errors.hpp>
#include <zlink/Contracts/Service/operation_contracts.hpp>
#include <zlink/Contracts/Sockets/routed_socket_contracts.hpp>

#include <cerrno>
#include <condition_variable>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <mutex>
#include <utility>
#include <vector>

namespace zlink::framework::detail::backend
{

namespace
{

std::vector<zlink::message_t> copy_parts (const runtime::messaging::message_parts_t &parts)
{
    std::vector<zlink::message_t> copied;
    copied.reserve (parts.size ());
    for (const auto &part : parts.items ()) {
        copied.push_back (part);
    }
    return copied;
}

template <typename TOperation>
auto append_remaining_parts (TOperation operation, std::vector<zlink::message_t> &parts)
{
    for (std::size_t index = 1; index < parts.size (); ++index) {
        operation = std::move (operation).message (parts[index]);
    }
    return operation;
}

result_t<void> submit_failure (const char *message)
{
    return result_t<void>::failure (framework_error_kind_t::request_failed, message);
}

bool is_route_unreachable_errno (int value)
{
    return value == EHOSTUNREACH || value == ENETUNREACH || value == ECONNREFUSED
           || value == ENOTCONN;
}

bool route_channel_trace_enabled ()
{
    const char *value = std::getenv ("ZLINK_CPP_CHANNEL_TRACE");
    return value != nullptr && *value != '\0';
}

void trace_native_route_backend (const std::string &message)
{
    if (route_channel_trace_enabled ()) {
        std::cerr << "zlink route-backend " << message << '\n';
    }
}

framework_exception_t map_native_route_exception (const std::exception &error)
{
    if (const auto *request_error = dynamic_cast<const zlink::request_error_t *> (&error);
        request_error != nullptr) {
        if (request_error->result () == zlink::request_result_t::timed_out) {
            return detail::make_boundary_exception (detail::boundary_error_t::timed_out,
                                          "native route request timed out", true);
        }
        if (request_error->result () == zlink::request_result_t::not_found) {
            return framework_exception_t (framework_error_kind_t::request_target_not_found,
                                          request_error->what ());
        }
        if (request_error->result () == zlink::request_result_t::not_connected
            || is_route_unreachable_errno (request_error->internal_errno ())) {
            return framework_exception_t (framework_error_kind_t::route_not_connected,
                                          request_error->what ());
        }
        return framework_exception_t (framework_error_kind_t::request_failed,
                                      request_error->what ());
    }
    if (const auto *submit_error = dynamic_cast<const zlink::submit_error_t *> (&error);
        submit_error != nullptr) {
        if (submit_error->result () == zlink::submit_result_t::not_connected
            || is_route_unreachable_errno (submit_error->internal_errno ())) {
            return framework_exception_t (framework_error_kind_t::route_not_connected,
                                          submit_error->what ());
        }
    }
    return framework_exception_t (framework_error_kind_t::request_failed, error.what ());
}

struct route_request_callback_state_t
{
    std::mutex mutex;
    std::condition_variable changed;
    bool completed = false;
    zlink::request_result_t result = zlink::request_result_t::internal_error;
    std::vector<zlink::message_t> reply;
};

result_t<void> wait_for_route_request_callback (
  const std::shared_ptr<route_request_callback_state_t> &callback_state,
  std::chrono::milliseconds timeout,
  const std::function<bool ()> &stopping,
  const std::function<void ()> &progress = {})
{
    const auto has_deadline = timeout > std::chrono::milliseconds::zero ();
    const auto deadline = has_deadline ? std::chrono::steady_clock::now () + timeout
                                       : std::chrono::steady_clock::time_point::max ();
    while (true) {
        {
            std::lock_guard lock (callback_state->mutex);
            if (callback_state->completed) {
                return result_t<void>::success ();
            }
        }
        if (stopping && stopping ()) {
            return detail::boundary_failure<void> (detail::boundary_error_t::shutdown,
                                            "native route backend is shutting down");
        }
        if (progress) {
            progress ();
            std::lock_guard lock (callback_state->mutex);
            if (callback_state->completed) {
                return result_t<void>::success ();
            }
        }
        if (has_deadline && std::chrono::steady_clock::now () >= deadline) {
            return detail::boundary_failure<void> (detail::boundary_error_t::disconnected,
                                            "native route request disconnected before reply");
        }
        auto wait_time = std::chrono::milliseconds (50);
        if (has_deadline) {
            const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds> (
              deadline - std::chrono::steady_clock::now ());
            if (remaining < wait_time) {
                wait_time = remaining > std::chrono::milliseconds::zero ()
                              ? remaining
                              : std::chrono::milliseconds (1);
            }
        }
        std::unique_lock lock (callback_state->mutex);
        callback_state->changed.wait_for (lock, wait_time);
    }
}

} // namespace

native_route_backend_t::native_route_backend_t (zlink::router_socket_t &router) : _router (&router)
{
}

native_route_backend_t::native_route_backend_t (zlink::router_socket_t &router,
                                                std::atomic_bool &stop,
                                                dispatch_options_t dispatch) :
    _router (&router), _stop (&stop), _dispatch (std::move (dispatch))
{
}

void native_route_backend_t::attach_spot_route_bridge (
  std::unique_ptr<zlink::service::spot_route_bridge_t> bridge, std::string channel_name)
{
    std::lock_guard route_lock (_router_mutex);
    _spot_route_bridge = std::shared_ptr<zlink::service::spot_route_bridge_t> (std::move (bridge));
    _spot_route_channel_name = std::move (channel_name);
}

result_t<void>
native_route_backend_t::submit_send (const zlink::routing_id_t &target_node_rid,
                                     const std::optional<zlink::routing_id_t> &target_spot_rid,
                                     const runtime::messaging::message_parts_t &parts)
{
    if (_router == nullptr) {
        return result_t<void>::failure (framework_error_kind_t::request_protocol_error,
                                        "native route backend has no router socket");
    }
    auto copied = copy_parts (parts);
    if (copied.empty ()) {
        return result_t<void>::failure (framework_error_kind_t::request_protocol_error,
                                        "native route send requires at least one message part");
    }
    try {
        if (target_spot_rid) {
            std::shared_ptr<zlink::service::spot_route_bridge_t> bridge;
            std::string channel_name;
            std::lock_guard route_lock (_router_mutex);
            bridge = _spot_route_bridge;
            channel_name = _spot_route_channel_name;
            if (bridge) {
                trace_native_route_backend (
                  "bridge-send channel=" + channel_name
                  + " target=" + target_node_rid.to_string ()
                  + " spot=" + target_spot_rid->to_string ()
                  + " parts=" + std::to_string (copied.size ()));
                if (!bridge->send (channel_name, target_node_rid, *target_spot_rid, copied,
                                   zlink::send_flags_t::none)) {
                    trace_native_route_backend ("bridge-send-submit-failed");
                    return submit_failure ("native route bridge send was not accepted");
                }
                trace_native_route_backend ("bridge-send-submitted");
                return result_t<void>::success ();
            }
        }
        std::lock_guard route_lock (_router_mutex);
        if (_router == nullptr) {
            return result_t<void>::failure (framework_error_kind_t::request_protocol_error,
                                            "native route backend has no router socket");
        }
        auto submit = std::move (_router->send (target_node_rid)).message (copied[0]);
        submit = append_remaining_parts (std::move (submit), copied);
        trace_native_route_backend (
          "router-send target=" + target_node_rid.to_string ()
          + " parts=" + std::to_string (copied.size ()));
        if (!std::move (submit).submit ()) {
            trace_native_route_backend ("router-send-submit-failed");
            return submit_failure ("native route send was not accepted");
        }
        trace_native_route_backend ("router-send-submitted");
        return result_t<void>::success ();
    }
    catch (const std::exception &ex) {
        const auto error = map_native_route_exception (ex);
        return detail::result_access_t::failure<void> (error);
    }
}

result_t<runtime::messaging::message_parts_t>
native_route_backend_t::submit_request (const zlink::routing_id_t &target_node_rid,
                                        const std::optional<zlink::routing_id_t> &target_spot_rid,
                                        const runtime::messaging::message_parts_t &parts,
                                        std::chrono::milliseconds timeout)
{
    if (_router == nullptr) {
        return result_t<runtime::messaging::message_parts_t>::failure (
          framework_error_kind_t::request_protocol_error,
          "native route backend has no router socket");
    }
    auto copied = copy_parts (parts);
    if (copied.empty ()) {
        return result_t<runtime::messaging::message_parts_t>::failure (
          framework_error_kind_t::request_protocol_error,
          "native route request requires at least one message part");
    }
    try {
        if (target_spot_rid) {
            auto callback_state = std::make_shared<route_request_callback_state_t> ();
            bool submitted = false;
            bool bridge_was_present = false;
            std::shared_ptr<zlink::service::spot_route_bridge_t> bridge;
            std::string channel_name;
            {
                std::lock_guard route_lock (_router_mutex);
                bridge = _spot_route_bridge;
                channel_name = _spot_route_channel_name;
                if (bridge) {
                    bridge_was_present = true;
                    trace_native_route_backend (
                      "bridge-request channel=" + channel_name
                      + " target=" + target_node_rid.to_string ()
                      + " spot=" + target_spot_rid->to_string ()
                      + " parts=" + std::to_string (copied.size ()));
                    submitted = bridge->request (
                      channel_name, target_node_rid, *target_spot_rid, copied,
                      [callback_state] (zlink::request_result_t result,
                                        std::vector<zlink::message_t> reply) {
                          trace_native_route_backend (
                            "bridge-request-callback result="
                            + std::to_string (static_cast<int> (result))
                            + " parts=" + std::to_string (reply.size ()));
                          {
                              std::lock_guard lock (callback_state->mutex);
                              callback_state->result = result;
                              callback_state->reply = std::move (reply);
                              callback_state->completed = true;
                          }
                          callback_state->changed.notify_all ();
                      },
                      zlink::send_flags_t::none, timeout);
                }
            }
            if (bridge_was_present) {
                if (!submitted) {
                    trace_native_route_backend ("bridge-request-submit-failed");
                    return result_t<runtime::messaging::message_parts_t>::failure (
                      framework_error_kind_t::request_failed,
                      "native route bridge request was not submitted");
                }
                trace_native_route_backend ("bridge-request-submitted");
                if (auto waited = wait_for_route_request_callback (callback_state, timeout,
                                                                   [this] { return stopping (); },
                                                                   [this] {
                                                                       drain_spot_route_bridge ();
                                                                   });
                    !waited) {
                    trace_native_route_backend (
                      "bridge-request-wait-failed error="
                      + std::string (waited.error () ? waited.error ()->what ()
                                                     : "native route request failed"));
                    return detail::propagate_failure<runtime::messaging::message_parts_t> (waited, "native route request failed");
                }
                if (callback_state->result != zlink::request_result_t::ok) {
                    throw zlink::request_error_t (callback_state->result);
                }
                auto reply = std::move (callback_state->reply);
                return result_t<runtime::messaging::message_parts_t>::success (
                  runtime::messaging::message_parts_t (std::move (reply)));
            }
        }
        auto callback_state = std::make_shared<route_request_callback_state_t> ();
        bool submitted = false;
        {
            std::lock_guard route_lock (_router_mutex);
            if (_router == nullptr) {
                return result_t<runtime::messaging::message_parts_t>::failure (
                  framework_error_kind_t::request_protocol_error,
                  "native route backend has no router socket");
            }
            auto submit = std::move (_router->request (target_node_rid)).message (copied[0]);
            submit = append_remaining_parts (std::move (submit), copied);
            trace_native_route_backend (
              "router-request target=" + target_node_rid.to_string ()
              + " parts=" + std::to_string (copied.size ()));
            submitted = std::move (submit).timeout (timeout).submit (
              [callback_state] (zlink::request_result_t result,
                                std::vector<zlink::message_t> reply) {
                  trace_native_route_backend (
                    "router-request-callback result="
                    + std::to_string (static_cast<int> (result))
                    + " parts=" + std::to_string (reply.size ()));
                  {
                      std::lock_guard lock (callback_state->mutex);
                      callback_state->result = result;
                      callback_state->reply = std::move (reply);
                      callback_state->completed = true;
                  }
                  callback_state->changed.notify_all ();
              });
        }
        if (!submitted) {
            trace_native_route_backend ("router-request-submit-failed");
            return result_t<runtime::messaging::message_parts_t>::failure (
              framework_error_kind_t::request_failed, "native route request was not submitted");
        }
        trace_native_route_backend ("router-request-submitted");
        if (auto waited = wait_for_route_request_callback (callback_state, timeout,
                                                           [this] { return stopping (); },
                                                           [this] {
                                                               drain_spot_route_bridge ();
                                                           });
            !waited) {
            trace_native_route_backend (
              "router-request-wait-failed error="
              + std::string (waited.error () ? waited.error ()->what ()
                                             : "native route request failed"));
            return detail::propagate_failure<runtime::messaging::message_parts_t> (waited, "native route request failed");
        }
        if (callback_state->result != zlink::request_result_t::ok) {
            throw zlink::request_error_t (callback_state->result);
        }
        auto reply = std::move (callback_state->reply);
        return result_t<runtime::messaging::message_parts_t>::success (
          runtime::messaging::message_parts_t (std::move (reply)));
    }
    catch (const std::exception &ex) {
        const auto error = map_native_route_exception (ex);
        return detail::result_access_t::failure<runtime::messaging::message_parts_t> (error);
    }
}

bool native_route_backend_t::handle_router_received (const zlink::routing_id_t &source_node_rid,
                                                     std::vector<zlink::message_t> &parts,
                                                     std::optional<std::uint64_t> request_seq)
{
    std::shared_ptr<zlink::service::spot_route_bridge_t> bridge;
    std::string channel_name;
    {
        std::lock_guard route_lock (_router_mutex);
        bridge = _spot_route_bridge;
        channel_name = _spot_route_channel_name;
    }
    if (!bridge) {
        return false;
    }
    const auto target_spot_rid = parts.size () >= 2 ? parts[1].to_string () : std::string ();
    std::optional<runtime::messaging::envelope_header_t> header;
    if (!request_seq && parts.size () >= 3) {
        auto decoded = runtime::messaging::envelope_codec_t ().decode_header (parts[2]);
        if (decoded
            && decoded.value ().kind == runtime::messaging::message_kind_t::command) {
            header = std::move (decoded.value ());
        }
    }
    const bool is_spot_send = header.has_value ();
    try {
        return bridge->handle_router_received (channel_name, source_node_rid, parts, request_seq);
    } catch (const zlink::binding_error_t &error) {
        trace_native_route_backend (
          "bridge-receive-failed errno=" + std::to_string (error.internal_errno ())
          + " send=" + std::string (is_spot_send ? "true" : "false")
          + " packet=" + (header ? header->message_name : std::string ())
          + " spot=" + target_spot_rid);
        /* The binding restores moved frames before throwing, so the native
         * ENOENT value is not retained. A decoded command envelope plus a
         * non-empty target identifies a routed one-way delivery rejection. */
        if (is_spot_send && header && !target_spot_rid.empty ()) {
            detail::dispatch_error_reporter_t (_dispatch).report (
              message_dispatch_error_event_t{
                dispatch_error_surface_t::spot_route,
                dispatch_message_kind_t::send,
                dispatch_error_reason_t::handler_missing,
                dispatch_error_action_t::drop,
                header->message_name,
                channel_name,
                std::nullopt,
                target_spot_rid,
                std::nullopt,
                source_node_rid.to_string (),
                header->correlation_id,
                std::make_exception_ptr (framework_exception_t (
                  framework_error_kind_t::spot_route_not_found,
                  "target spot route is not registered"))});
        }
        return true;
    }
    catch (const std::exception &) {
        return true;
    }
}

int native_route_backend_t::drain_spot_route_bridge ()
{
    std::shared_ptr<zlink::service::spot_route_bridge_t> bridge;
    {
        std::lock_guard route_lock (_router_mutex);
        bridge = _spot_route_bridge;
    }
    if (!bridge) {
        return 0;
    }
    try {
        return bridge->drain ();
    }
    catch (const std::exception &) {
        return -1;
    }
}

void native_route_backend_t::close () noexcept
{
    std::shared_ptr<zlink::service::spot_route_bridge_t> bridge;
    {
        std::lock_guard route_lock (_router_mutex);
        bridge = std::move (_spot_route_bridge);
        _spot_route_bridge.reset ();
        _router = nullptr;
    }
    if (bridge) {
        try {
            bridge->close ();
        }
        catch (...) {
        }
    }
}

std::mutex &native_route_backend_t::router_mutex () noexcept
{
    return _router_mutex;
}

bool native_route_backend_t::stopping () const noexcept
{
    return _stop != nullptr && _stop->load (std::memory_order_acquire);
}

} // namespace zlink::framework::detail::backend
