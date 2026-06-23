/* SPDX-License-Identifier: MPL-2.0 */

#include "runtime/actors/actor_route_internal_dispatcher.hpp"

#include "runtime/messaging/envelope_codec.hpp"
#include "runtime/spots/spot_route_packets.hpp"

namespace zlink::framework::detail
{

actor_route_internal_dispatcher_t::actor_route_internal_dispatcher_t (
  actor_gateway_runtime_t runtime,
  serializer_registry_t &serializers) :
    _runtime (std::move (runtime)), _serializers (&serializers)
{
}

bool actor_route_internal_dispatcher_t::can_handle_send (std::string_view packet_name) const
{
    (void) packet_name;
    return false;
}

bool actor_route_internal_dispatcher_t::can_handle_request (std::string_view packet_name) const
{
    return packet_name == actor_bound_session_route_request_t::packet_name;
}

result_t<void>
actor_route_internal_dispatcher_t::dispatch_send (const route_received_packet_t &received,
                                                  service_provider_t &services) const
{
    (void) received;
    (void) services;
    return result_t<void>::failure (framework_error_kind_t::route_handler_not_found,
                                    "actor route internal send is not supported");
}

result_t<zlink::message_t> actor_route_internal_dispatcher_t::dispatch_request (
  const route_received_packet_t &received,
  const runtime::messaging::envelope_header_t &header,
  service_provider_t &services) const
{
    (void) header;
    (void) services;
    auto body = runtime::messaging::envelope_codec_t{}.decode_body (received.parts);
    if (!body) {
        return result_t<zlink::message_t>::failure (
          body.error_kind (),
          body.error () ? body.error ()->what () : "actor route request body missing");
    }

    try {
        auto request =
          _serializers->get<actor_bound_session_route_request_t> ().deserialize (body.value ());
        auto dispatched = _runtime.dispatch_bound_session_send (
          actor_ref_from_bound_session_route (request), request.packet_name_value,
          zlink::message_t::from (request.payload));
        if (!dispatched) {
            return result_t<zlink::message_t>::failure (
              dispatched.error_kind (),
              dispatched.error () ? dispatched.error ()->what ()
                                  : "routed actor bound session send failed");
        }
        return result_t<zlink::message_t>::success (
          _serializers->get<actor_bound_session_route_reply_t> ().serialize (
            actor_bound_session_route_reply_t{.accepted = true}));
    }
    catch (const framework_exception_t &error) {
        return result_t<zlink::message_t>::failure (error.kind (), error.what (),
                                                    error.is_retriable ());
    }
    catch (const std::exception &error) {
        return result_t<zlink::message_t>::failure (
          framework_error_kind_t::request_protocol_error,
          std::string ("actor route request decode failed: ") + error.what ());
    }
}

} // namespace zlink::framework::detail
