/* SPDX-License-Identifier: FSL-1.1-ALv2 */
#pragma once

#include "../Configuration/sample_names.hpp"
#include "../Configuration/sample_topology.hpp"
#include "../Configuration/location_store.hpp"
#include "../common_codecs.hpp"
#include "../sample_log_dir.hpp"
#include "../../Shared/Contracts/messages.hpp"
#include "Handlers/authenticate_player_handler.hpp"
#include "Handlers/match_bingo_handler.hpp"

namespace zlink::samples::bingo
{

using namespace framework;

inline app_t &add_bingo_api_server (app_t &app, const sample_topology_t &topology)
{
    app.add_zlink_framework ([&] (zlink_framework_options_t &options) {
        options.configure_dispatch ()
          .message_flow (message_flow_log_mode_t::key_transitions)
          .trace_log_file (flow_log_path (topology.log_dir, "api-" + topology.api_node))
          .trace_label ("api-" + topology.api_node);
        use_default_bingo_codecs (options.codecs ());
        add_sample_location_store (options, topology);

        options.add_client_server_channel (sample_names_t::api_channel)
          .enable_server (topology.selected_api_channel_endpoint ())
          .set_routing_id (zlink::routing_id_t::from (topology.selected_api_route_rid ()))
          .use_handler_group ("api");

        options.add_client_server_channel (play_channel_for (topology.play_a_node_rid)).enable_client ();
        options.add_client_server_channel (play_channel_for (topology.play_b_node_rid)).enable_client ();

        options.handlers ()
          .group ("api")
          .add<authenticate_player_handler_t> ()
          .add<match_bingo_api_handler_t> ();
    });
    return app;
}

} // namespace zlink::samples::bingo
