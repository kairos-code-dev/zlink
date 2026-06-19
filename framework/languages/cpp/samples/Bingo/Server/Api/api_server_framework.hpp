/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../Configuration/sample_names.hpp"
#include "../Configuration/sample_topology.hpp"
#include "../../Shared/Contracts/messages.hpp"
#include "Handlers/authenticate_player_handler.hpp"
#include "Handlers/match_bingo_handler.hpp"

#include <zlink/codecs/protobuf.hpp>

namespace zlink::samples::bingo
{

inline zlink::framework::app_t &add_bingo_api_server (zlink::framework::app_t &app,
                                                      const sample_topology_t &topology)
{
    app.add_zlink_framework ([&] (zlink::framework::zlink_framework_options_t &options) {
        options.services ().add_singleton<sample_topology_t> (
          std::make_unique<sample_topology_t> (topology));
        options.handlers ()
          .add<authenticate_player_handler_t> ("api")
          .add<match_bingo_api_handler_t> ("api");

        options.codecs ().use (
          zlink::framework_codecs::protobuf<authenticate_player_req_t,
                                            authenticate_player_res_t,
                                            match_bingo_api_req_t,
                                            match_bingo_api_res_t,
                                            allocate_bingo_room_req_t,
                                            allocate_bingo_room_res_t> ());

        options.use_discovery ().add_registry_endpoint (topology.registry_router_endpoint);

        options.add_client_server_channel (sample_names_t::api_channel)
          .enable_server (topology.selected_api_channel_endpoint ())
          .use_handler_group ("api");

        options.add_client_server_channel (sample_names_t::play_channel)
          .enable_client (topology.api_node == "b" ? topology.play_b_channel_endpoint
                                                   : topology.play_a_channel_endpoint);
    });
    return app;
}

} // namespace zlink::samples::bingo
