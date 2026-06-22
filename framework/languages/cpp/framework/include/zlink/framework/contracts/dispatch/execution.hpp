/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <zlink/framework/contracts/configuration/logging.hpp>

#include <atomic>
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
    received,       // a well-formed envelope arrived at a dispatch surface (inbound)
    dispatched,     // a fire-and-forget message was handed to its handler (inbound)
    replied,        // a request completed and a reply was produced (inbound)
    dropped,        // a message was intentionally discarded (no handler, decode failed, ...)
    sent,           // a message left this node toward another channel/spot/node (outbound)
    reply_received  // a reply came back for an outbound request (outbound)
};

struct unhandled_dispatch_options_t
{
    unhandled_dispatch_action_t request = unhandled_dispatch_action_t::reply_error;
    unhandled_dispatch_action_t send = unhandled_dispatch_action_t::log_and_drop;
    unhandled_dispatch_action_t publish = unhandled_dispatch_action_t::log_and_drop;
    log_level_t send_log_level = log_level_t::warn;
    log_level_t publish_log_level = log_level_t::debug;
};

// Diagnostics/tracing config. Fields are encapsulated: configure them only through
// the fluent builder on dispatch_options_t (configure_dispatch().message_flow(...)
// .trace_log_file(...).trace_node_id(...)...). Read access is via getters.
class dispatch_diagnostics_options_t
{
  public:
    // Configured (static) mode. Prefer effective_message_flow() for runtime checks.
    message_flow_log_mode_t message_flow () const noexcept { return _message_flow; }
    double sample_rate () const noexcept { return _sample_rate; }
    bool include_message_sizes () const noexcept { return _include_message_sizes; }
    bool include_native_diagnostics () const noexcept { return _include_native_diagnostics; }
    const std::optional<std::string> &log_file () const noexcept { return _log_file; }
    const std::optional<std::string> &node_id () const noexcept { return _node_id; }
    const std::shared_ptr<std::atomic<message_flow_log_mode_t>> &live_mode () const noexcept
    {
        return _live_mode;
    }

    // The mode actually in effect: the live (runtime-mutable) override if installed,
    // else the configured mode. Read live on every dispatch (relaxed atomic load).
    message_flow_log_mode_t effective_message_flow () const noexcept
    {
        return _live_mode ? _live_mode->load (std::memory_order_relaxed) : _message_flow;
    }

  private:
    // Only the dispatch options builder writes these (enforces builder-only config).
    friend struct dispatch_options_t;

    message_flow_log_mode_t _message_flow = message_flow_log_mode_t::errors_only;
    double _sample_rate = 1.0;
    bool _include_message_sizes = true;
    bool _include_native_diagnostics = false;
    // When set, tracing/error logs go to this dedicated file (separated from app
    // logs). Empty = shared app logger (or std::clog if no sink).
    std::optional<std::string> _log_file;
    // Node/runtime identity stamped on each trace line (`node=`) for cross-node
    // aggregation of process-local correlation ids. App-provided; empty = omitted.
    std::optional<std::string> _node_id;
    // Shared, runtime-mutable mode override (installed by the host at apply); copying
    // dispatch options shares the same atomic so every surface observes changes.
    std::shared_ptr<std::atomic<message_flow_log_mode_t>> _live_mode;
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
    // When set, message-flow transitions and dispatch errors are emitted through
    // the framework logger (so app.logging().use_file(...) captures them) instead
    // of the std::clog fallback. Wired by the host at apply() only if a logging
    // output sink is configured; otherwise left empty to preserve clog behavior.
    std::optional<logger_t<>> diagnostics_logger;

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

    // Fluent diagnostics/tracing config (chains off configure_dispatch()). This is
    // the only supported way to set diagnostics (the fields are encapsulated).
    dispatch_options_t &message_flow (message_flow_log_mode_t mode)
    {
        diagnostics._message_flow = mode;
        return *this;
    }

    dispatch_options_t &trace_sample_rate (double rate)
    {
        diagnostics._sample_rate = rate;
        return *this;
    }

    dispatch_options_t &include_message_sizes (bool include)
    {
        diagnostics._include_message_sizes = include;
        return *this;
    }

    dispatch_options_t &include_native_diagnostics (bool include)
    {
        diagnostics._include_native_diagnostics = include;
        return *this;
    }

    // Send message-flow/error tracing to its own file (separated from app logs).
    dispatch_options_t &trace_log_file (std::string path)
    {
        diagnostics._log_file = std::move (path);
        return *this;
    }

    // Node/runtime identity stamped on every trace line (`node=`) for cross-node
    // aggregation of process-local correlation ids.
    dispatch_options_t &trace_node_id (std::string id)
    {
        diagnostics._node_id = std::move (id);
        return *this;
    }

    // Install a shared, runtime-mutable mode cell (host wiring for runtime toggle;
    // app_t::set_message_flow_mode flips it). Overrides the configured message_flow.
    dispatch_options_t &message_flow_live (
      std::shared_ptr<std::atomic<message_flow_log_mode_t>> live)
    {
        diagnostics._live_mode = std::move (live);
        return *this;
    }
};

} // namespace zlink::framework
