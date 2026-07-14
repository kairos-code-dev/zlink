/* SPDX-License-Identifier: FSL-1.1-ALv2 */
#pragma once

#include <zlink/Contracts/Core/routing_id.hpp>
#include <zlink/framework/contracts/configuration/configuration.hpp>

#include <stdexcept>
#include <string>

namespace zlink::samples::bingo
{

using namespace framework;

struct sample_topology_t
{
    static sample_topology_t bind (const configuration_section_t &section)
    {
        sample_topology_t topology;
        topology.api_channel_endpoint =
          section.get ("apiChannelEndpoint").value_or (topology.api_channel_endpoint);
        topology.api_a_channel_endpoint =
          section.get ("apiAChannelEndpoint").value_or (topology.api_a_channel_endpoint);
        topology.api_b_channel_endpoint =
          section.get ("apiBChannelEndpoint").value_or (topology.api_b_channel_endpoint);
        topology.play_channel_endpoint =
          section.get ("playChannelEndpoint").value_or (topology.play_channel_endpoint);
        topology.play_a_channel_endpoint =
          section.get ("playAChannelEndpoint").value_or (topology.play_a_channel_endpoint);
        topology.play_b_channel_endpoint =
          section.get ("playBChannelEndpoint").value_or (topology.play_b_channel_endpoint);
        topology.play_a_route_endpoint =
          section.get ("playARouteEndpoint").value_or (topology.play_a_route_endpoint);
        topology.play_b_route_endpoint =
          section.get ("playBRouteEndpoint").value_or (topology.play_b_route_endpoint);
        topology.api_a_play_route_endpoint =
          section.get ("apiAPlayRouteEndpoint").value_or (topology.api_a_play_route_endpoint);
        topology.api_b_play_route_endpoint =
          section.get ("apiBPlayRouteEndpoint").value_or (topology.api_b_play_route_endpoint);
        topology.session_a_route_endpoint =
          section.get ("sessionAPlayRouteEndpoint").value_or (topology.session_a_route_endpoint);
        topology.session_b_route_endpoint =
          section.get ("sessionBPlayRouteEndpoint").value_or (topology.session_b_route_endpoint);
        topology.play_a_spot_endpoint =
          section.get ("playASpotEndpoint").value_or (topology.play_a_spot_endpoint);
        topology.play_b_spot_endpoint =
          section.get ("playBSpotEndpoint").value_or (topology.play_b_spot_endpoint);
        topology.play_a_spot_router_endpoint =
          section.get ("playASpotRouterEndpoint").value_or (topology.play_a_spot_router_endpoint);
        topology.play_b_spot_router_endpoint =
          section.get ("playBSpotRouterEndpoint").value_or (topology.play_b_spot_router_endpoint);
        topology.play_spot_endpoint =
          section.get ("playSpotEndpoint").value_or (topology.play_spot_endpoint);
        topology.play_spot_router_endpoint =
          section.get ("playSpotRouterEndpoint").value_or (topology.play_spot_router_endpoint);
        topology.session_spot_endpoint =
          section.get ("sessionSpotEndpoint").value_or (topology.session_spot_endpoint);
        topology.session_router_endpoint =
          section.get ("sessionRouterEndpoint").value_or (topology.session_router_endpoint);
        topology.log_dir = section.require ("logDir");
        topology.stream_endpoint =
          section.get ("streamEndpoint").value_or (topology.stream_endpoint);
        topology.session_a_stream_endpoint =
          section.get ("sessionAStreamEndpoint").value_or (topology.session_a_stream_endpoint);
        topology.session_b_stream_endpoint =
          section.get ("sessionBStreamEndpoint").value_or (topology.session_b_stream_endpoint);
        topology.api_node = section.get ("apiNode").value_or (topology.api_node);
        topology.play_node = section.get ("playNode").value_or (topology.play_node);
        topology.session_node = section.get ("sessionNode").value_or (topology.session_node);
        // The sample owns its Redis via run_sample.sh/run_sample.ps1, which
        // provisions an isolated container; require the endpoint so a stray
        // direct run never silently falls back to a developer's local Redis.
        if (auto value = section.get ("redisEndpoint")) {
            topology.redis_endpoint = *value;
        } else {
            throw std::runtime_error (
              "redisEndpoint is required; run the sample via run_sample.sh/run_sample.ps1, "
              "which provisions an isolated Redis container.");
        }
        topology.redis_key_prefix =
          section.get ("redisKeyPrefix").value_or (topology.redis_key_prefix);
        topology.session_a_router_rid =
          section.get ("sessionARouterRid").value_or (topology.session_a_router_rid);
        topology.session_b_router_rid =
          section.get ("sessionBRouterRid").value_or (topology.session_b_router_rid);
        if (auto value = section.get ("sessionRouterRid")) {
            topology.session_a_router_rid = *value;
        }
        topology.session_a_spot_node_rid =
          section.get ("sessionASpotNodeRid").value_or (topology.session_a_spot_node_rid);
        topology.session_b_spot_node_rid =
          section.get ("sessionBSpotNodeRid").value_or (topology.session_b_spot_node_rid);
        topology.session_a_pub_rid =
          section.get ("sessionAPubRid").value_or (topology.session_a_pub_rid);
        topology.session_b_pub_rid =
          section.get ("sessionBPubRid").value_or (topology.session_b_pub_rid);
        if (auto value = section.get ("sessionPubRid")) {
            topology.session_a_pub_rid = *value;
        }
        if (auto value = section.get ("playRid")) {
            topology.play_rid = zlink::routing_id_t::from (*value);
        }
        topology.play_a_node_rid = section.get ("playANodeRid").value_or (topology.play_a_node_rid);
        topology.play_b_node_rid = section.get ("playBNodeRid").value_or (topology.play_b_node_rid);
        topology.api_a_route_rid =
          section.get ("apiAPlayRouteRid").value_or (topology.api_a_route_rid);
        topology.api_b_route_rid =
          section.get ("apiBPlayRouteRid").value_or (topology.api_b_route_rid);
        topology.session_a_route_rid =
          section.get ("sessionAPlayRouteRid").value_or (topology.session_a_route_rid);
        topology.session_b_route_rid =
          section.get ("sessionBPlayRouteRid").value_or (topology.session_b_route_rid);
        return topology;
    }

