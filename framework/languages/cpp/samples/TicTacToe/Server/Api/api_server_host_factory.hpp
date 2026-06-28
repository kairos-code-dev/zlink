/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../Configuration/sample_configuration.hpp"
#include "../Configuration/sample_names.hpp"
#include "../Configuration/sample_topology.hpp"
#include "../../Shared/Contracts/messages.hpp"
#include "../host_support.hpp"
#include "../sample_log_dir.hpp"
#include "Handlers/authenticate_player_handler.hpp"
#include "Handlers/create_game_http_handler.hpp"

#include <memory>

namespace zlink::samples::tictactoe
{

using namespace framework;

inline constexpr const char *sample_log_file = "tictactoe-server.log";

class api_server_host_factory_t
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
        app.add_zlink_framework ([&] (zlink_framework_options_t &options) {
            options.configure_dispatch ()
              .message_flow (message_flow_log_mode_t::key_transitions)
              .trace_log_file (flow_log_path ("api"))
              .trace_label ("tictactoe-api");

            options.codecs ().add_json ();

            options.add_client_server_channel (sample_names_t::api_channel)
              .enable_server (topology.selected_api_endpoint ())
              .use_handler_group ("api");

            options.http ()
              .listen (topology.selected_api_http_endpoint ())
              .map_post<create_game_http_handler_t> ("/games");

            options.add_client_server_channel (sample_names_t::play_channel)
              .enable_client (topology.selected_play_endpoint ());

            options.handlers ()
              .group ("api")
              .add<authenticate_player_handler_t> ();
        });
        if (auto_stop) {
            app.add_hosted_service (std::make_unique<stop_after_start_service_t> (app));
        }
        return app;
    }
};

} // namespace zlink::samples::tictactoe
