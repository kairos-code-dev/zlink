/* SPDX-License-Identifier: FSL-1.1-ALv2 */
#pragma once

#include "../Configuration/sample_configuration.hpp"
#include "../Configuration/location_store.hpp"
#include "../Configuration/sample_names.hpp"
#include "../Configuration/sample_topology.hpp"
#include "../../Shared/Contracts/messages.hpp"
#include "../host_support.hpp"
#include "../sample_log_dir.hpp"
#include "Infrastructure/ZLink/Handlers/create_game_handler.hpp"
#include "Infrastructure/ZLink/Sessions/play_session.hpp"
#include "Infrastructure/ZLink/Actors/player_actor_relocation_adapter.hpp"
#include "Infrastructure/ZLink/Spots/EntrySpot/tictactoe_entry_spot.hpp"
#include "Infrastructure/ZLink/Spots/TicTacToeGameSpot/tictactoe_game_spot.hpp"
#include "Application/GameCreation/tictactoe_game_creator.hpp"

#include <memory>
#include <optional>

namespace zlink::samples::tictactoe
{

using namespace framework;

class play_server_host_factory_t
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
        if (auto_stop) {
            app.add_hosted_service (std::make_unique<stop_after_start_service_t> (app));
        }
        app.add_zlink_framework ([&] (zlink_framework_options_t &options) {
            options.configure_dispatch ()
              .message_flow (message_flow_log_mode_t::key_transitions)
              .trace_log_file (flow_log_path (topology.log_dir, "play-" + topology.play_node))
              .trace_label ("tictactoe-play-" + topology.play_node);
            options.services ()
              .add_singleton<sample_topology_t> (std::make_unique<sample_topology_t> (topology))
              .add_singleton<tictactoe_game_creator_t, sample_topology_t> ();
            add_sample_location_store (options, topology);
            options.add_client_server_channel (sample_names_t::play_channel)
              .enable_server (topology.selected_play_endpoint ())
              .use_handler_group ("play");
            /* 수동 endpoint scale-out(공통 sample spec §6/§18): API 두 노드를 직접 연결한다. */
            auto api_peers = options.add_client_server_channel (sample_names_t::api_channel);
            for (const auto &endpoint : topology.all_api_endpoints ()) {
                api_peers.enable_client (endpoint);
            }
            auto game_spot = options.add_route_mesh (sample_names_t::game_spot_node);
            game_spot.peer_connections ().connect (
              topology.peer_play_spot_router_endpoint ());
            game_spot.listen (topology.selected_play_spot_router_endpoint ())
              .add_entry_spot<tictactoe_entry_spot_t> (
                [] (entry_spot_context_t context) {
                    return std::make_shared<tictactoe_entry_spot_t> (
                      std::move (context));
                })
              .add_spot_factory<tictactoe_game_spot_t> (
                sample_names_t::match_spot,
                [] (spot_context_t context) {
                    return std::make_shared<tictactoe_game_spot_t> (
                      std::move (context));
                },
                [] (auto &factory) {
                    factory.disable_relocation ();
                })
              .add_actor_factory<player_actor_factory_t> (
                sample_names_t::actor_type,
                [] (auto &factory) {
                    factory
                      .template preserve_state_with<
                        player_actor_relocation_adapter_t> ();
                });
            options.add_stream_node (sample_names_t::stream_name)
              .bind (topology.selected_stream_endpoint ())
              .register_session<play_session_t> ();
            options.handlers ()
              .group ("play")
              .add<create_game_handler_t> ();
        });
        return app;
    }
};

} // namespace zlink::samples::tictactoe
