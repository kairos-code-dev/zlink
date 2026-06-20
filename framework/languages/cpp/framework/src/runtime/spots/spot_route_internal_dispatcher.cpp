/* SPDX-License-Identifier: MPL-2.0 */

#include "runtime/spots/spot_route_internal_dispatcher.hpp"

#include "runtime/messaging/envelope_codec.hpp"
#include "runtime/spots/spot_route_packets.hpp"

namespace zlink::framework::detail
{

spot_route_internal_dispatcher_t::spot_route_internal_dispatcher_t (
  spot_node_runtime_t runtime,
  actor_gateway_runtime_t actor_gateway,
  route_client_t route_client,
  serializer_registry_t &serializers) :
    _runtime (std::move (runtime)), _actor_gateway (std::move (actor_gateway)),
    _route_client (std::move (route_client)), _serializers (&serializers)
{
}

bool spot_route_internal_dispatcher_t::can_handle_send (std::string_view packet_name) const
{
    (void) packet_name;
    return false;
}

bool spot_route_internal_dispatcher_t::can_handle_request (std::string_view packet_name) const
{
    return packet_name == spot_actor_join_route_request_t::packet_name
           || packet_name == spot_actor_packet_route_request_t::packet_name;
}

result_t<void>
spot_route_internal_dispatcher_t::dispatch_send (const route_received_packet_t &received) const
{
    (void) received;
    return result_t<void>::failure (framework_error_kind_t::route_handler_not_found,
                                    "SPOT route internal send is not supported");
}

result_t<zlink::message_t> spot_route_internal_dispatcher_t::dispatch_request (
  const route_received_packet_t &received,
  const runtime::messaging::envelope_header_t &header) const
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
              body.value ());
            auto runtime = _runtime;
            auto actor_ref = actor_ref_from_spot_route (request);
            auto actor_gateway = _actor_gateway;
            actor_gateway.bind_session_route (actor_ref, _route_client, header.channel_name,
                                              received.source_node_rid,
                                              stream_codec_t::message_pack);
            spot_actor_message_metadata_t metadata;
            metadata.values = request.metadata;
            service_collection_t services;
            auto provider = services.build_provider ();
            auto relayed = runtime.manager ().relay_actor_packet (
              actor_ref, _actor_gateway.actor_context (actor_ref), request.packet_name_value,
              zlink::message_t::from (request.payload), provider, *_serializers, std::move (metadata));
            if (!relayed) {
                return result_t<zlink::message_t>::failure (
                  relayed.error_kind (),
                  relayed.error () ? relayed.error ()->what () : "remote actor packet failed");
            }
            auto current_actor_ref = runtime.current_actor_ref (actor_ref).value_or (actor_ref);
            auto reply = spot_actor_packet_route_reply_t{
              .actor_ref_present = true,
              .actor_node_rid = std::string (current_actor_ref.node_rid ().value ()),
              .actor_type = std::string (current_actor_ref.actor_type ()),
              .actor_id = std::string (current_actor_ref.actor_id ()),
              .actor_generation = current_actor_ref.generation (),
              .has_reply = relayed.value ().has_value (),
              .payload = relayed.value () ? relayed.value ()->to_bytes ()
                                          : std::vector<std::uint8_t>{}};
            return result_t<zlink::message_t>::success (
              _serializers->get<spot_actor_packet_route_reply_t> ().serialize (reply));
        }
        auto request = _serializers->get<spot_actor_join_route_request_t> ().deserialize (
          body.value ());
        auto runtime = _runtime;
        auto actor_ref = actor_ref_from_spot_route (request);
        auto actor_gateway = _actor_gateway;
        actor_gateway.bind_session_route (actor_ref, _route_client, header.channel_name,
                                          received.source_node_rid,
                                          stream_codec_t::message_pack);
        auto joined = runtime.join_remote_actor_to_spot_erased (
          actor_ref, spot_rid_t::from_string (request.spot_rid),
          zlink::message_t::from (request.payload), _actor_gateway.actor_context (actor_ref));
        if (!joined) {
            return result_t<zlink::message_t>::failure (
              joined.error_kind (),
              joined.error () ? joined.error ()->what () : "remote actor join failed");
        }
        auto reply = make_spot_actor_join_route_reply (joined.value ());
        return result_t<zlink::message_t>::success (
          _serializers->get<spot_actor_join_route_reply_t> ().serialize (reply));
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
