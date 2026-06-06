/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../../Shared/sample.hpp"
#include "../../Shared/sample_log.hpp"
#include "../../Shared/E2E/client_e2e_server.hpp"
#include "../Play/Handlers/create_match_room_handler.hpp"
#include "Handlers/authenticate_actor_handler.hpp"
#include "Handlers/create_match_handler.hpp"

#include <memory>

namespace zlink::samples::tictactoe
{

class api_server_host_factory_t
{
  public:
    static zlink::framework::app_t build (const sample_topology_t &topology, bool auto_stop = true)
    {
        if (!auto_stop) {
            return build_client_e2e_api_server (topology);
        }

        auto app = zlink::framework::app_t::create ();
        app.logging ().use_file (sample_log_file);
        app.add_zlink_framework ([&] (zlink::framework::zlink_framework_options_t &options) {
            options.services ().add_singleton<sample_topology_t> (std::make_unique<sample_topology_t> (topology));
            options.services ().add_singleton<create_match_room_handler_t> ();

            options.handlers ().add<authenticate_actor_handler_t> ("api").add<create_match_handler_t> ("api");

            options.codecs ().add_json ();

            options.use_discovery ().add_registry_endpoint (topology.registry_router_endpoint);

            options.add_client_server_channel (sample_names_t::api_channel)
              .enable_server (topology.api_endpoint)
              .use_handler_group ("api");

            options.http ().listen (topology.api_http_endpoint).map_post<create_match_handler_t> ("/games");

            options.add_client_server_channel (sample_names_t::play_channel).enable_client ();
        });
        app.add_hosted_service (std::make_unique<stop_after_start_service_t> (app));
        return app;
    }
};

} // namespace zlink::samples::tictactoe
