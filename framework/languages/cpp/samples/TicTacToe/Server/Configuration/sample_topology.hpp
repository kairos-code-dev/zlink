/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <zlink/Contracts/Core/routing_id.hpp>
#include <zlink/framework/contracts/configuration/configuration.hpp>

#include <string>

namespace zlink::samples::tictactoe
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
        topology.api_endpoint = section.get ("apiEndpoint").value_or (topology.api_endpoint);
        topology.api_http_endpoint =
          section.get ("apiHttpEndpoint").value_or (topology.api_http_endpoint);
        topology.play_endpoint = section.get ("playEndpoint").value_or (topology.play_endpoint);
        topology.session_spot_endpoint =
          section.get ("sessionSpotEndpoint").value_or (topology.session_spot_endpoint);
        topology.session_router_endpoint =
          section.get ("sessionRouterEndpoint").value_or (topology.session_router_endpoint);
        topology.play_router_endpoint =
          section.get ("playRouterEndpoint").value_or (topology.play_router_endpoint);
        topology.play_spot_endpoint =
          section.get ("playSpotEndpoint").value_or (topology.play_spot_endpoint);
        topology.play_spot_router_endpoint =
          section.get ("playSpotRouterEndpoint").value_or (topology.play_spot_router_endpoint);
        topology.stream_endpoint =
          section.get ("streamEndpoint").value_or (topology.stream_endpoint);
        if (auto value = section.get ("sessionRid")) {
            topology.session_rid = zlink::routing_id_t::from (*value);
        }
        if (auto value = section.get ("playRid")) {
            topology.play_rid = zlink::routing_id_t::from (*value);
        }
        return topology;
    }

    std::string registry_pub_endpoint = "tcp://127.0.0.1:48101";
    std::string registry_router_endpoint = "tcp://127.0.0.1:48102";
    std::string api_endpoint = "tcp://127.0.0.1:48103";
    std::string api_http_endpoint = "http://127.0.0.1:48113";
    std::string play_endpoint = "tcp://127.0.0.1:48104";
    std::string session_spot_endpoint = "tcp://127.0.0.1:48105";
    std::string session_router_endpoint = "tcp://127.0.0.1:48106";
    std::string play_router_endpoint = "tcp://127.0.0.1:48109";
    std::string play_spot_endpoint = "tcp://127.0.0.1:48110";
    std::string play_spot_router_endpoint = "tcp://127.0.0.1:48111";
    std::string stream_endpoint = "tcp://127.0.0.1:48112";
    zlink::routing_id_t session_rid = zlink::routing_id_t::from ("1101");
    zlink::routing_id_t play_rid = zlink::routing_id_t::from ("2202");
};

} // namespace zlink::samples::tictactoe
