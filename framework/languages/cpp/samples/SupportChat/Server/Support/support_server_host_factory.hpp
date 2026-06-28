/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../Configuration/sample_configuration.hpp"
#include "../Configuration/sample_names.hpp"
#include "../Configuration/sample_topology.hpp"
#include "../../Shared/Contracts/codecs.hpp"
#include "../../Shared/Contracts/messages.hpp"
#include "../host_support.hpp"
#include "../sample_log_dir.hpp"
#include "Infrastructure/ZLink/Actors/support_actor_directory.hpp"
#include "Infrastructure/ZLink/Actors/support_user_actor_factory.hpp"
#include "Infrastructure/ZLink/Handlers/allocate_conversation_handler.hpp"
#include "Infrastructure/ZLink/Handlers/assign_agent_handler.hpp"
#include "Infrastructure/ZLink/Handlers/ensure_support_user_actor_handler.hpp"
#include "Infrastructure/ZLink/Spots/ConversationSpot/Notifications/conversation_notification_publisher.hpp"
#include "Infrastructure/ZLink/Spots/ConversationSpot/conversation_spot.hpp"
#include "Infrastructure/ZLink/Spots/EntrySpot/support_entry_spot.hpp"
#include "Application/ConversationAssignment/agent_assignment_service.hpp"
#include "Application/ConversationAssignment/agent_availability_directory.hpp"
#include "Application/ConversationAssignment/support_conversation_allocator.hpp"

#include <memory>

namespace zlink::samples::supportchat
{

using namespace framework;

class support_server_host_factory_t
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
        auto notifications = std::make_shared<conversation_notification_publisher_t> ();
        conversation_spot_t::use_notification_publisher (notifications);
        if (auto_stop) {
            app.add_hosted_service (std::make_unique<stop_after_start_service_t> (app));
        }
        app.add_zlink_framework ([&] (zlink_framework_options_t &options) {
            options.configure_dispatch ()
              .message_flow (message_flow_log_mode_t::key_transitions)
              .trace_log_file (flow_log_path ("support"))
              .trace_label ("supportchat-support");
            options.services ()
              .add_singleton<support_conversation_allocator_t> ()
              .add_singleton<agent_availability_directory_t> ()
              .add_singleton<agent_assignment_service_t, agent_availability_directory_t> ()
              .add_singleton<support_actor_directory_t> ();
            options.codecs ().use (support_chat_json_codec ());
            options.use_discovery ().add_registry_endpoint (topology.registry_router_endpoint);
            options.add_client_server_channel (sample_names_t::support_channel)
              .enable_server (topology.support_channel_endpoint)
              .use_handler_group ("support");
            options.add_client_server_channel (sample_names_t::api_channel).enable_client ();
            options.add_route_mesh (sample_names_t::actor_session_route_channel)
              .enable_server (topology.support_actor_route_endpoint)
              .set_routing_id (zlink::routing_id_t::from (sample_names_t::support_spot_node))
              .enable_client (topology.session_actor_route_endpoint);
            options.add_spot_mesh (sample_names_t::support_spot_discovery)
              .use_registry_spot_resolver (sample_names_t::actor_session_route_channel)
              .set_routing_id (zlink::routing_id_t::from (sample_names_t::support_spot_node))
              .enable_router (topology.support_router_endpoint)
              .enable_pub_sub (topology.support_spot_endpoint)
              .add_spot<conversation_spot_t> (sample_names_t::conversation_spot)
              .add_entry_spot<support_entry_spot_t> ()
              .add_actor_factory<support_user_actor_factory_t> (sample_names_t::support_actor_type);
            options.handlers ()
              .group ("support")
              .add<ensure_support_user_actor_handler_t> ()
              .add<allocate_conversation_handler_t> ()
              .add<assign_agent_handler_t> ();
        });
        return app;
    }
};

} // namespace zlink::samples::supportchat
