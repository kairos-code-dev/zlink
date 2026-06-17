/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../Configuration/sample_configuration.hpp"
#include "../Configuration/sample_names.hpp"
#include "../Configuration/sample_topology.hpp"
#include "../../Shared/Contracts/messages.hpp"
#include "../host_support.hpp"
#include "Adapters/ZLink/Handlers/allocate_bingo_room_handler.hpp"
#include "Adapters/ZLink/Handlers/ensure_player_actor_handler.hpp"
#include "Adapters/ZLink/Actors/player_actor_factory.hpp"
#include "Adapters/ZLink/Spots/bingo_entry_spot.hpp"
#include "Adapters/ZLink/Spots/bingo_room_spot.hpp"
#include "Application/RoomAllocation/bingo_room_allocator.hpp"

#include "runtime/actors/actor_gateway_runtime.hpp"
#include "runtime/spots/spot_runtime.hpp"
#include <zlink/framework/extensions/remote_actor_packet_handler.hpp>

#include <memory>

namespace zlink::samples::bingo
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
            (void) spots.get_or_create_spot (sample_names_t::room_spot, spot_rid);
            auto actor = spots.actor_instance<player_actor_t> (actor_ref);
            if (!actor) {
                return zlink::framework::result_t<zlink::framework::detail::actor_join_reply_t>::
                  failure (zlink::framework::framework_error_kind_t::actor_route_not_found,
                           "player actor instance is not registered");
            }
            return spots.join_actor_to_spot<bingo_room_spot_t> (actor_ref, std::move (spot_rid),
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
            return spots.join_actor_to_entry_spot<bingo_entry_spot_t> (
              actor_ref, std::move (node_rid), actor->get (), payload);
        });
    }

    void stop () noexcept override {}
};

using remote_actor_packet_handler_t =
  zlink::framework::extensions::remote_actor_packet_handler_t<player_actor_t,
                                                              remote_actor_packet_req_t,
                                                              remote_actor_packet_res_t>;

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
        auto notifications =
          std::make_shared<bingo_notification_publisher_t> (topology.notification_channel_endpoint);
        bingo_room_spot_t::use_notification_publisher (notifications);
        if (auto_stop) {
            app.add_hosted_service (std::make_unique<stop_after_start_service_t> (app));
        }
        app.add_hosted_service (std::make_unique<play_spot_gateway_wiring_service_t> ());
        app.add_hosted_service (
          std::make_unique<bingo_notification_publisher_hosted_service_t> (notifications));
        app.add_zlink_framework ([&] (zlink::framework::zlink_framework_options_t &options) {
            options.services ().add_singleton<bingo_room_allocator_t> ();
            options.handlers ()
              .add<allocate_bingo_room_handler_t> ("play")
              .add<ensure_player_actor_handler_t> ("play")
              .add<remote_actor_packet_handler_t> ("play");
            options.codecs ()
              .add_protobuf ()
              .add_protobuf<ensure_player_actor_req_t> ()
              .add_protobuf<ensure_player_actor_res_t> ()
              .add_protobuf<remote_actor_packet_req_t> ()
              .add_protobuf<remote_actor_packet_res_t> ()
              .add_protobuf<allocate_bingo_room_req_t> ()
              .add_protobuf<allocate_bingo_room_res_t> ()
              .add_protobuf<match_bingo_req_t> ()
              .add_protobuf<match_bingo_res_t> ()
              .add_protobuf<bingo_room_join_req_t> ()
              .add_protobuf<bingo_room_join_res_t> ()
              .add_protobuf<submit_bingo_card_req_t> ()
              .add_protobuf<submit_bingo_card_res_t> ()
              .add_protobuf<player_joined_notify_t> ()
              .add_protobuf<game_started_notify_t> ()
              .add_protobuf<number_drawn_notify_t> ()
              .add_protobuf<game_ended_notify_t> ();
            options.use_discovery ().add_registry_endpoint (topology.registry_router_endpoint);
            options.add_client_server_channel (sample_names_t::play_channel)
              .enable_server (topology.play_channel_endpoint)
              .use_handler_group ("play");
            options.add_client_server_channel (sample_names_t::api_channel)
              .enable_client (topology.api_channel_endpoint);
            options.add_fanout_channel (sample_names_t::notification_channel)
              .enable_publisher (topology.notification_channel_endpoint);
            options.add_spot_mesh (sample_names_t::room_spot_discovery)
              .add_node (sample_names_t::room_spot_node)
              .enable_router (topology.play_spot_router_endpoint, topology.play_rid)
              .enable_actor_gateway ()
              .enable_pub_sub (topology.play_spot_endpoint)
              .attach_channel_client (sample_names_t::api_channel)
              .add_entry_spot<bingo_entry_spot_t> ()
              .add_spot<bingo_room_spot_t> (sample_names_t::room_spot)
              .add_actor_factory<player_actor_factory_t> (sample_names_t::player_actor_type);
        });
        return app;
    }
};

} // namespace zlink::samples::bingo
