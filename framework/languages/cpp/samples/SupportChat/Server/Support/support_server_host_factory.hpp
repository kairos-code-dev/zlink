/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../Configuration/sample_configuration.hpp"
#include "../Configuration/sample_names.hpp"
#include "../Configuration/sample_topology.hpp"
#include "../../Shared/Contracts/messages.hpp"
#include "../host_support.hpp"
#include "Adapters/ZLink/Actors/support_user_actor_factory.hpp"
#include "Adapters/ZLink/Handlers/allocate_conversation_handler.hpp"
#include "Adapters/ZLink/Handlers/assign_agent_handler.hpp"
#include "Adapters/ZLink/Handlers/ensure_support_user_actor_handler.hpp"
#include "Adapters/ZLink/Notifications/conversation_notification_publisher.hpp"
#include "Adapters/ZLink/Spots/conversation_spot.hpp"
#include "Adapters/ZLink/Spots/support_entry_spot.hpp"
#include "Application/ConversationAssignment/agent_assignment_service.hpp"
#include "Application/ConversationAssignment/agent_availability_directory.hpp"
#include "Application/ConversationAssignment/support_conversation_allocator.hpp"

#include "runtime/actors/actor_gateway_runtime.hpp"
#include "runtime/spots/spot_runtime.hpp"
#include <zlink/framework/extensions/remote_actor_packet_handler.hpp>

#include <memory>

namespace zlink::samples::supportchat
{

// Wires the actor gateway join callbacks to the spot node runtime, creating the
// ConversationSpot on demand and joining the SupportUserActor. Mirrors the Bingo
// play server spot gateway wiring.
class support_spot_gateway_wiring_service_t final : public zlink::framework::hosted_service_t
{
  public:
    void start (zlink::framework::service_provider_t &provider) override
    {
        auto &gateway = provider.get_required<zlink::framework::detail::actor_gateway_runtime_t> ();
        auto &spots = provider.get_required<zlink::framework::detail::spot_node_runtime_t> ();
        gateway.on_join_spot ([&spots] (const zlink::framework::actor_ref_t &actor_ref,
                                        zlink::framework::spot_rid_t spot_rid,
                                        const zlink::message_t &payload) {
            (void) spots.get_or_create_spot (sample_names_t::conversation_spot, spot_rid, payload);
            auto actor = spots.actor_instance<support_user_actor_t> (actor_ref);
            if (!actor) {
                return zlink::framework::result_t<zlink::framework::detail::actor_join_reply_t>::
                  failure (zlink::framework::framework_error_kind_t::actor_route_not_found,
                           "support actor instance is not registered");
            }
            auto join_payload =
              to_stream_payload (join_conversation_req_t{std::string (spot_rid.value ())});
            return spots.join_actor_to_spot<conversation_spot_t> (actor_ref, std::move (spot_rid),
                                                                  actor->get (), join_payload);
        });
        gateway.on_join_entry_spot ([&spots] (const zlink::framework::actor_ref_t &actor_ref,
                                              zlink::framework::node_rid_t node_rid,
                                              const zlink::message_t &payload) {
            auto actor = spots.actor_instance<support_user_actor_t> (actor_ref);
            if (!actor) {
                return zlink::framework::result_t<zlink::framework::detail::actor_join_reply_t>::
                  failure (zlink::framework::framework_error_kind_t::actor_route_not_found,
                           "support actor instance is not registered");
            }
            return spots.join_actor_to_entry_spot<support_entry_spot_t> (
              actor_ref, std::move (node_rid), actor->get (), payload);
        });
    }

    void stop () noexcept override {}
};

using remote_actor_packet_handler_t =
  zlink::framework::extensions::remote_actor_packet_handler_t<support_user_actor_t,
                                                              remote_actor_packet_req_t,
                                                              remote_actor_packet_res_t>;

class support_server_host_factory_t
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
        auto notifications = std::make_shared<conversation_notification_publisher_t> ();
        conversation_spot_t::use_notification_publisher (notifications);
        if (auto_stop) {
            app.add_hosted_service (std::make_unique<stop_after_start_service_t> (app));
        }
        app.add_hosted_service (std::make_unique<support_spot_gateway_wiring_service_t> ());
        app.add_zlink_framework ([&] (zlink::framework::zlink_framework_options_t &options) {
            options.services ()
              .add_singleton<support_conversation_allocator_t> ()
              .add_singleton<agent_availability_directory_t> ()
              .add_singleton<agent_assignment_service_t, agent_availability_directory_t> ()
              .add_singleton<support_actor_directory_t> ();
            options.handlers ()
              .add<ensure_support_user_actor_handler_t> ("support")
              .add<allocate_conversation_handler_t> ("support")
              .add<assign_agent_handler_t> ("support")
              .add<remote_actor_packet_handler_t> ("support");
            options.codecs ().add_json ();
            options.use_discovery ().add_registry_endpoint (topology.registry_router_endpoint);
            options.add_client_server_channel (sample_names_t::support_channel)
              .enable_server (topology.support_channel_endpoint)
              .use_handler_group ("support");
            options.add_client_server_channel (sample_names_t::api_channel)
              .enable_client (topology.api_channel_endpoint);
            options.add_spot_mesh (sample_names_t::support_spot_discovery)
              .add_node (sample_names_t::support_spot_node)
              .enable_router (topology.support_router_endpoint, topology.support_entry_rid)
              .enable_actor_gateway ()
              .enable_pub_sub (topology.support_spot_endpoint)
              .attach_channel_client (sample_names_t::api_channel)
              .add_entry_spot<support_entry_spot_t> ()
              .add_spot<conversation_spot_t> (sample_names_t::conversation_spot)
              .add_actor_factory<support_user_actor_factory_t> (sample_names_t::support_actor_type);
        });
        return app;
    }
};

} // namespace zlink::samples::supportchat
