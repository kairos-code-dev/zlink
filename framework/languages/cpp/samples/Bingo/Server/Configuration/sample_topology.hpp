/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <zlink/Contracts/Core/routing_id.hpp>
#include <zlink/framework/contracts/configuration/configuration.hpp>

#include <string>

namespace zlink::samples::bingo
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
        topology.play_channel_endpoint =
          section.get ("playChannelEndpoint").value_or (topology.play_channel_endpoint);
        topology.notification_channel_endpoint =
          section.get ("notificationChannelEndpoint")
            .value_or (topology.notification_channel_endpoint);
        topology.play_spot_endpoint =
          section.get ("playSpotEndpoint").value_or (topology.play_spot_endpoint);
        topology.play_spot_router_endpoint =
          section.get ("playSpotRouterEndpoint").value_or (topology.play_spot_router_endpoint);
        topology.session_spot_endpoint =
          section.get ("sessionSpotEndpoint").value_or (topology.session_spot_endpoint);
        topology.session_router_endpoint =
          section.get ("sessionRouterEndpoint").value_or (topology.session_router_endpoint);
        topology.stream_endpoint =
          section.get ("streamEndpoint").value_or (topology.stream_endpoint);
        if (auto value = section.get ("sessionRouterRid")) {
            topology.session_router_rid = zlink::routing_id_t::from (*value);
        }
        if (auto value = section.get ("sessionPubRid")) {
            topology.session_pub_rid = zlink::routing_id_t::from (*value);
        }
        if (auto value = section.get ("playRid")) {
            topology.play_rid = zlink::routing_id_t::from (*value);
        }
        return topology;
    }

    std::string registry_pub_endpoint = "tcp://127.0.0.1:47101";
    std::string registry_router_endpoint = "tcp://127.0.0.1:47102";
    std::string api_channel_endpoint = "tcp://127.0.0.1:47103";
    std::string play_channel_endpoint = "tcp://127.0.0.1:47104";
    std::string notification_channel_endpoint = "tcp://127.0.0.1:47120";
    std::string play_spot_endpoint = "tcp://127.0.0.1:47110";
    std::string play_spot_router_endpoint = "tcp://127.0.0.1:47111";
    std::string session_spot_endpoint = "tcp://127.0.0.1:47112";
    std::string session_router_endpoint = "tcp://127.0.0.1:47113";
    std::string stream_endpoint = "tcp://127.0.0.1:47114";
    zlink::routing_id_t session_router_rid = zlink::routing_id_t::from ("1101");
    zlink::routing_id_t session_pub_rid = zlink::routing_id_t::from ("1102");
    zlink::routing_id_t play_rid = zlink::routing_id_t::from ("2202");
};

} // namespace zlink::samples::bingo
