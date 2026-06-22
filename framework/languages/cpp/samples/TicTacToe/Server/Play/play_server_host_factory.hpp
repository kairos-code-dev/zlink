/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../Configuration/sample_configuration.hpp"
#include "../Configuration/sample_names.hpp"
#include "../Configuration/redis_room_route_store.hpp"
#include "../Configuration/sample_topology.hpp"
#include "../../Shared/Contracts/messages.hpp"
#include "../host_support.hpp"
#include "Infrastructure/ZLink/Handlers/ensure_player_actor_handler.hpp"
#include "Infrastructure/ZLink/Handlers/create_game_handler.hpp"
#include "Infrastructure/ZLink/Sessions/play_session.hpp"
#include "Infrastructure/ZLink/Spots/tictactoe_entry_spot.hpp"
#include "Infrastructure/ZLink/Spots/tictactoe_game_spot.hpp"
#include "Application/GameCreation/tictactoe_game_creator.hpp"

#include <memory>
#include <optional>

namespace zlink::samples::tictactoe
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
              .add_singleton<redis_room_route_store_t, sample_topology_t> ()
              .add_singleton<tictactoe_game_creator_t, sample_topology_t, redis_room_route_store_t> ();
            options.handlers ()
              .add<create_game_handler_t> ("play")
              .add<ensure_player_actor_handler_t> ("play");
            options.codecs ()
              .add_json ()
              .add_json<authenticate_player_req_t> ()
              .add_json<authenticate_player_res_t> ()
              .add_json<authenticate_req_t> ()
              .add_json<authenticate_res_t> ()
              .add_json<create_game_req_t> ()
              .add_json<create_game_res_t> ()
              .add_json<join_game_req_t> ()
              .add_json<tictactoe_game_join_req_t> ()
              .add_json<join_game_res_t> ()
              .add_json<place_mark_req_t> ()
              .add_json<place_mark_res_t> ()
              .add_json<observe_milestone_req_t> ()
              .add_json<observe_milestone_res_t> ()
              .add_json<leave_game_req_t> ()
              .add_json<player_win_milestone_event_t> ()
              .add_json<win_milestone_notify_t> ()
              .add_json<player_joined_notify_t> ()
              .add_json<game_state_notify_t> ()
              .add_json<game_ended_notify_t> ();
            options.services ().add_singleton<sample_topology_t> (
              std::make_unique<sample_topology_t> (topology));
            options.add_client_server_channel (sample_names_t::play_channel)
              .enable_server (topology.selected_play_endpoint ())
              .use_handler_group ("play");
            options.add_route_mesh_channel (sample_names_t::play_route_channel)
              .enable_server (topology.selected_play_route_endpoint ())
              .set_routing_id (zlink::routing_id_t::from (topology.selected_play_node_rid ()))
              .enable_client (topology.peer_play_route_endpoint ())
              .enable_spot_route_egress (sample_names_t::play_route_channel);
            options.add_client_server_channel (sample_names_t::api_channel)
              .enable_client (topology.api_endpoint);
            options.add_spot_mesh (sample_names_t::game_spot_discovery)
              .use_registry_spot_resolver (sample_names_t::play_route_channel)
              .add_node (topology.selected_play_node_rid ())
              .enable_router (topology.selected_play_spot_router_endpoint (),
                              zlink::routing_id_t::from (topology.selected_play_node_rid ()))
              .enable_actor_gateway ()
              .enable_pub_sub (topology.selected_play_spot_endpoint (),
                               zlink::routing_id_t::from (topology.selected_play_node_rid ()))
              .connect_peer_pub (topology.peer_play_spot_endpoint ())
              .accept_routes_from_channel (sample_names_t::play_route_channel,
                                           topology.peer_play_route_endpoint ())
              .add_entry_spot<entry_spot_t> ()
              .add_spot<tictactoe_game_spot_t> (sample_names_t::match_spot)
              .add_actor_factory<player_actor_factory_t> (sample_names_t::actor_type);
            options.add_stream_node (sample_names_t::stream_name)
              .bind (topology.selected_stream_endpoint ())
              .register_session<play_session_t> ()
              .attach_actor_gateway (topology.selected_play_node_rid ());
        });
        return app;
    }
};

} // namespace zlink::samples::tictactoe
