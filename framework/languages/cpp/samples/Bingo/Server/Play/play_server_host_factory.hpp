/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../../Shared/sample.hpp"
#include "BingoRoomSpots/bingo_room.hpp"
#include "Handlers/allocate_bingo_room_handler.hpp"
#include "Handlers/bingo_room_directory.hpp"
#include "Handlers/ensure_player_actor_handler.hpp"

namespace zlink::samples::bingo
{

class play_server_host_factory_t
{
  public:
    static zlink::framework::app_t build (const sample_topology_t &topology)
    {
        auto app = zlink::framework::app_t::create ();
        add_sample_auto_stop (app);
        app.add_zlink_framework ([&] (zlink::framework::zlink_framework_options_t &options) {
            options.services ().add_singleton<bingo_room_directory_t> ();
            options.handlers ().add<allocate_bingo_room_handler_t> ("play").add<ensure_player_actor_handler_t> ("play");
            options.codecs ().add_json ();
            options.discovery ().add (topology.registry_router_endpoint);
            options.client_server_channel (sample_names_t::play_channel)
              .server (topology.play_channel_endpoint)
              .handler_group ("play");
            options.client_server_channel (sample_names_t::api_channel).client ();
            options.publisher_channel (sample_names_t::notification_channel).bind ("tcp://127.0.0.1:47120");
            options.spot_mesh (sample_names_t::room_spot_discovery)
              .node (sample_names_t::room_spot_node)
              .enable_router (topology.play_spot_router_endpoint, topology.play_rid)
              .enable_pub_sub (topology.play_spot_endpoint)
              .attach_channel_client (sample_names_t::api_channel)
              .add_spot<bingo_room_t> (sample_names_t::room_spot);
        });
        return app;
    }
};

} // namespace zlink::samples::bingo
