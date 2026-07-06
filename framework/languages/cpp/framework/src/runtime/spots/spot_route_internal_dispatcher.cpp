/* SPDX-License-Identifier: MPL-2.0 */

#include "runtime/spots/spot_route_internal_dispatcher.hpp"

#include "runtime/messaging/client_call_codec.hpp"
#include "runtime/messaging/envelope_codec.hpp"
#include "runtime/spots/spot_route_packets.hpp"

#include <zlink.hpp>

namespace zlink::framework::detail
{

namespace
{

constexpr std::string_view actor_relay_kind_metadata_key = "__zlink.actorRelayKind";
constexpr std::string_view actor_relay_kind_send = "send";

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
    return packet_name == spot_actor_join_route_request_t::packet_name
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
    actor_gateway.bind_session_route (actor_ref, _route_client, header.channel_name,
                                      received.source_node_rid, stream_codec_t::message_pack);
    if (_runtime.native_node ()) {
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
          stream_codec_t::message_pack);
    }
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
        if (header.message_name == spot_actor_packet_route_request_t::packet_name) {
            auto request = _serializers->get<spot_actor_packet_route_request_t> ().deserialize (
              detail::encoded_payload_from_raw (body.value ()));
            auto runtime = _runtime;
            auto actor_ref = actor_ref_from_spot_route (request);
            auto actor_gateway = bind_actor_route (actor_ref, header, received);
            spot_actor_message_metadata_t metadata;
            metadata.content_type = request.content_type;
            metadata.values = request.metadata;
            const auto message_kind = actor_relay_kind_from_metadata (metadata);
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
