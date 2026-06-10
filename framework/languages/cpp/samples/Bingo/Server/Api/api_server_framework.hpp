/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../../Shared/sample.hpp"
#include "Handlers/authenticate_player_handler.hpp"
#include "Handlers/match_bingo_handler.hpp"

namespace zlink::samples::bingo
{

inline zlink::framework::app_t &add_bingo_api_server (zlink::framework::app_t &app,
                                                      const sample_topology_t &topology)
{
    app.add_zlink_framework ([&] (zlink::framework::zlink_framework_options_t &options) {
        options.handlers ()
          .add<authenticate_player_handler_t> ("api")
          .add<match_bingo_api_handler_t> ("api");

        options.codecs ()
          .add_protobuf ()
          .add_protobuf<allocate_bingo_room_req_t> ()
          .add_protobuf<allocate_bingo_room_res_t> ();

        options.use_discovery ().add_registry_endpoint (topology.registry_router_endpoint);

        options.add_client_server_channel (sample_names_t::api_channel)
          .enable_server (topology.api_channel_endpoint)
          .use_handler_group ("api");

        options.add_client_server_channel (sample_names_t::play_channel).enable_client ();
    });
    return app;
}

} // namespace zlink::samples::bingo
