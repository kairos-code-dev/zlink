/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <zlink/framework/contracts/configuration/logging.hpp>

#include <cstddef>
#include <exception>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace zlink::framework
{

enum class handler_execution_t
{
    inline_on_runtime,
    offload
};

enum class dispatch_mode_t
{
    compiled = 1,
    dynamic = 2
};

enum class unhandled_dispatch_action_t
{
    reply_error,
    log_and_drop,
    drop,
    throw_exception
};

enum class message_flow_log_mode_t
{
    off,
    errors_only,
    key_transitions,
    verbose,
    diagnostic
};

// A transition in a message's lifecycle on the success path. Errors are
// reported separately via message_dispatch_error_event_t; these phases describe
// healthy traffic so that "did the message arrive / get handled / get replied"
// can be followed by correlation id without resorting to ad-hoc printf debugging.
enum class message_flow_phase_t
{
    received,   // a well-formed envelope arrived at a dispatch surface
    dispatched, // a fire-and-forget message was handed to its handler
    replied,    // a request completed and a reply was produced
    dropped     // a message was intentionally discarded (no handler, decode failed, ...)
};

struct unhandled_dispatch_options_t
{
    unhandled_dispatch_action_t request = unhandled_dispatch_action_t::reply_error;
    unhandled_dispatch_action_t send = unhandled_dispatch_action_t::log_and_drop;
    unhandled_dispatch_action_t publish = unhandled_dispatch_action_t::log_and_drop;
    log_level_t send_log_level = log_level_t::warn;
    log_level_t publish_log_level = log_level_t::debug;
};

struct dispatch_diagnostics_options_t
{
    message_flow_log_mode_t message_flow = message_flow_log_mode_t::errors_only;
    double sample_rate = 1.0;
    bool include_message_sizes = true;
    bool include_native_diagnostics = false;
};

enum class dispatch_error_surface_t
{
    channel,
    dealer_mesh_channel,
    route_mesh_channel,
    spot_route,
    spot_subscription,
    spot_actor,
    stream_session
};

enum class dispatch_message_kind_t
{
    request,
    send,
    publish,
    response,
    error,
    actor_request,
    actor_send
};

enum class dispatch_error_reason_t
{
    handler_missing,
    payload_decode_failed,
    handler_exception,
    invalid_frame,
    reply_path_missing,
    unexpected_reply
};

enum class dispatch_error_action_t
{
    reply_error,
    drop
};

struct message_dispatch_error_event_t
{
    dispatch_error_surface_t surface;
    dispatch_message_kind_t message_kind;
    dispatch_error_reason_t reason;
    dispatch_error_action_t action;
    std::optional<std::string> packet_name;
    std::optional<std::string> channel_name;
    std::optional<std::string> topic;
    std::optional<std::string> spot_rid;
    std::optional<std::string> actor_id;
    std::optional<std::string> source_rid;
    std::optional<std::string> correlation_id;
    std::exception_ptr exception;
};

class message_dispatch_error_observer_t
{
  public:
    virtual ~message_dispatch_error_observer_t () = default;
    virtual void on_dispatch_error (const message_dispatch_error_event_t &error) = 0;
};

struct message_flow_event_t
{
    message_flow_phase_t phase;
    dispatch_error_surface_t surface;
    dispatch_message_kind_t message_kind;
    std::optional<std::string> packet_name;
    std::optional<std::string> channel_name;
    std::optional<std::string> topic;
    std::optional<std::string> correlation_id;
    std::optional<std::string> source_rid;
    std::optional<std::string> spot_rid;
    std::optional<std::string> actor_id;
    std::optional<std::size_t> message_size;
};

class message_flow_observer_t
{
  public:
    virtual ~message_flow_observer_t () = default;
    virtual void on_message_flow (const message_flow_event_t &event) = 0;
};

struct dispatch_options_t
{
    dispatch_mode_t spot_dispatch_mode = dispatch_mode_t::compiled;
    dispatch_mode_t stream_dispatch_mode = dispatch_mode_t::compiled;
    unhandled_dispatch_options_t unhandled;
    dispatch_diagnostics_options_t diagnostics;
    std::shared_ptr<message_dispatch_error_observer_t> message_dispatch_error_observer;
    std::function<void (const message_dispatch_error_event_t &)> message_dispatch_error_callback;
    std::shared_ptr<message_flow_observer_t> message_flow_observer;
    std::function<void (const message_flow_event_t &)> message_flow_callback;

    dispatch_options_t &set_message_dispatch_error_observer (
      std::shared_ptr<message_dispatch_error_observer_t> observer)
    {
        message_dispatch_error_observer = std::move (observer);
        message_dispatch_error_callback = {};
        return *this;
    }

    dispatch_options_t &set_message_dispatch_error_observer (
      std::function<void (const message_dispatch_error_event_t &)> observer)
    {
        message_dispatch_error_callback = std::move (observer);
        message_dispatch_error_observer.reset ();
        return *this;
    }

    dispatch_options_t &set_message_flow_observer (
      std::shared_ptr<message_flow_observer_t> observer)
    {
        message_flow_observer = std::move (observer);
        message_flow_callback = {};
        return *this;
    }

    dispatch_options_t &set_message_flow_observer (
      std::function<void (const message_flow_event_t &)> observer)
    {
        message_flow_callback = std::move (observer);
        message_flow_observer.reset ();
        return *this;
    }
};

} // namespace zlink::framework
