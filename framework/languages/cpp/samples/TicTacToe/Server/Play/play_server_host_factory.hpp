/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../../Shared/sample.hpp"

namespace zlink::samples::tictactoe
{

class play_server_host_factory_t
{
public:
  static zlink::framework::app_t build (
    const sample_topology_t &topology)
  {
    auto app = zlink::framework::app_t::create ();
    add_sample_auto_stop (app);
    app.add_zlink_framework (
      [&](zlink::framework::zlink_framework_options_t &options) {
        options.handlers ()
          .add<create_match_room_handler_t> ("play")
          .add<ensure_player_actor_handler_t> ("play");
        options.codecs ().add_json ();
        options.client_server_channel (sample_names_t::play_channel)
          .server (topology.play_endpoint)
          .handler_group ("play");
        options.spot_node (sample_names_t::spot_node)
          .bind (topology.spot_endpoint)
          .enable_actor_gateway ()
          .add_entry_spot<entry_spot_t> ()
          .add_spot<tictactoe_match_room_t> (sample_names_t::match_spot)
          .add_actor_factory<player_actor_factory_t> (
            sample_names_t::actor_type);
      });
    return app;
  }
};

} // namespace zlink::samples::tictactoe