    std::string api_channel_endpoint = "tcp://127.0.0.1:47103";
    std::string api_a_channel_endpoint = "tcp://127.0.0.1:47103";
    std::string api_b_channel_endpoint = "tcp://127.0.0.1:47117";
    std::string play_channel_endpoint = "tcp://127.0.0.1:47104";
    std::string play_a_channel_endpoint = "tcp://127.0.0.1:47104";
    std::string play_b_channel_endpoint = "tcp://127.0.0.1:47114";
    std::string play_a_route_endpoint = "tcp://127.0.0.1:47118";
    std::string play_b_route_endpoint = "tcp://127.0.0.1:47119";
    std::string api_a_play_route_endpoint = "tcp://127.0.0.1:47120";
    std::string api_b_play_route_endpoint = "tcp://127.0.0.1:47123";
    std::string session_a_route_endpoint = "tcp://127.0.0.1:47121";
    std::string session_b_route_endpoint = "tcp://127.0.0.1:47122";
    std::string play_spot_endpoint = "tcp://127.0.0.1:47110";
    std::string play_spot_router_endpoint = "tcp://127.0.0.1:47111";
    std::string play_a_spot_endpoint = "tcp://127.0.0.1:47110";
    std::string play_b_spot_endpoint = "tcp://127.0.0.1:47115";
    std::string play_a_spot_router_endpoint = "tcp://127.0.0.1:47111";
    std::string play_b_spot_router_endpoint = "tcp://127.0.0.1:47116";
    std::string session_spot_endpoint = "tcp://127.0.0.1:47112";
    std::string session_router_endpoint = "tcp://127.0.0.1:47113";
    std::string log_dir = "logs";
    std::string stream_endpoint = "tcp://127.0.0.1:47114";
    std::string session_a_stream_endpoint = "tcp://127.0.0.1:47114";
    std::string session_b_stream_endpoint = "tcp://127.0.0.1:47117";
    std::string api_node = "a";
    std::string play_node = "a";
    std::string session_node = "a";
    std::string play_a_node_rid = "2201";
    std::string play_b_node_rid = "2202";
    std::string session_a_spot_node_rid = "1201-spot";
    std::string session_b_spot_node_rid = "1202-spot";
    std::string api_a_route_rid = "3301";
    std::string api_b_route_rid = "3302";
    std::string session_a_route_rid = "1201";
    std::string session_b_route_rid = "1202";
    std::string redis_endpoint;
    std::string redis_key_prefix = "bingo:";
    std::string session_a_router_rid = "1101";
    std::string session_b_router_rid = "1103";
    std::string session_a_pub_rid = "1102";
    std::string session_b_pub_rid = "1104";
    zlink::routing_id_t play_rid = zlink::routing_id_t::from ("2202");

