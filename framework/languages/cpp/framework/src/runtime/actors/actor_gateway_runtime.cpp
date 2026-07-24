/* SPDX-License-Identifier: FSL-1.1-ALv2 */

#include "actor_gateway_runtime.hpp"

#include "runtime/diagnostics/dispatch_error_reporter.hpp"
#include "runtime/diagnostics/message_flow_tracer.hpp"
#include "runtime/spots/spot_route_packets.hpp"

#include <utility>

namespace zlink::framework
{

using detail::stream_header_flags_t;
using detail::stream_header_t;
using detail::stream_message_kind_t;

namespace detail
{

namespace
{
thread_local std::optional<stream_header_t> current_stream_relay_header;

stream_header_t actor_relay_header (stream_message_kind_t kind, std::string packet_name)
{
    return stream_header_t (kind, stream_codec_t::json, stream_header_flags_t::none, std::nullopt,
                            std::move (packet_name));
}

stream_codec_t stream_codec_from_content_type (std::string_view content_type)
{
    if (content_type == "application/json")
        return stream_codec_t::json;
    if (content_type == "application/x-msgpack")
        return stream_codec_t::message_pack;
    if (content_type == "application/x-protobuf")
        return stream_codec_t::protobuf;
    return stream_codec_t::raw;
}
}

void enter_stream_relay_dispatch (const stream_header_t &header)
{
    current_stream_relay_header = header;
}

void exit_stream_relay_dispatch () noexcept
{
    current_stream_relay_header.reset ();
}

std::optional<stream_header_t> current_stream_relay_dispatch ()
{
    return current_stream_relay_header;
}

} // namespace detail

actor_ref_t::actor_ref_t (node_rid_t node_rid,
                          std::string actor_type,
                          std::string actor_id,
                          std::uint64_t generation) :
    _node_rid (std::move (node_rid)),
    _actor_type (std::move (actor_type)),
    _actor_id (std::move (actor_id)),
    _generation (generation)
{
}

const node_rid_t &actor_ref_t::node_rid () const noexcept
{
    return _node_rid;
}

std::string_view actor_ref_t::actor_type () const noexcept
{
    return _actor_type;
}

std::string_view actor_ref_t::actor_id () const noexcept
{
    return _actor_id;
}

std::uint64_t actor_ref_t::generation () const noexcept
{
    return _generation;
}

bool actor_ref_t::empty () const noexcept
{
    return _actor_id.empty () || _actor_type.empty ();
}

bound_session_t::bound_session_t () : _state (std::make_shared<detail::actor_gateway_state_t> ())
{
}

bound_session_t::bound_session_t (std::shared_ptr<detail::actor_gateway_state_t> state,
                                  actor_ref_t actor_ref,
                                  std::uint64_t expected_binding_generation) :
    _state (std::move (state)), _actor_ref (std::move (actor_ref)),
    _expected_binding_generation (expected_binding_generation)
{
}

bound_session_t::~bound_session_t () = default;
bound_session_t::bound_session_t (bound_session_t &&) noexcept = default;
bound_session_t &bound_session_t::operator= (bound_session_t &&) noexcept = default;

bound_session_send_call_t bound_session_t::send (const message_t &payload)
{
    if (!_state) {
        return bound_session_send_call_t (send_call_t (
          result_t<void>::failure (framework_error_kind_t::request_protocol_error,
                                   "bound session send requires actor gateway state")));
    }
    if (!_state->serializers) {
        return bound_session_send_call_t (send_call_t (
          result_t<void>::failure (framework_error_kind_t::request_protocol_error,
                                   "bound session send requires a serializer registry")));
    }
    try {
        const auto codec =
          detail::stream_codec_from_content_type (_state->serializers->content_type (payload._type));
        return send_erased ("actor.push", codec,
                            detail::message_to_raw (payload, *_state->serializers));
    }
    catch (const framework_exception_t &error) {
        return bound_session_send_call_t (send_call_t (
          detail::result_access_t::failure<void> (error)));
    }
}

bound_session_send_call_t bound_session_t::send_typed (
  std::string packet_name,
  std::type_index message_type,
  std::function<encoded_payload_t (serializer_registry_t &)> encode_payload)
{
    if (!_state || !_state->serializers) {
        return bound_session_send_call_t (send_call_t (
          result_t<void>::failure (framework_error_kind_t::request_protocol_error,
                                   "bound session send requires a serializer registry")));
    }
    try {
        auto payload = detail::encoded_payload_to_raw (encode_payload (*_state->serializers));
        const auto codec = detail::stream_codec_from_content_type (
          _state->serializers->content_type (message_type));
        return send_erased (std::move (packet_name), codec, payload);
    }
    catch (const framework_exception_t &error) {
        return bound_session_send_call_t (send_call_t (
          detail::result_access_t::failure<void> (error)));
    }
}

bound_session_send_call_t bound_session_t::send_typed (std::string packet_name,
                                                       std::type_index message_type,
                                                       const void *message)
{
    if (!_state || !_state->serializers) {
        return bound_session_send_call_t (send_call_t (
          result_t<void>::failure (framework_error_kind_t::request_protocol_error,
                                   "bound session send requires a serializer registry")));
    }
    try {
        auto payload =
          detail::encoded_payload_to_raw (_state->serializers->serialize (message_type, message));
        const auto codec = detail::stream_codec_from_content_type (
          _state->serializers->content_type (message_type));
        return send_erased (std::move (packet_name), codec, payload);
    }
    catch (const framework_exception_t &error) {
        return bound_session_send_call_t (send_call_t (
          detail::result_access_t::failure<void> (error)));
    }
}

task_t<void> bound_session_t::disconnect ()
{
    const std::lock_guard lock (_state->mutex);
    const auto found = _state->actors_by_id.find (std::string (_actor_ref.actor_id ()));
    if (found != _state->actors_by_id.end ()) {
        if (found->second.ref.generation () != _actor_ref.generation ()) {
            return task_t<void> (detail::boundary_failure<void> (detail::boundary_error_t::stale_generation, "actor generation is stale"));
        }
        found->second.bound = false;
        found->second.disconnected = true;
    }
    return task_t<void> (result_t<void>::success ());
}

bound_session_send_call_t bound_session_t::send_erased (std::string packet_name,
                                                        stream_codec_t codec,
                                                        const zlink::message_t &payload)
{
    std::function<task_t<void> (std::string, const zlink::message_t &)> sink;
    detail::actor_gateway_state_t::bound_session_sender_t remote_sender;
    stream_header_t header;
    {
        const std::lock_guard lock (_state->mutex);
        const auto actor_id = std::string (_actor_ref.actor_id ());
        const auto found = _state->actors_by_id.find (actor_id);
        remote_sender = _state->bound_session_sender;
        if (found != _state->actors_by_id.end () && found->second.disconnected) {
            return bound_session_send_call_t (send_call_t (detail::boundary_failure<void> (detail::boundary_error_t::disconnected, "actor session is disconnected")));
        }
        if (found == _state->actors_by_id.end () || !found->second.bound) {
            if (!remote_sender) {
                return bound_session_send_call_t (send_call_t (result_t<void>::failure (
                  framework_error_kind_t::actor_session_not_bound, "actor session is not bound")));
            }
            header = stream_header_t (stream_message_kind_t::send, codec,
                                      stream_header_flags_t::none, std::nullopt, packet_name);
        }
        else if (found->second.ref.actor_type () != _actor_ref.actor_type ()) {
            return bound_session_send_call_t (
              send_call_t (result_t<void>::failure (framework_error_kind_t::actor_type_mismatch,
                                                    "actor id is already bound to another type")));
        }
        else if (found->second.ref.generation () != _actor_ref.generation ()) {
            return bound_session_send_call_t (send_call_t (detail::boundary_failure<void> (detail::boundary_error_t::stale_generation, "actor generation is stale")));
        }
        else {
            found->second.ref = _actor_ref;
            header = stream_header_t (stream_message_kind_t::send, codec,
                                      stream_header_flags_t::none, std::nullopt, packet_name);
            _state->bound_session_pushes.push_back (
              detail::relayed_frame_t{found->second.ref, header, payload});
            const auto found_sink = _state->bound_session_sinks.find (actor_id);
            if (found_sink != _state->bound_session_sinks.end ()) {
                sink = found_sink->second;
            }
        }
    }
    if (!sink && !remote_sender) {
        return bound_session_send_call_t (
          send_call_t (result_t<void>::failure (framework_error_kind_t::actor_session_not_bound,
                                                "actor bound session has no send sink")));
    }
    return bound_session_send_call_t (send_call_t (
      std::move (packet_name),
      [sink = std::move (sink), remote_sender = std::move (remote_sender),
       actor_ref = _actor_ref, expected_binding_generation = _expected_binding_generation,
       header = std::move (header),
       payload] (const std::string &name,
                  const send_call_t::metadata_map_t &) mutable {
          try {
              if (sink)
                  return sink (name, payload).result ();
              return remote_sender (
                actor_ref, expected_binding_generation, header, payload);
          }
          catch (const framework_exception_t &error) {
              return detail::result_access_t::failure<void> (error);
          }
          catch (const std::exception &error) {
              return result_t<void>::failure (framework_error_kind_t::request_failed,
                                              error.what ());
          }
      }));
}

actor_context_t::actor_context_t () :
    _state (std::make_shared<detail::actor_gateway_state_t> ()),
    _actor_ref (std::make_shared<actor_ref_t> ())
{
}

actor_context_t::actor_context_t (std::shared_ptr<detail::actor_gateway_state_t> state,
                                  actor_ref_t actor_ref,
                                  std::uint64_t source_binding_generation) :
    _state (std::move (state)), _actor_ref (std::make_shared<actor_ref_t> (std::move (actor_ref))),
    _source_binding_generation (source_binding_generation)
{
}

actor_context_t::~actor_context_t () = default;
actor_context_t::actor_context_t (actor_context_t &&) noexcept = default;
actor_context_t &actor_context_t::operator= (actor_context_t &&) noexcept = default;

const actor_ref_t &actor_context_t::actor_ref () const noexcept
{
    return *_actor_ref;
}

std::optional<spot_id_t> actor_context_t::spot_id () const
{
    detail::actor_gateway_state_t::membership_query_t query;
    {
        const std::lock_guard lock (_state->mutex);
        query = _state->membership_query;
    }
    if (!query || _actor_ref->empty ()) {
        return std::nullopt;
    }
    return query (*_actor_ref);
}

serializer_registry_t *actor_context_t::serializer_registry () const noexcept
{
    return _state->serializers;
}

std::optional<zlink::message_t> actor_context_t::create_payload () const
{
    const std::lock_guard lock (_state->mutex);
    const auto found = _state->actors_by_id.find (std::string (_actor_ref->actor_id ()));
    if (found == _state->actors_by_id.end ()) {
        return std::nullopt;
    }
    return found->second.create_payload;
}

bound_session_t actor_context_t::bound_session () const
{
    return bound_session_t (_state, *_actor_ref, _source_binding_generation);
}

result_t<detail::actor_join_reply_t>
actor_context_t::join_spot_erased (spot_id_t spot_id,
                                   const zlink::message_t &request,
                                   std::chrono::milliseconds timeout)
{
    detail::actor_gateway_state_t::join_spot_dispatcher_t dispatcher;
    {
        const std::lock_guard lock (_state->mutex);
        if (_actor_ref->empty ()) {
            return result_t<detail::actor_join_reply_t>::failure (
              framework_error_kind_t::actor_route_not_found, "actor ref is empty");
        }
        if (spot_id.empty ()) {
            return result_t<detail::actor_join_reply_t>::failure (
              framework_error_kind_t::spot_route_not_found, "spot id is empty");
        }
        if (!_state->join_spot_dispatcher) {
            return result_t<detail::actor_join_reply_t>::failure (
              framework_error_kind_t::actor_dispatch_handler_not_found,
              "actor join spot dispatcher is not configured");
        }
        dispatcher = _state->join_spot_dispatcher;
    }
    detail::message_flow_tracer_t (_state->dispatch).trace (message_flow_outcome_t::sent, [&] {
        return message_flow_event_t{message_flow_outcome_t::sent,
                                    dispatch_error_surface_t::spot_actor,
                                    dispatch_message_kind_t::actor_request,
                                    std::nullopt,
                                    std::nullopt,
                                    std::nullopt,
                                    std::nullopt,
                                    std::nullopt,
                                    std::string (spot_id),
                                    std::string (_actor_ref->actor_id ()),
                                    std::nullopt};
    });
    auto joined = dispatcher (*_actor_ref, spot_id, request, timeout);
    if (!joined) {
        const auto *error = joined.error ();
        return result_t<detail::actor_join_reply_t>::failure (
          joined.error_kind (), error != nullptr ? error->what () : "actor join spot failed");
    }
    detail::message_flow_tracer_t (_state->dispatch)
      .trace (message_flow_outcome_t::reply_received, [&] {
          return message_flow_event_t{message_flow_outcome_t::reply_received,
                                      dispatch_error_surface_t::spot_actor,
                                      dispatch_message_kind_t::actor_request,
                                      std::nullopt,
                                      std::nullopt,
                                      std::nullopt,
                                      std::nullopt,
                                      std::nullopt,
                                      std::string (spot_id),
                                      std::string (_actor_ref->actor_id ()),
                                      std::nullopt};
      });

    if (joined.value ().result_code == 0) {
        const std::lock_guard lock (_state->mutex);
        *_actor_ref = joined.value ().actor;
        auto found = _state->actors_by_id.find (std::string (_actor_ref->actor_id ()));
        if (found != _state->actors_by_id.end ()) {
            found->second.ref = *_actor_ref;
        }
    }
    return joined;
}

actor_join_call_t actor_context_t::join_entry_spot_payload (
  const zlink::message_t &request)
{
    return actor_join_call_t (
      [context = *this, request] (std::chrono::milliseconds timeout) mutable {
          detail::actor_gateway_state_t::join_entry_spot_dispatcher_t dispatcher;
          zlink::message_t effective_request = request;
          {
              const std::lock_guard lock (context._state->mutex);
              if (context._actor_ref->empty ()) {
                  throw framework_exception_t (
                    framework_error_kind_t::actor_route_not_found,
                    "actor ref is empty");
              }
              if (!context._state->join_entry_spot_dispatcher) {
                  throw framework_exception_t (
                    framework_error_kind_t::actor_dispatch_handler_not_found,
                    "actor join entry spot dispatcher is not configured");
              }
              dispatcher = context._state->join_entry_spot_dispatcher;
              if (effective_request.to_string ().empty ()) {
                  const auto found = context._state->actors_by_id.find (
                    std::string (context._actor_ref->actor_id ()));
                  if (found != context._state->actors_by_id.end ()
                      && found->second.create_payload) {
                      effective_request = *found->second.create_payload;
                  }
              }
          }

          auto joined = dispatcher (*context._actor_ref, effective_request, timeout);
          if (!joined) {
              const auto *error = joined.error ();
              throw framework_exception_t (
                joined.error_kind (),
                error != nullptr ? error->what () : "actor join entry spot failed");
          }

          if (joined.value ().result_code == 0) {
              const std::lock_guard lock (context._state->mutex);
              *context._actor_ref = joined.value ().actor;
              auto found =
                context._state->actors_by_id.find (std::string (context._actor_ref->actor_id ()));
              if (found != context._state->actors_by_id.end ()) {
                  found->second.ref = *context._actor_ref;
              }
          }
      });
}

session_actor_t::session_actor_t () : _state (std::make_shared<detail::actor_gateway_state_t> ())
{
}

session_actor_t::session_actor_t (std::shared_ptr<detail::actor_gateway_state_t> state,
                                  actor_ref_t ref,
                                  std::uint64_t binding_token) :
    _state (std::move (state)), _ref (std::move (ref)),
    _binding_token (binding_token)
{
}

session_actor_t::~session_actor_t () = default;
session_actor_t::session_actor_t (session_actor_t &&) noexcept = default;
session_actor_t &session_actor_t::operator= (session_actor_t &&) noexcept = default;

const actor_ref_t &session_actor_t::ref () const noexcept
{
    return _ref;
}

std::string_view session_actor_t::actor_id () const noexcept
{
    return _ref.actor_id ();
}

actor_context_t session_actor_t::context () const
{
    return actor_context_t (_state, _ref);
}

bound_session_t session_actor_t::bound_session () const
{
    return bound_session_t (_state, _ref);
}

task_t<void> session_actor_t::relay_internal (const zlink::message_t &payload)
{
    const auto header = detail::current_stream_relay_dispatch ();
    if (!header) {
        return task_t<void> (
          result_t<void>::failure (framework_error_kind_t::request_protocol_error,
                                   "actor relay requires current stream dispatch state"));
    }
    detail::actor_gateway_state_t::relay_dispatcher_t dispatcher;
    {
        const std::lock_guard lock (_state->mutex);
        if (_ref.empty ()) {
            return task_t<void> (result_t<void>::failure (
              framework_error_kind_t::actor_route_not_found, "session actor is not bound"));
        }
        const auto found = _state->actors_by_id.find (std::string (_ref.actor_id ()));
        if (found != _state->actors_by_id.end () && found->second.disconnected) {
            return task_t<void> (detail::boundary_failure<void> (detail::boundary_error_t::disconnected,
                                                          "actor session is disconnected"));
        }
        if (found == _state->actors_by_id.end ()) {
            return task_t<void> (result_t<void>::failure (
              framework_error_kind_t::actor_route_not_found, "actor route is not found"));
        }
        if (!found->second.bound) {
            return task_t<void> (result_t<void>::failure (
              framework_error_kind_t::actor_session_not_bound, "actor session is not bound"));
        }
        if (_binding_token != 0
            && found->second.binding_token != _binding_token) {
            return task_t<void> (result_t<void>::failure (
              framework_error_kind_t::actor_session_not_bound,
              "actor session binding is stale"));
        }
        if (found->second.ref.generation () != _ref.generation ()) {
            return task_t<void> (detail::boundary_failure<void> (detail::boundary_error_t::stale_generation, "actor generation is stale"));
        }
        _ref = found->second.ref;
        if (!_state->relay_dispatcher) {
            _state->relayed_frames.push_back (detail::relayed_frame_t{_ref, *header, payload});
            return task_t<void> (result_t<void>::success ());
        }
        dispatcher = _state->relay_dispatcher;
    }

    auto dispatched = dispatcher (_ref, context (), *header, payload);
    if (!dispatched) {
        return task_t<void> (detail::propagate_failure<void> (dispatched, "actor relay failed"));
    }
    return task_t<void> (result_t<void>::success ());
}

task_t<void> session_actor_t::relay (const zlink::message_t &payload)
{
    return detail::submit_one_way_task (
      [this, payload] { return relay_internal (payload).result (); });
}

task_t<void> session_actor_t::relay (std::string packet_name,
                                    const zlink::message_t &payload)
{
    return detail::submit_one_way_task (
      [this, packet_name = std::move (packet_name), payload] {
          detail::enter_stream_relay_dispatch (
            detail::actor_relay_header (stream_message_kind_t::send, packet_name));
          auto result = relay_internal (payload).result ();
          detail::exit_stream_relay_dispatch ();
          return result;
      });
}

relay_request_call_t session_actor_t::relay_request (const zlink::message_t &payload)
{
    const auto header = detail::current_stream_relay_dispatch ();
    if (!header) {
        return relay_request_call_t (result_t<zlink::message_t>::failure (
          framework_error_kind_t::request_protocol_error,
          "actor relay request requires current stream dispatch state"));
    }
    detail::actor_gateway_state_t::relay_dispatcher_t dispatcher;
    {
        const std::lock_guard lock (_state->mutex);
        if (_ref.empty ()) {
            return relay_request_call_t (result_t<zlink::message_t>::failure (
              framework_error_kind_t::actor_route_not_found, "session actor is not bound"));
        }
        const auto found = _state->actors_by_id.find (std::string (_ref.actor_id ()));
        if (found != _state->actors_by_id.end () && found->second.disconnected) {
            return relay_request_call_t (detail::boundary_failure<zlink::message_t> (detail::boundary_error_t::disconnected, "actor session is disconnected"));
        }
        if (found == _state->actors_by_id.end ()) {
            return relay_request_call_t (result_t<zlink::message_t>::failure (
              framework_error_kind_t::actor_route_not_found, "actor route is not found"));
        }
        if (!found->second.bound) {
            return relay_request_call_t (result_t<zlink::message_t>::failure (
              framework_error_kind_t::actor_session_not_bound, "actor session is not bound"));
        }
        if (_binding_token != 0
            && found->second.binding_token != _binding_token) {
            return relay_request_call_t (result_t<zlink::message_t>::failure (
              framework_error_kind_t::actor_session_not_bound,
              "actor session binding is stale"));
        }
        if (found->second.ref.generation () != _ref.generation ()) {
            return relay_request_call_t (detail::boundary_failure<zlink::message_t> (detail::boundary_error_t::stale_generation, "actor generation is stale"));
        }
        _ref = found->second.ref;
        if (!_state->relay_dispatcher) {
            _state->relayed_frames.push_back (detail::relayed_frame_t{_ref, *header, payload});
            return relay_request_call_t (result_t<zlink::message_t>::failure (
              framework_error_kind_t::actor_dispatch_handler_not_found,
              "actor relay dispatcher is not configured"));
        }
        dispatcher = _state->relay_dispatcher;
    }
    auto dispatched = dispatcher (_ref, context (), *header, payload);
    if (!dispatched) {
        return relay_request_call_t (detail::propagate_failure<zlink::message_t> (dispatched, "actor relay failed"));
    }
    if (!dispatched.value ()) {
        /* framework API §2.4.3: reply frame이 없는 local 경로다. caller의 task를 framework
         * 오류로 완료하고, 그 실패를 fail_caller 액션으로 관측할 수 있게 남긴다. */
        const framework_exception_t missing_reply (framework_error_kind_t::request_protocol_error,
                                                   "actor relay request has no reply");
        detail::dispatch_error_reporter_t (_state->dispatch)
          .report (message_dispatch_error_event_t{
            dispatch_error_surface_t::spot_actor, dispatch_message_kind_t::actor_request,
            dispatch_error_reason_t::reply_path_missing, dispatch_error_action_t::fail_caller,
            std::string (header->packet_name ()), std::nullopt, std::nullopt, std::nullopt,
            std::string (_ref.actor_id ()), std::nullopt, std::nullopt,
            std::make_exception_ptr (missing_reply)});
        return relay_request_call_t (
          detail::result_access_t::failure<zlink::message_t> (missing_reply));
    }
    return relay_request_call_t (
      result_t<zlink::message_t>::success (std::move (*dispatched.value ())));
}

relay_request_call_t session_actor_t::relay_request (std::string packet_name,
                                                     const zlink::message_t &payload)
{
    detail::enter_stream_relay_dispatch (
      detail::actor_relay_header (stream_message_kind_t::request, std::move (packet_name)));
    auto call = relay_request (payload);
    detail::exit_stream_relay_dispatch ();
    return call;
}

task_t<void> session_actor_t::notify_disconnected ()
{
    detail::actor_gateway_state_t::disconnect_dispatcher_t dispatcher;
    std::string session_id;
    std::uint64_t binding_token = 0;
    {
        const std::lock_guard lock (_state->mutex);
        const auto found = _state->actors_by_id.find (std::string (_ref.actor_id ()));
        if (found != _state->actors_by_id.end ()) {
            if (_binding_token != 0
                && found->second.binding_token != _binding_token) {
                return task_t<void> (result_t<void>::failure (
                  framework_error_kind_t::actor_session_not_bound,
                  "actor session binding is stale"));
            }
            if (found->second.ref.generation () != _ref.generation ()) {
                return task_t<void> (detail::boundary_failure<void> (detail::boundary_error_t::stale_generation, "actor generation is stale"));
            }
            if (found->second.disconnected)
                return task_t<void> (result_t<void>::success ());
            _ref = found->second.ref;
            found->second.bound = false;
            found->second.disconnected = true;
            session_id = found->second.binding_session_id;
            binding_token = found->second.binding_token;
        }
        dispatcher = _state->disconnect_dispatcher;
    }
    result_t<void> notified = result_t<void>::success ();
    if (dispatcher) {
        try {
            notified = dispatcher (_ref);
        }
        catch (const framework_exception_t &error) {
            notified = detail::result_access_t::failure<void> (error);
        }
        catch (const std::exception &error) {
            notified = result_t<void>::failure (
              framework_error_kind_t::actor_dispatch_handler_not_found,
              error.what ());
        }
    }
    if (binding_token != 0) {
        detail::actor_gateway_runtime_t (_state).unbind_session_stream (
          std::string (_ref.actor_id ()), std::move (session_id),
          binding_token);
    }
    return task_t<void> (std::move (notified));
}

session_actor_manager_t::session_actor_manager_t () :
    _state (std::make_shared<detail::actor_gateway_state_t> ()),
    _binding_context (std::make_shared<detail::session_actor_binding_context_t> ())
{
}

session_actor_manager_t::session_actor_manager_t (
  std::shared_ptr<detail::actor_gateway_state_t> state) :
    _state (std::move (state)),
    _binding_context (std::make_shared<detail::session_actor_binding_context_t> ())
{
}

session_actor_manager_t::~session_actor_manager_t () = default;
session_actor_manager_t::session_actor_manager_t (session_actor_manager_t &&) noexcept = default;
session_actor_manager_t &
session_actor_manager_t::operator= (session_actor_manager_t &&) noexcept = default;

result_t<session_actor_t> session_actor_manager_t::create (std::string actor_type,
                                                           std::string actor_id)
{
    return create_erased (std::move (actor_type), std::move (actor_id), std::nullopt);
}

result_t<session_actor_t> session_actor_manager_t::create (std::string actor_type,
                                                           std::string actor_id,
                                                           const zlink::message_t &request)
{
    return create_erased (std::move (actor_type), std::move (actor_id), request);
}

result_t<session_actor_t> session_actor_manager_t::create (std::string actor_type,
                                                           std::string actor_id,
                                                           const message_t &request)
{
    if (!_state || !_state->serializers) {
        return result_t<session_actor_t>::failure (framework_error_kind_t::request_protocol_error,
                                                   "actor create requires a serializer registry");
    }
    try {
        return create_erased (std::move (actor_type), std::move (actor_id),
                              detail::message_to_raw (request, *_state->serializers));
    }
    catch (const framework_exception_t &error) {
        return detail::result_access_t::failure<session_actor_t> (error);
    }
}

result_t<session_actor_t> session_actor_manager_t::create_erased (
  std::string actor_type, std::string actor_id, std::optional<zlink::message_t> request)
{
    if (actor_type.empty () || actor_id.empty ()) {
        return result_t<session_actor_t>::failure (framework_error_kind_t::request_protocol_error,
                                                   "actor type and id are required");
    }
    detail::actor_gateway_state_t::create_dispatcher_t dispatcher;
    {
        const std::lock_guard lock (_state->mutex);
        if (_state->actors_by_id.find (actor_id) != _state->actors_by_id.end ()) {
            return result_t<session_actor_t>::failure (
              framework_error_kind_t::actor_already_exists, "actor already exists");
        }
        dispatcher = _state->create_dispatcher;
    }
    actor_ref_t ref;
    if (dispatcher) {
        auto created = dispatcher (actor_type, actor_id, request);
        if (!created) {
            return result_t<session_actor_t>::failure (
              created.error_kind (),
              created.error () ? created.error ()->what () : "Actor creation failed");
        }
        ref = created.value ();
    } else {
        ref = actor_ref_t (
          node_rid_t::from_string (std::string (detail::local_actor_node_placeholder)),
          actor_type, actor_id, 1);
    }
    detail::actor_record_t record{ref, false, false};
    record.create_payload = std::move (request);
    {
        const std::lock_guard lock (_state->mutex);
        const auto [_, inserted] = _state->actors_by_id.emplace (actor_id, std::move (record));
        if (!inserted) {
            return result_t<session_actor_t>::failure (
              framework_error_kind_t::actor_already_exists, "actor already exists");
        }
    }
    return result_t<session_actor_t>::success (session_actor_t (_state, ref));
}

std::optional<session_actor_t> session_actor_manager_t::find (std::string actor_id) const
{
    const std::lock_guard lock (_state->mutex);
    const auto found = _state->actors_by_id.find (actor_id);
    if (found == _state->actors_by_id.end ()) {
        return std::nullopt;
    }
    return session_actor_t (
      _state, found->second.ref, found->second.binding_token);
}

result_t<session_actor_t> session_actor_manager_t::get_or_create (std::string actor_type,
                                                                  std::string actor_id)
{
    return get_or_create_erased (std::move (actor_type), std::move (actor_id), std::nullopt);
}

result_t<session_actor_t> session_actor_manager_t::get_or_create (std::string actor_type,
                                                                  std::string actor_id,
                                                                  const zlink::message_t &request)
{
    return get_or_create_erased (std::move (actor_type), std::move (actor_id), request);
}

result_t<session_actor_t> session_actor_manager_t::get_or_create (std::string actor_type,
                                                                  std::string actor_id,
                                                                  const message_t &request)
{
    if (!_state || !_state->serializers) {
        return result_t<session_actor_t>::failure (
          framework_error_kind_t::request_protocol_error,
          "actor get or create requires a serializer registry");
    }
    try {
        return get_or_create_erased (std::move (actor_type), std::move (actor_id),
                                     detail::message_to_raw (request, *_state->serializers));
    }
    catch (const framework_exception_t &error) {
        return detail::result_access_t::failure<session_actor_t> (error);
    }
}

result_t<session_actor_t> session_actor_manager_t::get_or_create_erased (
  std::string actor_type, std::string actor_id, std::optional<zlink::message_t> request)
{
    if (auto actor = find (actor_id)) {
        if (actor->ref ().actor_type () != actor_type) {
            return result_t<session_actor_t>::failure (framework_error_kind_t::actor_type_mismatch,
                                                       "actor id is already bound to another type");
        }
        return result_t<session_actor_t>::success (*actor);
    }
    return create_erased (std::move (actor_type), std::move (actor_id), std::move (request));
}

zlink::message_t session_actor_manager_t::serialize_request (std::type_index request_type,
                                                             const void *request) const
{
    if (!_state || !_state->serializers) {
        throw framework_exception_t (framework_error_kind_t::request_protocol_error,
                                     "actor create requires a serializer registry");
    }
    return detail::encoded_payload_to_raw (_state->serializers->serialize (request_type, request));
}

request_call_t<session_actor_t> session_actor_manager_t::bind (actor_ref_t actor_ref)
{
    {
        const std::lock_guard lock (_state->mutex);
        if (actor_ref.empty ()) {
            return request_call_t<session_actor_t> (result_t<session_actor_t>::failure (
              framework_error_kind_t::actor_route_not_found, "actor ref is empty"));
        }
        auto found = _state->actors_by_id.find (std::string (actor_ref.actor_id ()));
        if (found == _state->actors_by_id.end ()) {
            _state->actors_by_id.emplace (std::string (actor_ref.actor_id ()),
                                          detail::actor_record_t{actor_ref, true, false});
        } else {
            if (found->second.ref.actor_type () != actor_ref.actor_type ()) {
                return request_call_t<session_actor_t> (
                  result_t<session_actor_t>::failure (
                    framework_error_kind_t::actor_type_mismatch,
                    "actor id is already bound to another type"));
            }
            found->second.ref = actor_ref;
            found->second.bound = true;
            found->second.disconnected = false;
        }
    }
    try {
        const auto token = bind_current_session (actor_ref);
        return request_call_t<session_actor_t> (
          result_t<session_actor_t>::success (
            session_actor_t (_state, actor_ref, token)));
    }
    catch (const framework_exception_t &error) {
        return request_call_t<session_actor_t> (
          detail::result_access_t::failure<session_actor_t> (error));
    }
}

request_call_t<session_actor_t> session_actor_manager_t::bind_or_get (actor_ref_t actor_ref)
{
    {
        const std::lock_guard lock (_state->mutex);
        if (actor_ref.empty ()) {
            return request_call_t<session_actor_t> (result_t<session_actor_t>::failure (
              framework_error_kind_t::actor_route_not_found, "actor ref is empty"));
        }
        auto found = _state->actors_by_id.find (std::string (actor_ref.actor_id ()));
        if (found == _state->actors_by_id.end ()) {
            _state->actors_by_id.emplace (std::string (actor_ref.actor_id ()),
                                          detail::actor_record_t{actor_ref, true, false});
        } else {
            if (found->second.ref.actor_type () != actor_ref.actor_type ()) {
                return request_call_t<session_actor_t> (
                  result_t<session_actor_t>::failure (
                    framework_error_kind_t::actor_type_mismatch,
                    "actor id is already bound to another type"));
            }
            if (found->second.ref.generation () <= actor_ref.generation ()) {
                found->second.ref = actor_ref;
            }
            actor_ref = found->second.ref;
            found->second.bound = true;
            found->second.disconnected = false;
        }
    }
    try {
        const auto token = bind_current_session (actor_ref);
        return request_call_t<session_actor_t> (
          result_t<session_actor_t>::success (
            session_actor_t (_state, actor_ref, token)));
    }
    catch (const framework_exception_t &error) {
        return request_call_t<session_actor_t> (
          detail::result_access_t::failure<session_actor_t> (error));
    }
}

std::uint64_t session_actor_manager_t::bind_current_session (
  const actor_ref_t &actor_ref)
{
    if (!_binding_context) {
        return 0;
    }
    const auto actor_id = std::string (actor_ref.actor_id ());
    const std::lock_guard binding_lock (_binding_context->mutex);
    if (!_binding_context->stream) {
        return 0;
    }
    std::uint64_t token;
    {
        const std::lock_guard lock (_state->mutex);
        token = _state->next_binding_token++;
    }
    _binding_context->actor_tokens[actor_id] = token;
    try {
        detail::actor_gateway_runtime_t (_state).bind_session_stream (
          actor_id, *_binding_context->stream, _binding_context->codec,
          _binding_context->session_id, token);
        if (_binding_context->native_binder) {
            auto bound = _binding_context->native_binder (actor_ref);
            if (!bound) {
                throw framework_exception_t (
                  bound.error_kind (),
                  bound.error () ? bound.error ()->what ()
                                 : "Core STREAM actor binding failed");
            }
        }
    }
    catch (...) {
        const auto current = _binding_context->actor_tokens.find (actor_id);
        if (current != _binding_context->actor_tokens.end () && current->second == token) {
            _binding_context->actor_tokens.erase (current);
        }
        detail::actor_gateway_runtime_t (_state).unbind_session_stream (
          actor_id, _binding_context->session_id, token);
        throw;
    }
    return token;
}

void detail::session_actor_manager_access_t::attach (session_actor_manager_t &manager,
                                                     stream_t stream)
{
    const std::lock_guard lock (manager._binding_context->mutex);
    manager._binding_context->session_id = stream.session_id ();
    manager._binding_context->stream = std::move (stream);
}

void detail::session_actor_manager_access_t::set_codec (session_actor_manager_t &manager,
                                                        stream_codec_t codec)
{
    const std::lock_guard lock (manager._binding_context->mutex);
    manager._binding_context->codec = codec;
}

void detail::session_actor_manager_access_t::bind_native (
  session_actor_manager_t &manager,
  std::function<result_t<void> (const actor_ref_t &)> binder)
{
    const std::lock_guard lock (manager._binding_context->mutex);
    manager._binding_context->native_binder = std::move (binder);
}

void detail::session_actor_manager_access_t::disconnect (session_actor_manager_t &manager) noexcept
{
    if (!manager._binding_context) {
        return;
    }
    std::map<std::string, std::uint64_t> bindings;
    std::string session_id;
    {
        const std::lock_guard lock (manager._binding_context->mutex);
        bindings = manager._binding_context->actor_tokens;
        manager._binding_context->actor_tokens.clear ();
        session_id = manager._binding_context->session_id;
        manager._binding_context->stream.reset ();
    }
    for (const auto &[actor_id, token] : bindings) {
        detail::actor_gateway_state_t::disconnect_dispatcher_t dispatcher;
        actor_ref_t actor;
        {
            const std::lock_guard lock (manager._state->mutex);
            const auto found = manager._state->actors_by_id.find (actor_id);
            if (found != manager._state->actors_by_id.end ()
                && found->second.binding_session_id == session_id
                && found->second.binding_token == token
                && found->second.bound
                && !found->second.disconnected) {
                actor = found->second.ref;
                found->second.bound = false;
                found->second.disconnected = true;
                dispatcher = manager._state->disconnect_dispatcher;
            }
        }
        if (dispatcher && !actor.empty ()) {
            try {
                (void) dispatcher (actor);
            }
            catch (...) {
            }
        }
        detail::actor_gateway_runtime_t (manager._state)
          .unbind_session_stream (actor_id, session_id, token);
    }
}

} // namespace zlink::framework

