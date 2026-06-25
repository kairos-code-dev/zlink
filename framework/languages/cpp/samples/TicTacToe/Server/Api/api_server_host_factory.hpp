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

inline constexpr const char *sample_log_file = "tictactoe-server.log";

class api_server_host_factory_t
{
  public:
    static zlink::framework::app_t build (const sample_topology_t &topology, bool auto_stop = true)
    {
        auto app = zlink::framework::app_t::create ();
        configure (app, topology, auto_stop);
        return app;
    }

    static zlink::framework::app_t &configure (zlink::framework::app_t &app,
                                               const sample_topology_t &topology,
                                               bool auto_stop = true)
    {
        app.add_zlink_framework ([&] (zlink::framework::zlink_framework_options_t &options) {
            options.configure_dispatch ()
              .message_flow (zlink::framework::message_flow_log_mode_t::key_transitions)
              .trace_log_file (flow_log_path ("api"))
              .trace_label ("tictactoe-api");
            options.handlers ().add<authenticate_player_handler_t> ("api");

            options.codecs ()
              .add_json ()
              .add_json<authenticate_player_req_t> ()
              .add_json<authenticate_player_res_t> ()
              .add_json<create_game_req_t> ()
              .add_json<create_game_res_t> ();

            options.add_client_server_channel (sample_names_t::api_channel)
              .enable_server (topology.selected_api_endpoint ())
              .use_handler_group ("api");

            options.http ()
              .listen (topology.selected_api_http_endpoint ())
              .map_post<create_game_http_handler_t> ("/games");

            options.add_client_server_channel (sample_names_t::play_channel)
              .enable_client (topology.selected_play_endpoint ());
        });
        if (auto_stop) {
            app.add_hosted_service (std::make_unique<stop_after_start_service_t> (app));
        }
        return app;
    }
};

} // namespace zlink::samples::tictactoe
