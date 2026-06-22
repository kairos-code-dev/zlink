/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <zlink/Contracts/Core/routing_id.hpp>
#include <zlink/framework/contracts/configuration/configuration.hpp>

#include <string>

namespace zlink::samples::supportchat
{

struct sample_topology_t
{
    static sample_topology_t bind (const zlink::framework::configuration_section_t &section)
    {
        sample_topology_t topology;
        topology.registry_pub_endpoint =
          section.get ("registryPubEndpoint").value_or (topology.registry_pub_endpoint);
        topology.registry_router_endpoint =
          section.get ("registryRouterEndpoint").value_or (topology.registry_router_endpoint);
        topology.api_channel_endpoint =
          section.get ("apiChannelEndpoint").value_or (topology.api_channel_endpoint);
        topology.support_channel_endpoint =
          section.get ("supportChannelEndpoint").value_or (topology.support_channel_endpoint);
        topology.notification_channel_endpoint =
          section.get ("notificationChannelEndpoint")
            .value_or (topology.notification_channel_endpoint);
        topology.session_spot_endpoint =
          section.get ("sessionSpotEndpoint").value_or (topology.session_spot_endpoint);
        topology.session_router_endpoint =
          section.get ("sessionRouterEndpoint").value_or (topology.session_router_endpoint);
        topology.session_actor_route_endpoint =
          section.get ("sessionActorRouteEndpoint")
            .value_or (topology.session_actor_route_endpoint);
        topology.support_router_endpoint =
          section.get ("supportRouterEndpoint").value_or (topology.support_router_endpoint);
        topology.support_actor_route_endpoint =
          section.get ("supportActorRouteEndpoint")
            .value_or (topology.support_actor_route_endpoint);
        topology.support_spot_endpoint =
          section.get ("supportSpotEndpoint").value_or (topology.support_spot_endpoint);
        topology.conversation_spot_router_endpoint =
          section.get ("conversationSpotRouterEndpoint")
            .value_or (topology.conversation_spot_router_endpoint);
        topology.conversation_spot_endpoint =
          section.get ("conversationSpotEndpoint").value_or (topology.conversation_spot_endpoint);
        topology.stream_endpoint =
          section.get ("streamEndpoint").value_or (topology.stream_endpoint);
        if (auto value = section.get ("sessionRouterRid")) {
            topology.session_router_rid = zlink::routing_id_t::from (*value);
        }
        if (auto value = section.get ("sessionPubRid")) {
            topology.session_pub_rid = zlink::routing_id_t::from (*value);
        }
        if (auto value = section.get ("supportEntryRid")) {
            topology.support_entry_rid = zlink::routing_id_t::from (*value);
        }
        return topology;
    }

    std::string registry_pub_endpoint = "tcp://127.0.0.1:47201";
    std::string registry_router_endpoint = "tcp://127.0.0.1:47202";
    std::string api_channel_endpoint = "tcp://127.0.0.1:47203";
    std::string support_channel_endpoint = "tcp://127.0.0.1:47204";
    std::string session_spot_endpoint = "tcp://127.0.0.1:47205";
    std::string session_router_endpoint = "tcp://127.0.0.1:47206";
    std::string session_actor_route_endpoint = "tcp://127.0.0.1:47213";
    std::string support_router_endpoint = "tcp://127.0.0.1:47207";
    std::string support_actor_route_endpoint = "tcp://127.0.0.1:47214";
    std::string support_spot_endpoint = "tcp://127.0.0.1:47208";
    std::string conversation_spot_router_endpoint = "tcp://127.0.0.1:47210";
    std::string conversation_spot_endpoint = "tcp://127.0.0.1:47211";
    std::string stream_endpoint = "tcp://127.0.0.1:47212";
    std::string notification_channel_endpoint = "tcp://127.0.0.1:47220";
    zlink::routing_id_t session_router_rid = zlink::routing_id_t::from ("3101");
    zlink::routing_id_t session_pub_rid = zlink::routing_id_t::from ("3102");
    zlink::routing_id_t support_entry_rid = zlink::routing_id_t::from ("4201");
};

} // namespace zlink::samples::supportchat
