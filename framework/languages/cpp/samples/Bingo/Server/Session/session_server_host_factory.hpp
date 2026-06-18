/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../Configuration/sample_configuration.hpp"
#include "../Configuration/sample_names.hpp"
#include "../Configuration/sample_topology.hpp"
#include "../../Shared/Contracts/messages.hpp"
#include "Notifications/bingo_notification_subscriber_service.hpp"
#include "../host_support.hpp"
#include "Sessions/bingo_session.hpp"

#include <zlink/codecs/protobuf.hpp>

namespace zlink::samples::bingo
{

class session_server_host_factory_t
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
        app.add_hosted_service (std::make_unique<bingo_notification_subscriber_service_t> (
          topology.notification_channel_endpoint));
        app.add_zlink_framework ([&] (zlink::framework::zlink_framework_options_t &options) {
            options.codecs ().use (
              zlink::framework_codecs::protobuf<authenticate_req_t,
                                                authenticate_res_t,
                                                authenticate_player_req_t,
                                                authenticate_player_res_t,
                                                ensure_player_actor_req_t,
                                                ensure_player_actor_res_t,
                                                remote_actor_packet_req_t,
                                                remote_actor_packet_res_t,
                                                match_bingo_req_t,
                                                match_bingo_res_t,
                                                submit_bingo_card_req_t,
                                                submit_bingo_card_res_t> ());
            options.use_discovery ().add_registry_endpoint (topology.registry_router_endpoint);
            options.add_client_server_channel (sample_names_t::api_channel)
              .enable_client (topology.api_channel_endpoint);
            options.add_client_server_channel (sample_names_t::play_channel)
              .enable_client (topology.play_channel_endpoint);
            options.add_spot_mesh (sample_names_t::room_spot_discovery)
              .add_node (sample_names_t::session_spot_node)
              .enable_router (topology.session_router_endpoint, topology.session_router_rid)
              .enable_pub_sub (topology.session_spot_endpoint, topology.session_pub_rid);
            options.add_stream_node (sample_names_t::stream_node)
              .bind (topology.stream_endpoint)
              .register_session<bingo_session_t> ()
              .attach_actor_gateway (sample_names_t::room_spot_node);
        });
        return app;
    }
};

} // namespace zlink::samples::bingo