    std::string selected_api_channel_endpoint () const
    {
        return api_node == "b" ? api_b_channel_endpoint : api_a_channel_endpoint;
    }

    std::string selected_play_channel_endpoint () const
    {
        return play_node == "b" ? play_b_channel_endpoint : play_a_channel_endpoint;
    }

    std::string peer_play_channel_endpoint () const
    {
        return play_node == "b" ? play_a_channel_endpoint : play_b_channel_endpoint;
    }

    std::string peer_play_route_endpoint () const
    {
        return play_node == "b" ? play_a_route_endpoint : play_b_route_endpoint;
    }

    std::string selected_api_play_route_endpoint () const
    {
        return api_node == "b" ? api_b_play_route_endpoint : api_a_play_route_endpoint;
    }

    std::string selected_api_route_rid () const
    {
        return api_node == "b" ? api_b_route_rid : api_a_route_rid;
    }

    std::string selected_session_route_endpoint () const
    {
        return session_node == "b" ? session_b_route_endpoint : session_a_route_endpoint;
    }

    std::string selected_play_route_endpoint () const
    {
        return play_node == "b" ? play_b_route_endpoint : play_a_route_endpoint;
    }

    std::string selected_session_route_rid () const
    {
        return session_node == "b" ? session_b_route_rid : session_a_route_rid;
    }

    std::string selected_session_router_rid () const
    {
        return session_node == "b" ? session_b_router_rid : session_a_router_rid;
    }

    std::string selected_session_pub_rid () const
    {
        return session_node == "b" ? session_b_pub_rid : session_a_pub_rid;
    }

    std::string selected_session_spot_node_rid () const
    {
        return session_node == "b" ? session_b_spot_node_rid : session_a_spot_node_rid;
    }

    std::string selected_play_spot_endpoint () const
    {
        return play_node == "b" ? play_b_spot_endpoint : play_a_spot_endpoint;
    }

    std::string peer_play_spot_endpoint () const
    {
        return play_node == "b" ? play_a_spot_endpoint : play_b_spot_endpoint;
    }

    std::string selected_play_spot_router_endpoint () const
    {
        return play_node == "b" ? play_b_spot_router_endpoint : play_a_spot_router_endpoint;
    }

    std::string selected_play_node_rid () const
    {
        return play_node == "b" ? play_b_node_rid : play_a_node_rid;
    }

    std::string preferred_play_node_rid () const
    {
        return session_node == "b" ? play_b_node_rid : play_a_node_rid;
    }

    std::string preferred_play_spot_router_endpoint () const
    {
        return session_node == "b" ? play_b_spot_router_endpoint : play_a_spot_router_endpoint;
    }

    std::string selected_stream_endpoint () const
    {
        return session_node == "b" ? session_b_stream_endpoint : session_a_stream_endpoint;
    }
};

} // namespace zlink::samples::bingo
