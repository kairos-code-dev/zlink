/* SPDX-License-Identifier: FSL-1.1-ALv2 */
#pragma once

#include "../Configuration/sample_configuration.hpp"
#include "../Configuration/location_store.hpp"
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
              .trace_log_file (flow_log_path (topology.log_dir, "api"))
              .trace_label ("tictactoe-api");

            add_sample_location_store (options, topology);

            options.add_client_server_channel (sample_names_t::api_channel)
              .enable_server (topology.selected_api_endpoint ())
              .use_handler_group ("api");

            options.http ()
              .listen (topology.selected_api_http_endpoint ())
              .map_post<create_game_http_handler_t> ("/games");

            /* 정본 TicTacToe는 location store 자동 연결 대신 수동 endpoint scale-out을
             * 보여 준다(공통 sample spec §6/§18): Play 두 노드를 직접 연결한다. */
            auto play_peers = options.add_client_server_channel (sample_names_t::play_channel);
            for (const auto &endpoint : topology.all_play_endpoints ()) {
                play_peers.enable_client (endpoint);
            }

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
