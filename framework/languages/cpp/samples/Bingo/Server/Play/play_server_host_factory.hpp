/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../Configuration/sample_configuration.hpp"
#include "../Configuration/sample_names.hpp"
#include "../Configuration/sample_topology.hpp"
#include "../../Shared/Contracts/messages.hpp"
#include "../host_support.hpp"
#include "Infrastructure/ZLink/Handlers/allocate_bingo_room_handler.hpp"
#include "Infrastructure/ZLink/Handlers/ensure_player_actor_handler.hpp"
#include "Infrastructure/ZLink/Matchmaking/redis_bingo_match_queue.hpp"
#include "Infrastructure/ZLink/Actors/player_actor_factory.hpp"
#include "Infrastructure/ZLink/Spots/bingo_entry_spot.hpp"
#include "Infrastructure/ZLink/Spots/bingo_room_spot.hpp"
#include "Application/RoomAllocation/bingo_room_allocator.hpp"

#include <zlink/codecs/protobuf.hpp>

#include <memory>

namespace zlink::samples::bingo
{

class play_server_host_factory_t
{
  public:
    static zlink::framework::app_t build (const sample_topology_t &topology, bool auto_stop = true)
    {
        auto app = zlink::framework::app_t::create ();
        configure (app, topology, auto_stop);
        return app;
    }

    static zlink::framework::app_t &configure (zlink::framework::app_t &app,
                                               const sample_topology_t &topology,
                                               bool auto_stop = true)
    {
        if (auto_stop) {
            app.add_hosted_service (std::make_unique<stop_after_start_service_t> (app));
        }
        app.add_zlink_framework ([&] (zlink::framework::zlink_framework_options_t &options) {
            options.services ()
              .add_singleton<sample_topology_t> (std::make_unique<sample_topology_t> (topology))
              .add_singleton<bingo_match_queue_t> (
                std::make_unique<redis_bingo_match_queue_t> (topology))
              .add_singleton<bingo_room_allocator_t, bingo_match_queue_t> ();
            options.handlers ()
              .add<allocate_bingo_room_handler_t> ("play")
              .add<ensure_player_actor_handler_t> ("play");
            options.codecs ().use (
              zlink::framework_codecs::protobuf<ensure_player_actor_req_t,
                                                ensure_player_actor_res_t,
                                                allocate_bingo_room_req_t,
                                                allocate_bingo_room_res_t,
                                                match_bingo_api_req_t,
                                                match_bingo_api_res_t,
                                                match_bingo_req_t,
                                                match_bingo_res_t,
                                                bingo_room_join_req_t,
                                                bingo_room_join_res_t,
                                                submit_bingo_card_req_t,
                                                submit_bingo_card_res_t,
                                                player_joined_notify_t,
                                                game_started_notify_t,
                                                number_drawn_notify_t,
                                                game_ended_notify_t,
                                                observe_bingo_events_req_t,
                                                observe_bingo_events_res_t,
                                                stop_observing_bingo_events_req_t,
                                                stop_observing_bingo_events_res_t,
                                                bingo_reward_acquired_event_t,
                                                bingo_reward_announced_notify_t,
                                                bingo_room_settings_payload_t> ());
            options.use_discovery ().add_registry_endpoint (topology.registry_router_endpoint);
            options.add_client_server_channel (sample_names_t::play_channel)
              .enable_server (topology.selected_play_channel_endpoint ())
              .use_handler_group ("play");
            options.add_route_mesh_channel (sample_names_t::play_route_channel)
              .enable_server (topology.selected_play_route_endpoint ())
              .set_routing_id (zlink::routing_id_t::from (topology.selected_play_node_rid ()))
              .enable_client ()
              .enable_spot_route_egress (sample_names_t::play_route_channel);
            options.add_client_server_channel (sample_names_t::api_channel)
              .enable_client ();
            options.add_spot_mesh (sample_names_t::room_spot_discovery)
              .use_registry_spot_resolver (sample_names_t::play_route_channel)
              .add_node (topology.selected_play_node_rid ())
              .enable_router (topology.selected_play_spot_router_endpoint (),
                              zlink::routing_id_t::from (topology.selected_play_node_rid ()))
              .enable_actor_gateway ()
              .enable_pub_sub (topology.selected_play_spot_endpoint (),
                               zlink::routing_id_t::from (topology.selected_play_node_rid ()))
              .accept_routes_from_channel (sample_names_t::play_route_channel)
              .attach_channel_client (sample_names_t::api_channel)
              .add_entry_spot<bingo_entry_spot_t> ()
              .add_spot<bingo_room_spot_t> (sample_names_t::room_spot)
              .add_actor_factory<player_actor_factory_t> (sample_names_t::player_actor_type);
        });
        return app;
    }
};

} // namespace zlink::samples::bingo