namespace zlink::framework::detail
{

actor_gateway_runtime_t::actor_gateway_runtime_t () :
    _state (std::make_shared<actor_gateway_state_t> ())
{
}

actor_gateway_runtime_t::actor_gateway_runtime_t (std::shared_ptr<actor_gateway_state_t> state) :
    _state (std::move (state))
{
}

session_actor_manager_t actor_gateway_runtime_t::manager () const
{
    return session_actor_manager_t (_state);
}

std::vector<relayed_frame_t> actor_gateway_runtime_t::relayed_frames () const
{
    const std::lock_guard lock (_state->mutex);
    return _state->relayed_frames;
}

std::vector<relayed_frame_t> actor_gateway_runtime_t::bound_session_pushes () const
{
    const std::lock_guard lock (_state->mutex);
    return _state->bound_session_pushes;
}

std::optional<actor_bound_session_route_t>
actor_gateway_runtime_t::bound_session_route (const actor_ref_t &actor_ref) const
{
    const std::lock_guard lock (_state->mutex);
    const auto found = _state->actors_by_id.find (std::string (actor_ref.actor_id ()));
    if (found == _state->actors_by_id.end () || !found->second.bound
        || found->second.ref.actor_type () != actor_ref.actor_type ()
        || found->second.ref.generation () != actor_ref.generation ()) {
        return std::nullopt;
    }
    return found->second.bound_session_route;
}

bool actor_gateway_runtime_t::actor_bound (std::string actor_id) const
{
    const std::lock_guard lock (_state->mutex);
    const auto found = _state->actors_by_id.find (actor_id);
    return found != _state->actors_by_id.end () && found->second.bound;
}

bool actor_gateway_runtime_t::actor_disconnected (std::string actor_id) const
{
    const std::lock_guard lock (_state->mutex);
    const auto found = _state->actors_by_id.find (actor_id);
    return found != _state->actors_by_id.end () && found->second.disconnected;
}

actor_context_t actor_gateway_runtime_t::actor_context (
  const actor_ref_t &actor_ref,
  std::uint64_t source_binding_generation) const
{
    const std::lock_guard lock (_state->mutex);
    const auto found = _state->actors_by_id.find (std::string (actor_ref.actor_id ()));
    if (found == _state->actors_by_id.end ())
        return actor_context_t (_state, actor_ref, source_binding_generation);
    if (source_binding_generation != 0)
        found->second.source_binding_generation = source_binding_generation;
    return actor_context_t (
      _state, actor_ref, found->second.source_binding_generation);
}

result_t<void> actor_gateway_runtime_t::update_actor_ref (const actor_ref_t &actor_ref)
{
    const std::lock_guard lock (_state->mutex);
    if (actor_ref.empty ()) {
        return result_t<void>::failure (framework_error_kind_t::actor_route_not_found,
                                        "actor ref is empty");
    }
    const auto found = _state->actors_by_id.find (std::string (actor_ref.actor_id ()));
    if (found == _state->actors_by_id.end ()) {
        return result_t<void>::success ();
    }
    if (found->second.ref.actor_type () != actor_ref.actor_type ()) {
        return result_t<void>::failure (framework_error_kind_t::actor_type_mismatch,
                                        "actor id is already bound to another type");
    }
    if (found->second.ref.generation () != actor_ref.generation ()) {
        return detail::boundary_failure<void> (detail::boundary_error_t::stale_generation,
                                        "actor generation is stale. actor="
                                          + std::string (actor_ref.actor_id ()) + ", current="
                                          + std::to_string (found->second.ref.generation ())
                                          + ", received="
                                          + std::to_string (actor_ref.generation ()));
    }
    found->second.ref = actor_ref;
    return result_t<void>::success ();
}

result_t<void> actor_gateway_runtime_t::destroy_actor (const actor_ref_t &actor_ref)
{
    const std::lock_guard lock (_state->mutex);
    if (actor_ref.empty ()) {
        return result_t<void>::failure (framework_error_kind_t::actor_route_not_found,
                                        "actor ref is empty");
    }
    const auto found = _state->actors_by_id.find (std::string (actor_ref.actor_id ()));
    if (found == _state->actors_by_id.end ()) {
        return result_t<void>::success ();
    }
    if (found->second.ref.actor_type () != actor_ref.actor_type ()) {
        return result_t<void>::failure (framework_error_kind_t::actor_type_mismatch,
                                        "actor id is already bound to another type");
    }
    if (found->second.ref.generation () != actor_ref.generation ()) {
        return result_t<void>::success ();
    }
    _state->bound_session_sinks.erase (std::string (actor_ref.actor_id ()));
    _state->actors_by_id.erase (found);
    return result_t<void>::success ();
}

void actor_gateway_runtime_t::bind_session_stream (std::string actor_id,
                                                   stream_t stream,
                                                   stream_codec_t codec,
                                                   std::string session_id,
                                                   std::uint64_t binding_token)
{
    actor_ref_t actor_ref;
    actor_gateway_state_t::bound_session_registrar_t registrar;
    {
        const std::lock_guard lock (_state->mutex);
        auto found = _state->actors_by_id.find (actor_id);
        if (found != _state->actors_by_id.end ()) {
            found->second.bound_session_codec = codec;
            found->second.bound_session_stream_sink = true;
            found->second.bound_session_route.reset ();
            actor_ref = found->second.ref;
            found->second.binding_session_id = std::move (session_id);
            found->second.binding_token = binding_token;
        }
        _state->bound_session_sinks[actor_id] = [stream = std::move (stream),
                                                 codec] (std::string packet_name,
                                                         const zlink::message_t &payload) mutable {
            stream_header_t header (stream_message_kind_t::send, codec,
                                    stream_header_flags_t::none, std::nullopt,
                                    std::move (packet_name));
            try {
                stream.write_packet_with_header (std::move (header), payload)
                  .submit ().result ().value ();
                return task_t<void> (result_t<void>::success ());
            }
            catch (const framework_exception_t &error) {
                return task_t<void> (detail::result_access_t::failure<void> (error));
            }
        };
        registrar = _state->bound_session_registrar;
    }
    if (registrar && !actor_ref.empty ()) {
        auto registered = registrar (actor_ref);
        if (!registered) {
            unbind_session_stream (actor_id, {}, binding_token);
            throw framework_exception_t (
              registered.error_kind (), registered.error () ? registered.error ()->what ()
                                                             : "bound session route registration failed");
        }
    }
}

void actor_gateway_runtime_t::bind_session_route (actor_ref_t actor_ref,
                                                  route_client_t route_client,
                                                  std::string route_channel_name,
                                                  zlink::routing_id_t target_node_rid,
                                                  stream_codec_t codec,
                                                  bool replace_existing)
{
    const auto actor_id = std::string (actor_ref.actor_id ());
    bind_session_sink (
      std::move (actor_ref),
      [state = _state, actor_id, route_client = std::move (route_client),
       route_channel_name = std::move (route_channel_name),
       target_node_rid = std::move (target_node_rid)] (std::string packet_name,
                                                       const zlink::message_t &payload) mutable {
          actor_ref_t current_actor_ref;
          {
              const std::lock_guard lock (state->mutex);
              const auto found = state->actors_by_id.find (actor_id);
              if (found == state->actors_by_id.end () || !found->second.bound) {
                  return task_t<void> (result_t<void>::failure (
                    framework_error_kind_t::actor_session_not_bound, "actor session is not bound"));
              }
              current_actor_ref = found->second.ref;
          }
          try {
              route_client
                .send_to_node (route_channel_name, target_node_rid,
                               make_actor_bound_session_route_request (current_actor_ref,
                                                                       packet_name, payload))
                .submit ().result ().value ();
              return task_t<void> (result_t<void>::success ());
          }
          catch (const framework_exception_t &error) {
              return task_t<void> (
                detail::result_access_t::failure<void> (error));
          }
      },
      codec, replace_existing);
}

void actor_gateway_runtime_t::bind_session_sink (
  actor_ref_t actor_ref,
  std::function<task_t<void> (std::string, const zlink::message_t &)> sink,
  stream_codec_t codec,
  bool replace_existing)
{
    const auto actor_id = std::string (actor_ref.actor_id ());
    const std::lock_guard lock (_state->mutex);
    auto found = _state->actors_by_id.find (actor_id);
    if (found == _state->actors_by_id.end ()) {
        _state->actors_by_id.emplace (actor_id, actor_record_t{actor_ref, true, false, codec});
    } else {
        const bool keep_existing_sink =
          found->second.bound && !found->second.disconnected
          && (found->second.bound_session_stream_sink || !replace_existing)
          && _state->bound_session_sinks.find (actor_id) != _state->bound_session_sinks.end ();
        found->second.ref = actor_ref;
        found->second.bound = true;
        found->second.disconnected = false;
        found->second.bound_session_codec = codec;
        if (keep_existing_sink) {
            return;
        }
        found->second.bound_session_stream_sink = false;
    }
    _state->bound_session_sinks[actor_id] = std::move (sink);
}

void actor_gateway_runtime_t::record_bound_session_route (const actor_ref_t &actor_ref,
                                                          zlink::routing_id_t node_rid,
                                                          std::optional<zlink::routing_id_t> session_rid)
{
    const auto actor_id = std::string (actor_ref.actor_id ());
    const std::lock_guard lock (_state->mutex);
    auto found = _state->actors_by_id.find (actor_id);
    if (found == _state->actors_by_id.end ()) {
        found = _state->actors_by_id
                  .emplace (actor_id,
                            actor_record_t{.ref = actor_ref, .bound = true, .disconnected = false})
                  .first;
    } else {
        if (found->second.ref.generation () != actor_ref.generation ())
            return;
        found->second.ref = actor_ref;
        found->second.bound = true;
        found->second.disconnected = false;
    }
    found->second.bound_session_route =
      actor_bound_session_route_t{std::move (node_rid), std::move (session_rid)};
}

void actor_gateway_runtime_t::unbind_session_stream (std::string actor_id,
                                                     std::string session_id,
                                                     std::uint64_t binding_token)
{
    const std::lock_guard lock (_state->mutex);
    auto found = _state->actors_by_id.find (actor_id);
    if (found != _state->actors_by_id.end ()) {
        if (binding_token != 0
            && (found->second.binding_token != binding_token
                || (!session_id.empty ()
                    && found->second.binding_session_id != session_id))) {
            return;
        }
        found->second.bound_session_stream_sink = false;
        found->second.bound_session_route.reset ();
        found->second.binding_session_id.clear ();
        found->second.binding_token = 0;
    }
    _state->bound_session_sinks.erase (actor_id);
}

result_t<void> actor_gateway_runtime_t::dispatch_bound_session_send (
  const actor_ref_t &actor_ref, std::string packet_name, const zlink::message_t &payload) const
{
    std::function<task_t<void> (std::string, const zlink::message_t &)> sink;
    {
        const std::lock_guard lock (_state->mutex);
        const auto actor_id = std::string (actor_ref.actor_id ());
        const auto found = _state->actors_by_id.find (actor_id);
        if (found == _state->actors_by_id.end () || !found->second.bound) {
            return result_t<void>::failure (framework_error_kind_t::actor_session_not_bound,
                                            "actor session is not bound");
        }
        if (found->second.ref.actor_type () != actor_ref.actor_type ()) {
            return result_t<void>::failure (framework_error_kind_t::actor_type_mismatch,
                                            "actor id is already bound to another type");
        }
        if (found->second.ref.generation () != actor_ref.generation ()) {
            return detail::boundary_failure<void> (detail::boundary_error_t::stale_generation,
                                            "actor generation is stale");
        }
        found->second.ref = actor_ref;
        const auto found_sink = _state->bound_session_sinks.find (actor_id);
        if (found_sink == _state->bound_session_sinks.end ()) {
            return result_t<void>::failure (framework_error_kind_t::actor_session_not_bound,
                                            "actor session stream is not bound");
        }
        sink = found_sink->second;
    }
    auto sent = sink (std::move (packet_name), payload).result ();
    if (!sent) {
        return result_t<void>::failure (sent.error_kind (),
                                        sent.error () ? sent.error ()->what ()
                                                      : "actor bound session dispatch failed");
    }
    return result_t<void>::success ();
}

void actor_gateway_runtime_t::on_join_spot (
  actor_gateway_state_t::join_spot_dispatcher_t dispatcher)
{
    const std::lock_guard lock (_state->mutex);
    _state->join_spot_dispatcher = std::move (dispatcher);
}

void actor_gateway_runtime_t::on_create (
  actor_gateway_state_t::create_dispatcher_t dispatcher)
{
    const std::lock_guard lock (_state->mutex);
    _state->create_dispatcher = std::move (dispatcher);
}

void actor_gateway_runtime_t::on_join_entry_spot (
  actor_gateway_state_t::join_entry_spot_dispatcher_t dispatcher)
{
    const std::lock_guard lock (_state->mutex);
    _state->join_entry_spot_dispatcher = std::move (dispatcher);
}

void actor_gateway_runtime_t::on_relay (actor_gateway_state_t::relay_dispatcher_t dispatcher)
{
    const std::lock_guard lock (_state->mutex);
    _state->relay_dispatcher = std::move (dispatcher);
}

void actor_gateway_runtime_t::on_disconnect (
  actor_gateway_state_t::disconnect_dispatcher_t dispatcher)
{
    const std::lock_guard lock (_state->mutex);
    _state->disconnect_dispatcher = std::move (dispatcher);
}

void actor_gateway_runtime_t::on_bound_session (
  actor_gateway_state_t::bound_session_registrar_t registrar)
{
    const std::lock_guard lock (_state->mutex);
    _state->bound_session_registrar = std::move (registrar);
}

void actor_gateway_runtime_t::on_bound_session_send (
  actor_gateway_state_t::bound_session_sender_t sender)
{
    const std::lock_guard lock (_state->mutex);
    _state->bound_session_sender = std::move (sender);
}

void actor_gateway_runtime_t::on_membership (actor_gateway_state_t::membership_query_t query)
{
    const std::lock_guard lock (_state->mutex);
    _state->membership_query = std::move (query);
}

void actor_gateway_runtime_t::bind_serializers (serializer_registry_t &serializers)
{
    const std::lock_guard lock (_state->mutex);
    _state->serializers = &serializers;
}

void actor_gateway_runtime_t::set_dispatch (dispatch_options_t options)
{
    const std::lock_guard lock (_state->mutex);
    _state->dispatch = std::move (options);
}

} // namespace zlink::framework::detail
