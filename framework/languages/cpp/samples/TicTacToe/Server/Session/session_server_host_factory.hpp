/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../../Shared/sample.hpp"

namespace zlink::samples::tictactoe
{

class session_server_host_factory_t
{
public:
  static zlink::framework::app_t build (
    const sample_topology_t &topology)
  {
    auto app = zlink::framework::app_t::create ();
    add_sample_auto_stop (app);
    app.add_zlink_framework (
      [&](zlink::framework::zlink_framework_options_t &options) {
        options.codecs ().add_json ();
        options.stream_node (sample_names_t::stream_name)
          .bind (topology.stream_endpoint)
          .packet_session ("client-session")
          .attach_actor_gateway (sample_names_t::spot_node);
      });
    return app;
  }
};

} // namespace zlink::samples::tictactoe
