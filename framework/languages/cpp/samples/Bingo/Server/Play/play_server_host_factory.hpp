/* SPDX-License-Identifier: FSL-1.1-ALv2 */
#pragma once

#include "../Configuration/sample_configuration.hpp"
#include "../Configuration/location_store.hpp"
#include "../Configuration/sample_names.hpp"
#include "../Configuration/sample_topology.hpp"
#include "../common_codecs.hpp"
#include "../sample_log_dir.hpp"
#include "../../Shared/Contracts/messages.hpp"
#include "../host_support.hpp"
#include "Infrastructure/ZLink/Handlers/allocate_bingo_room_handler.hpp"
#include "Infrastructure/Redis/redis_bingo_match_queue.hpp"
#include "Infrastructure/ZLink/Actors/player_actor_factory.hpp"
#include "Infrastructure/ZLink/Actors/player_actor_transfer_adapter.hpp"
#include "Infrastructure/ZLink/Spots/EntrySpot/bingo_entry_spot.hpp"
#include "Infrastructure/ZLink/Spots/BingoRoomSpot/bingo_room_spot.hpp"
#include "Application/RoomAllocation/bingo_room_allocator.hpp"

#include <memory>

namespace zlink::samples::bingo
{

using namespace framework;

class play_server_host_factory_t
{
  public:
    static app_t build (const sample_topology_t &topology, bool auto_stop = true)
    {
        auto app = app_t::create ();
        configure (app, topology, auto_stop);
        return app;
    }

    static app_t &configure (app_t &app, const sample_topology_t &topology, bool auto_stop = true)
    {
        if (auto_stop) {
            app.add_hosted_service (std::make_unique<stop_after_start_service_t> (app));
        }
        app.logging ().use_console ().set_min_level (log_level_t::info);
        observe_runtime_metrics (app, topology.log_dir, "play-" + topology.play_node);
        app.add_zlink_framework ([&] (zlink_framework_options_t &options) {
            options.configure_dispatch ()
              .message_flow (message_flow_log_mode_t::key_transitions)
              .trace_log_file (flow_log_path (topology.log_dir, "play-" + topology.play_node))
              .trace_label ("play-" + topology.play_node);
            options.services ()
              .add_singleton<sample_topology_t> (std::make_unique<sample_topology_t> (topology))
              .add_singleton<bingo_match_queue_t> (
                std::make_unique<redis_bingo_match_queue_t> (topology))
              .add_singleton<bingo_room_allocator_t, bingo_match_queue_t> ();
            use_default_bingo_codecs (options.codecs ());
            add_sample_location_store (options, topology);
            auto services = options.services ().build_provider ();
            options.add_client_server_channel (play_channel_for (topology.selected_play_node_rid ()))
              .enable_server (topology.selected_play_route_endpoint ())
              .set_routing_id (zlink::routing_id_t::from (topology.selected_play_node_rid ()))
              .enable_client ()
              .use_handler_group ("play");
            options.add_client_server_channel (sample_names_t::api_channel).enable_client ();
            auto room_mesh = options.add_route_mesh (sample_names_t::room_spot_mesh);
            room_mesh.channel_name (sample_names_t::room_spot_mesh);
            room_mesh.peer_connections ().connect (
              routing_id_t::from (topology.peer_play_node_rid ()),
              topology.peer_play_spot_router_endpoint ());
            room_mesh.set_routing_id (routing_id_t::from (topology.selected_play_node_rid ()))
              .listen (topology.selected_play_spot_router_endpoint ())
              .add_entry_spot<bingo_entry_spot_t> (
                [topology, services] {
                    return std::make_shared<bingo_entry_spot_t> (topology, services);
                })
              .add_spot<bingo_room_spot_t> (sample_names_t::room_spot)
              .add_actor_factory<player_actor_factory_t> (sample_names_t::player_actor_type)
              .add_actor_transfer_adapter<player_actor_t, player_actor_transfer_adapter_t> (
                sample_names_t::player_actor_type);
            options.handlers ()
              .group ("play")
              .add<allocate_bingo_room_handler_t> ();
        });
        return app;
    }
};

} // namespace zlink::samples::bingo
