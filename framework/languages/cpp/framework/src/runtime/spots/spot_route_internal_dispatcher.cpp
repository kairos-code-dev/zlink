/* SPDX-License-Identifier: MPL-2.0 */

#include "runtime/spots/spot_route_internal_dispatcher.hpp"

#include "runtime/messaging/client_call_codec.hpp"
#include "runtime/messaging/envelope_codec.hpp"
#include "runtime/spots/spot_route_packets.hpp"
#include "runtime/streams/stream_runtime.hpp"

#include <zlink.hpp>

#include <memory>
#include <vector>

namespace zlink::framework::detail
{

namespace
{

constexpr std::string_view actor_relay_kind_metadata_key = "__zlink.actorRelayKind";
constexpr std::string_view actor_relay_kind_send = "send";
constexpr std::string_view actor_bind_session_route_metadata_key =
  "__zlink.actorBindSessionRoute";

stream_message_kind_t actor_relay_kind_from_metadata (spot_actor_message_metadata_t &metadata)
{
    auto kind = stream_message_kind_t::request;
    const auto found = metadata.values.find (std::string (actor_relay_kind_metadata_key));
    if (found != metadata.values.end ()) {
        if (found->second == actor_relay_kind_send) {
            kind = stream_message_kind_t::send;
        }
        metadata.values.erase (found);
    }
    return kind;
}

bool should_bind_actor_session_route (spot_actor_message_metadata_t &metadata)
{
    const auto found =
      metadata.values.find (std::string (actor_bind_session_route_metadata_key));
    if (found == metadata.values.end ()) {
        return true;
    }
    const auto bind = found->second != "false";
    metadata.values.erase (found);
    return bind;
}

result_t<zlink::message_t> encode_bound_session_frame (stream_codec_t codec,
                                                       std::string packet_name,
                                                       const zlink::message_t &payload)
{
    stream_runtime_t stream_runtime (std::make_shared<stream_runtime_state_t> ());
    const stream_header_t header (stream_message_kind_t::send, codec,
                                  stream_header_flags_t::none, std::nullopt,
                                  std::move (packet_name));
    auto encoded_header = stream_runtime.encode_header (header);
    if (!encoded_header) {
        return result_t<zlink::message_t>::failure (
          encoded_header.error_kind (),
          encoded_header.error () ? encoded_header.error ()->what ()
                                  : "STREAM header encode failed");
    }
    const auto payload_bytes = payload.to_bytes ();
    const auto header_size = encoded_header.value ().size ();
    std::vector<std::uint8_t> frame;
    frame.reserve (6 + header_size + payload_bytes.size ());
    frame.push_back (static_cast<std::uint8_t> ((header_size >> 8) & 0xff));
    frame.push_back (static_cast<std::uint8_t> (header_size & 0xff));
    frame.push_back (static_cast<std::uint8_t> ((payload_bytes.size () >> 24) & 0xff));
    frame.push_back (static_cast<std::uint8_t> ((payload_bytes.size () >> 16) & 0xff));
    frame.push_back (static_cast<std::uint8_t> ((payload_bytes.size () >> 8) & 0xff));
    frame.push_back (static_cast<std::uint8_t> (payload_bytes.size () & 0xff));
    frame.insert (frame.end (), encoded_header.value ().begin (), encoded_header.value ().end ());
    frame.insert (frame.end (), payload_bytes.begin (), payload_bytes.end ());
    return result_t<zlink::message_t>::success (zlink::message_t::from (std::move (frame)));
}

framework_error_kind_t submit_result_error_kind (zlink::submit_result_t result)
{
    switch (result) {
        case zlink::submit_result_t::not_connected:
            return framework_error_kind_t::disconnected;
        case zlink::submit_result_t::backpressured:
            return framework_error_kind_t::request_failed;
        case zlink::submit_result_t::invalid_argument:
        case zlink::submit_result_t::invalid_handle:
            return framework_error_kind_t::request_protocol_error;
        default:
            return framework_error_kind_t::request_failed;
    }
}

} // namespace

spot_route_internal_dispatcher_t::spot_route_internal_dispatcher_t (
  spot_node_runtime_t runtime,
  actor_gateway_runtime_t actor_gateway,
  route_client_t route_client,
  serializer_registry_t &serializers) :
    _runtime (std::move (runtime)),
    _actor_gateway (std::move (actor_gateway)),
    _route_client (std::move (route_client)),
    _serializers (&serializers)
{
}

bool spot_route_internal_dispatcher_t::can_handle_send (std::string_view packet_name) const
{
    return packet_name == actor_bound_session_route_request_t::packet_name;
}

bool spot_route_internal_dispatcher_t::can_handle_request (std::string_view packet_name) const
{
    return packet_name == actor_bound_session_route_request_t::packet_name
           || packet_name == spot_actor_join_route_request_t::packet_name
           || packet_name == spot_actor_packet_route_request_t::packet_name
           || packet_name == spot_actor_disconnect_route_request_t::packet_name;
}

result_t<void>
spot_route_internal_dispatcher_t::dispatch_send (const route_received_packet_t &received,
                                                 service_provider_t &services) const
{
    (void) received;
    (void) services;
    auto body = runtime::messaging::envelope_codec_t{}.decode_body (received.parts);
    if (!body) {
        return result_t<void>::failure (body.error_kind (), body.error ()
                                                              ? body.error ()->what ()
                                                              : "actor route send body missing");
    }
    try {
        auto request = _serializers->get<actor_bound_session_route_request_t> ().deserialize (
          detail::encoded_payload_from_raw (body.value ()));
        auto actor_ref = actor_ref_from_bound_session_route (request);
        auto actor_gateway = _actor_gateway;
        auto updated = actor_gateway.update_actor_ref (actor_ref);
        if (!updated) {
            return result_t<void>::failure (updated.error_kind (), updated.error ()
                                                                     ? updated.error ()->what ()
                                                                     : "actor ref update failed");
        }
        return actor_gateway.dispatch_bound_session_send (
          actor_ref, request.packet_name_value, zlink::message_t::from (request.payload));
    }
    catch (const framework_exception_t &error) {
        return result_t<void>::failure (error.kind (), error.what (), error.is_retriable ());
    }
    catch (const std::exception &error) {
        return result_t<void>::failure (framework_error_kind_t::request_protocol_error,
                                        std::string ("actor route send decode failed: ")
                                          + error.what ());
    }
}

actor_gateway_runtime_t spot_route_internal_dispatcher_t::bind_actor_route (
  const actor_ref_t &actor_ref,
  const runtime::messaging::envelope_header_t &header,
  const route_received_packet_t &received) const
{
    auto actor_gateway = _actor_gateway;
    auto native = _runtime.native_node ();
    if (native && received.source_session_rid && received.source_session_rid->size () > 0) {
        const auto native_actor = zlink::service::spot_node_t::remote_actor_ref (
          zlink::routing_id_t::from (std::string (actor_ref.node_rid ().value ())),
          std::string (actor_ref.actor_id ()), actor_ref.generation ());
        native->bind_remote_actor_bound_session (
          native_actor, received.source_node_rid, *received.source_session_rid);
        actor_gateway.bind_session_sink (
          actor_ref,
          [native, native_actor] (std::string packet_name,
                                  const zlink::message_t &payload) mutable {
              auto frame =
                encode_bound_session_frame (stream_codec_t::message_pack, std::move (packet_name),
                                            payload);
              if (!frame) {
                  return task_t<void> (result_t<void>::failure (
                    frame.error_kind (),
                    frame.error () ? frame.error ()->what ()
                                   : "actor bound session frame encode failed"));
              }
              try {
                  auto submitted =
                    native->send_bound_session_msg (native_actor)
                      .message (std::move (frame.value ()))
                      .submit ();
                  if (!submitted) {
                      return task_t<void> (result_t<void>::failure (
                        framework_error_kind_t::request_failed,
                        "actor bound session send was backpressured", true));
                  }
                  return task_t<void> (result_t<void>::success ());
              }
              catch (const zlink::submit_error_t &error) {
                  return task_t<void> (result_t<void>::failure (
                    submit_result_error_kind (error.result ()), error.what ()));
              }
              catch (const std::exception &error) {
                  return task_t<void> (
                    result_t<void>::failure (framework_error_kind_t::request_failed, error.what ()));
              }
          },
          stream_codec_t::message_pack, false);
        return actor_gateway;
    }
    if (native) {
        auto *serializers = _serializers;
        actor_gateway.bind_session_sink (
          actor_ref,
          [runtime = _runtime,
           target_node_rid = received.source_node_rid,
           actor_ref,
           serializers] (std::string packet_name, const zlink::message_t &payload) mutable {
              auto native = runtime.native_node ();
              if (!native) {
                  return task_t<void> (result_t<void>::failure (
                    framework_error_kind_t::spot_route_not_found, "SPOT node is not running"));
              }
              try {
                  runtime::messaging::client_call_codec_t codec;
                  auto header = codec.create_envelope (
                    runtime::messaging::message_kind_t::command,
                    std::string (runtime.node_rid ().value ()),
                    actor_bound_session_route_request_t::packet_name);
                  auto current_actor_ref = runtime.current_actor_ref (actor_ref).value_or (actor_ref);
                  auto request = make_actor_bound_session_route_request (
                    current_actor_ref, std::move (packet_name), payload);
                  auto parts = codec.encode_envelope_parts (header, request, *serializers).items ();
                  if (parts.empty ()) {
                      return task_t<void> (result_t<void>::failure (
                        framework_error_kind_t::request_protocol_error,
                        "actor bound session route requires message parts"));
                  }
                  auto origin_spot =
                    native
                      ->get_or_create_spot (zlink::routing_id_t::from (
                        std::string (runtime.node_rid ().value ())
                        + ":__zlink-bound-session-route-origin"))
                      .first;
                  auto iterator = parts.begin ();
                  auto submit =
                    origin_spot
                      .send_to_spot (target_node_rid,
                                     zlink::routing_id_t::from (target_node_rid.to_string ()))
                      .message (*iterator);
                  ++iterator;
                  for (; iterator != parts.end (); ++iterator) {
                      submit = std::move (submit).message (*iterator);
                  }
                  if (!std::move (submit).submit ()) {
                      return task_t<void> (result_t<void>::failure (
                        framework_error_kind_t::request_failed,
                        "actor bound session route was not submitted"));
                  }
                  return task_t<void> (result_t<void>::success ());
              }
              catch (const framework_exception_t &error) {
                  return task_t<void> (
                    result_t<void>::failure (error.kind (), error.what (), error.is_retriable ()));
              }
              catch (const std::exception &error) {
                  return task_t<void> (
                    result_t<void>::failure (framework_error_kind_t::request_failed, error.what ()));
              }
          },
          stream_codec_t::message_pack, false);
        return actor_gateway;
    }
    const auto route_channel_name =
      _runtime.default_route_channel_name ().value_or (header.channel_name);
    actor_gateway.bind_session_route (actor_ref, _route_client, route_channel_name,
                                      received.source_node_rid, stream_codec_t::message_pack,
                                      false);
    return actor_gateway;
}

result_t<zlink::message_t> spot_route_internal_dispatcher_t::dispatch_request (
  const route_received_packet_t &received,
  const runtime::messaging::envelope_header_t &header,
  service_provider_t &services) const
{
    (void) header;
    auto body = runtime::messaging::envelope_codec_t{}.decode_body (received.parts);
    if (!body) {
        return result_t<zlink::message_t>::failure (
          body.error_kind (),
          body.error () ? body.error ()->what () : "SPOT route request body missing");
    }

    try {
        if (header.message_name == actor_bound_session_route_request_t::packet_name) {
            auto request = _serializers->get<actor_bound_session_route_request_t> ().deserialize (
              detail::encoded_payload_from_raw (body.value ()));
            auto actor_ref = actor_ref_from_bound_session_route (request);
            auto actor_gateway = _actor_gateway;
            auto updated = actor_gateway.update_actor_ref (actor_ref);
            if (!updated) {
                return result_t<zlink::message_t>::failure (
                  updated.error_kind (),
                  updated.error () ? updated.error ()->what () : "actor ref update failed");
            }
            auto dispatched = actor_gateway.dispatch_bound_session_send (
              actor_ref, request.packet_name_value, zlink::message_t::from (request.payload));
            if (!dispatched) {
                return result_t<zlink::message_t>::failure (
                  dispatched.error_kind (), dispatched.error ()
                                             ? dispatched.error ()->what ()
                                             : "routed actor bound session send failed");
            }
            return result_t<zlink::message_t>::success (detail::encoded_payload_to_raw (
              _serializers->get<actor_bound_session_route_reply_t> ().serialize (
                actor_bound_session_route_reply_t{.accepted = true})));
        }
        if (header.message_name == spot_actor_packet_route_request_t::packet_name) {
            auto request = _serializers->get<spot_actor_packet_route_request_t> ().deserialize (
              detail::encoded_payload_from_raw (body.value ()));
            auto runtime = _runtime;
            auto actor_ref = actor_ref_from_spot_route (request);
            spot_actor_message_metadata_t metadata;
            metadata.content_type = request.content_type;
            metadata.values = request.metadata;
            const auto message_kind = actor_relay_kind_from_metadata (metadata);
            auto actor_gateway =
              should_bind_actor_session_route (metadata)
                ? bind_actor_route (actor_ref, header, received)
                : _actor_gateway;
            auto relayed = runtime.manager ().relay_actor_packet (
              actor_ref, actor_gateway.actor_context (actor_ref), message_kind,
              request.packet_name_value, zlink::message_t::from (request.payload), services,
              *_serializers, std::move (metadata));
            if (!relayed) {
                return result_t<zlink::message_t>::failure (
                  relayed.error_kind (),
                  relayed.error () ? relayed.error ()->what () : "remote actor packet failed");
            }
            auto current_actor_ref = runtime.current_actor_ref (actor_ref).value_or (actor_ref);
            (void) actor_gateway.update_actor_ref (current_actor_ref);
            auto reply = spot_actor_packet_route_reply_t{
              .actor_ref_present = true,
              .actor_node_rid = std::string (current_actor_ref.node_rid ().value ()),
              .actor_type = std::string (current_actor_ref.actor_type ()),
              .actor_id = std::string (current_actor_ref.actor_id ()),
              .actor_generation = current_actor_ref.generation (),
              .has_reply = relayed.value ().has_value (),
              .payload =
                relayed.value () ? relayed.value ()->to_bytes () : std::vector<std::uint8_t>{}};
            return result_t<zlink::message_t>::success (detail::encoded_payload_to_raw (
              _serializers->get<spot_actor_packet_route_reply_t> ().serialize (reply)));
        }
        if (header.message_name == spot_actor_disconnect_route_request_t::packet_name) {
            auto request = _serializers->get<spot_actor_disconnect_route_request_t> ().deserialize (
              detail::encoded_payload_from_raw (body.value ()));
            auto disconnected =
              _runtime.notify_actor_disconnected_erased (actor_ref_from_spot_route (request));
            if (!disconnected) {
                return result_t<zlink::message_t>::failure (
                  disconnected.error_kind (), disconnected.error ()
                                                ? disconnected.error ()->what ()
                                                : "remote actor disconnect notify failed");
            }
            return result_t<zlink::message_t>::success (detail::encoded_payload_to_raw (
              _serializers->get<spot_actor_disconnect_route_reply_t> ().serialize (
                spot_actor_disconnect_route_reply_t{})));
        }
        auto request = _serializers->get<spot_actor_join_route_request_t> ().deserialize (
          detail::encoded_payload_from_raw (body.value ()));
        auto runtime = _runtime;
        auto actor_ref = actor_ref_from_spot_route (request);
        auto actor_gateway = bind_actor_route (actor_ref, header, received);
        auto joined =
          request.spot_rid.empty ()
            ? runtime.join_actor_to_entry_spot_erased (
                actor_ref, actor_ref.node_rid (), zlink::message_t::from (request.payload),
                request.actor_snapshot_present
                  ? std::make_optional (zlink::message_t::from (request.actor_snapshot))
                  : std::nullopt)
            : runtime.join_remote_actor_to_spot_erased (
                actor_ref, spot_rid_t::from_string (request.spot_rid),
                zlink::message_t::from (request.payload), actor_gateway.actor_context (actor_ref));
        if (!joined) {
            return result_t<zlink::message_t>::failure (
              joined.error_kind (),
              joined.error () ? joined.error ()->what () : "remote actor join failed");
        }
        (void) actor_gateway.update_actor_ref (joined.value ().actor);
        auto reply = make_spot_actor_join_route_reply (joined.value ());
        return result_t<zlink::message_t>::success (detail::encoded_payload_to_raw (
          _serializers->get<spot_actor_join_route_reply_t> ().serialize (reply)));
    }
    catch (const framework_exception_t &error) {
        return result_t<zlink::message_t>::failure (error.kind (), error.what (),
                                                    error.is_retriable ());
    }
    catch (const std::exception &error) {
        return result_t<zlink::message_t>::failure (
          framework_error_kind_t::request_protocol_error,
          std::string ("SPOT route request decode failed: ") + error.what ());
    }
}

} // namespace zlink::framework::detail
