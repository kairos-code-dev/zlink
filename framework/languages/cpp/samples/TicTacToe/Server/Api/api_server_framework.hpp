/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../../Shared/sample.hpp"

namespace zlink::samples::tictactoe
{

inline zlink::framework::app_t &
add_tictactoe_api_server (zlink::framework::app_t &app,
                          const sample_topology_t &topology)
{
  app.add_zlink_framework (
    [&](zlink::framework::zlink_framework_options_t &options) {
      options.services ().add_singleton<create_match_room_handler_t> ();

      options.handlers ()
        .add<authenticate_actor_handler_t> ("api")
        .add<create_match_handler_t> ("api");

      options.codecs ().add_json ();

      options.discovery ().add (topology.registry_router_endpoint);

      options.client_server_channel (sample_names_t::api_channel)
        .server (topology.api_endpoint)
        .handler_group ("api");

      options.client_server_channel (sample_names_t::play_channel)
        .client ();
    });
  return app;
}

} // namespace zlink::samples::tictactoe
