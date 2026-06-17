/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../Configuration/sample_configuration.hpp"
#include "../Configuration/sample_names.hpp"
#include "../Configuration/sample_topology.hpp"
#include "../../Shared/Contracts/messages.hpp"
#include "../host_support.hpp"
#include "Adapters/ZLink/Handlers/ensure_player_actor_handler.hpp"
#include "Adapters/ZLink/Handlers/create_game_handler.hpp"
#include "Adapters/ZLink/Sessions/play_session.hpp"
#include "Adapters/ZLink/Spots/tictactoe_entry_spot.hpp"
#include "Adapters/ZLink/Spots/tictactoe_game_spot.hpp"
#include "Application/GameCreation/tictactoe_game_creator.hpp"

#include "runtime/actors/actor_gateway_runtime.hpp"
#include "runtime/spots/spot_runtime.hpp"

#include <memory>

namespace zlink::samples::tictactoe
{

class play_spot_gateway_wiring_service_t final : public zlink::framework::hosted_service_t
{
  public:
    void start (zlink::framework::service_provider_t &provider) override
    {
        auto &gateway = provider.get_required<zlink::framework::detail::actor_gateway_runtime_t> ();
        auto &spots = provider.get_required<zlink::framework::detail::spot_node_runtime_t> ();
        gateway.on_join_spot ([&spots] (const zlink::framework::actor_ref_t &actor_ref,
                                        zlink::framework::spot_rid_t spot_rid,
                                        const zlink::message_t &payload) {
            auto actor = spots.actor_instance<player_actor_t> (actor_ref);
            if (!actor) {
                return zlink::framework::result_t<zlink::framework::detail::actor_join_reply_t>::
                  failure (zlink::framework::framework_error_kind_t::actor_route_not_found,
                           "player actor instance is not registered");
            }
            return spots.join_actor_to_spot<tictactoe_game_spot_t> (actor_ref, std::move (spot_rid),
                                                                    actor->get (), payload);
        });
        gateway.on_join_entry_spot ([&spots] (const zlink::framework::actor_ref_t &actor_ref,
                                              zlink::framework::node_rid_t node_rid,
                                              const zlink::message_t &payload) {
            auto actor = spots.actor_instance<player_actor_t> (actor_ref);
            if (!actor) {
                return zlink::framework::result_t<zlink::framework::detail::actor_join_reply_t>::
                  failure (zlink::framework::framework_error_kind_t::actor_route_not_found,
                           "player actor instance is not registered");
            }
            return spots.join_actor_to_entry_spot<entry_spot_t> (actor_ref, std::move (node_rid),
                                                                 actor->get (), payload);
        });
    }

    void stop () noexcept override {}
};

class play_server_host_factory_t
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
        if (auto_stop) {
            app.add_hosted_service (std::make_unique<stop_after_start_service_t> (app));
        }
        app.add_hosted_service (std::make_unique<play_spot_gateway_wiring_service_t> ());
        app.add_zlink_framework ([&] (zlink::framework::zlink_framework_options_t &options) {
            options.services ().add_singleton<tictactoe_game_creator_t> ();
            options.handlers ()
              .add<create_game_handler_t> ("play")
              .add<ensure_player_actor_handler_t> ("play");
            options.codecs ()
              .add_json ()
              .add_json<authenticate_player_req_t> ()
              .add_json<authenticate_player_res_t> ()
              .add_json<create_game_req_t> ()
              .add_json<create_game_res_t> ()
              .add_json<join_game_req_t> ()
              .add_json<join_game_res_t> ()
              .add_json<place_mark_req_t> ()
              .add_json<place_mark_res_t> ()
              .add_json<player_joined_notify_t> ()
              .add_json<game_state_notify_t> ()
              .add_json<game_ended_notify_t> ();
            options.services ().add_singleton<sample_topology_t> (
              std::make_unique<sample_topology_t> (topology));
            options.add_client_server_channel (sample_names_t::play_channel)
              .enable_server (topology.play_endpoint)
              .use_handler_group ("play");
            options.add_client_server_channel (sample_names_t::api_channel)
              .enable_client (topology.api_endpoint);
            options.add_route_mesh_channel (sample_names_t::router_channel)
              .enable_server (topology.play_router_endpoint)
              .set_routing_id (topology.play_rid)
              .enable_spot_route_egress (sample_names_t::game_spot_discovery);
            options.add_spot_mesh (sample_names_t::game_spot_discovery)
              .add_node (sample_names_t::spot_node)
              .enable_router (topology.play_spot_router_endpoint, topology.play_rid)
              .enable_actor_gateway ()
              .add_entry_spot<entry_spot_t> ()
              .add_spot<tictactoe_game_spot_t> (sample_names_t::match_spot)
              .add_actor_factory<player_actor_factory_t> (sample_names_t::actor_type);
            options.add_stream_node (sample_names_t::stream_name)
              .bind (topology.stream_endpoint)
              .register_session<play_session_t> ()
              .attach_actor_gateway (sample_names_t::spot_node);
        });
        return app;
    }
};

} // namespace zlink::samples::tictactoe
