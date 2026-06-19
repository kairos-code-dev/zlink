/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <zlink/Contracts/Messaging/message.hpp>
#include <zlink/framework/contracts/errors/result.hpp>
#include <zlink/framework/contracts/actors/actor.hpp>
#include <zlink/framework/contracts/codecs/serializer.hpp>
#include <zlink/framework/contracts/configuration/services.hpp>
#include <zlink/framework/contracts/spots/spot.hpp>

namespace zlink::framework::extensions
{

template <typename TActor, typename TRequest, typename TReply>
class remote_actor_packet_handler_t
{
  public:
    using request_type = TRequest;
    using reply_type = TReply;

    using dependency_types =
      dependency_list_t<spot_node_manager_t, actor_gateway_t, serializer_registry_t>;

    explicit remote_actor_packet_handler_t (
      spot_node_manager_t &spots,
      actor_gateway_t &gateway,
      serializer_registry_t &serializers) :
        _spots (spots), _gateway (gateway), _serializers (serializers)
    {
    }

    task_t<TReply> handle (const TRequest &request)
    {
        auto actor_ref = actor_ref_t (
          node_rid_t::from_string (request.actor_node_rid),
          request.actor_type, request.actor_id, request.actor_generation);
        auto reply = _spots.relay_actor_packet (
          actor_ref,
          _gateway.actor_context (actor_ref),
          request.relayed_packet_name,
          zlink::message_t::from (request.payload),
          _provider,
          _serializers,
          spot_actor_message_metadata_t{request.metadata});
        if (!reply) {
            co_return result_t<TReply>::failure (
              reply.error_kind (),
              reply.error () ? reply.error ()->what () : "remote actor relay failed");
        }

        TReply response;
        response.actor_ref_present = true;
        response.actor_node_rid = std::string (actor_ref.node_rid ().value ());
        response.actor_type = std::string (actor_ref.actor_type ());
        response.actor_id = std::string (actor_ref.actor_id ());
        response.actor_generation = actor_ref.generation ();
        if (reply.value ()) {
            response.has_reply = true;
            response.reply_payload = reply.value ()->to_bytes ();
        }
        co_return result_t<TReply>::success (std::move (response));
    }

  private:
    spot_node_manager_t &_spots;
    actor_gateway_t &_gateway;
    serializer_registry_t &_serializers;
    service_provider_t _provider;
};

} // namespace zlink::framework::extensions
