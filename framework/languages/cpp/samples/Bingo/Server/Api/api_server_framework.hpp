/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../../Shared/sample.hpp"

namespace zlink::samples::bingo
{

inline zlink::framework::app_t &
add_bingo_api_server (zlink::framework::app_t &app,
                      const sample_topology_t &topology)
{
  app.logging ().use_console ().set_level ("info");
  app.add_zlink_framework (
    [&](zlink::framework::zlink_framework_options_t &options) {
      options.handlers ()
        .add<authenticate_player_handler_t> ("api")
        .add<match_bingo_api_handler_t> ("api");

      options.codecs ().add_json ();

      options.discovery ().add (topology.registry_router_endpoint);

      options.client_server_channel (sample_names_t::api_channel)
        .server (topology.api_channel_endpoint)
        .handler_group ("api");

      options.client_server_channel (sample_names_t::play_channel)
        .client ();
    });
  return app;
}

} // namespace zlink::samples::bingo
