/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <zlink/Contracts/Messaging/message.hpp>
#include <zlink/framework/contracts/errors/result.hpp>
#include <zlink/framework/contracts/actors/actor.hpp>
#include <zlink/framework/contracts/codecs/serializer.hpp>
#include <zlink/framework/contracts/configuration/services.hpp>
#include <zlink/framework/contracts/spots/spot.hpp>

#include "runtime/actors/actor_gateway_runtime.hpp"
#include "runtime/spots/spot_runtime.hpp"

namespace zlink::framework::extensions
{

template <typename TActor, typename TRequest, typename TReply>
class remote_actor_packet_handler_t
{
  public:
    using request_type = TRequest;
    using reply_type = TReply;

    using dependency_types =
      dependency_list_t<detail::spot_node_runtime_t,
                        detail::actor_gateway_runtime_t,
                        serializer_registry_t>;

    explicit remote_actor_packet_handler_t (
      detail::spot_node_runtime_t &spots,
      detail::actor_gateway_runtime_t &gateway,
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
        if (auto actor = _spots.actor_instance<TActor> (actor_ref);
            actor && !actor->get ().context.actor_ref ().empty ()) {
            const auto &updated = actor->get ().context.actor_ref ();
            response.actor_ref_present = true;
            response.actor_node_rid = std::string (updated.node_rid ().value ());
            response.actor_type = std::string (updated.actor_type ());
            response.actor_id = std::string (updated.actor_id ());
            response.actor_generation = updated.generation ();
        }
        if (reply.value ()) {
            response.has_reply = true;
            response.reply_payload = reply.value ()->to_bytes ();
        }
        co_return result_t<TReply>::success (std::move (response));
    }

  private:
    detail::spot_node_runtime_t &_spots;
    detail::actor_gateway_runtime_t &_gateway;
    serializer_registry_t &_serializers;
    service_provider_t _provider;
};

} // namespace zlink::framework::extensions
