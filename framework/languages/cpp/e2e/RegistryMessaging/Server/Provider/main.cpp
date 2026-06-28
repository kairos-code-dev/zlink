/* SPDX-License-Identifier: MPL-2.0 */

#include "Configuration/provider_options.hpp"
#include "Endpoints/provider_endpoints.hpp"
#include "Handlers/provider_handlers.hpp"
#include "Infrastructure/scenario_state.hpp"

#include "../../Shared/registry_messaging_contracts.hpp"

#include <zlink/framework.hpp>

#include <cstdint>
#include <memory>
#include <string>

namespace e2e = zlink::framework::e2e::registry_messaging;
namespace rm_provider = zlink::framework::e2e::registry_messaging::provider;

namespace
{

void configure_common_codecs (zlink::framework::codec_options_builder_t codecs)
{
    codecs.add_json ();
    codecs.add_json<e2e::profile_request_t,
                    e2e::profile_reply_t,
                    e2e::profile_command_t,
                    e2e::route_ping_t,
                    e2e::route_pong_t,
                    e2e::evidence_entry_t,
                    e2e::evidence_snapshot_t> ();
}

std::string dispatch_reason_name (zlink::framework::dispatch_error_reason_t reason)
{
    switch (reason) {
        case zlink::framework::dispatch_error_reason_t::handler_missing:
            return "handler_missing";
        case zlink::framework::dispatch_error_reason_t::payload_decode_failed:
            return "payload_decode_failed";
        case zlink::framework::dispatch_error_reason_t::handler_exception:
            return "handler_exception";
        case zlink::framework::dispatch_error_reason_t::invalid_frame:
            return "invalid_frame";
        case zlink::framework::dispatch_error_reason_t::reply_path_missing:
            return "reply_path_missing";
        case zlink::framework::dispatch_error_reason_t::unexpected_reply:
            return "unexpected_reply";
    }
    return "unknown";
}

std::string dispatch_action_name (zlink::framework::dispatch_error_action_t action)
{
    switch (action) {
        case zlink::framework::dispatch_error_action_t::drop:
            return "drop";
        case zlink::framework::dispatch_error_action_t::reply_error:
            return "reply_error";
    }
    return "unknown";
}

} // namespace

int main (int argc, char **argv)
{
    const auto options = rm_provider::read_provider_options ();
    auto app = zlink::framework::app_t::create ();
    app.logging ()
      .use_file (options.log_dir + "/" + options.rid + ".log")
      .set_min_level (zlink::framework::log_level_t::debug);
    app.add_zlink_framework ([&] (zlink::framework::zlink_framework_options_t &framework) {
        framework.configure_dispatch ()
          .message_flow (zlink::framework::message_flow_log_mode_t::key_transitions)
          .trace_log_file (options.log_dir + "/" + options.rid + "-flow.log")
          .trace_label ("cpp-rm-" + options.rid);
        auto state =
          std::make_unique<rm_provider::scenario_state_t> (options.rid, options.instance_id);
        auto *state_ptr = state.get ();
        framework.configure_dispatch ().set_message_flow_observer (
          [state_ptr] (const zlink::framework::message_flow_event_t &event) {
              if (event.outcome != zlink::framework::message_flow_outcome_t::error
                  || !event.error_reason || !event.error_action) {
                  return;
              }
              state_ptr->record ("DispatchError",
                                 dispatch_reason_name (*event.error_reason) + ":"
                                   + dispatch_action_name (*event.error_action));
          });
        framework.services ().add_singleton<rm_provider::scenario_state_t> (std::move (state));
        framework.services ().add_transient<rm_provider::route_ping_handler_t,
                                            rm_provider::scenario_state_t> ();
        framework.services ().add_transient<rm_provider::server_weight_handler_t,
                                            zlink::framework::channel_runtime_options_t> ();
        configure_common_codecs (framework.codecs ());
        framework.handlers ()
          .add<rm_provider::profile_request_handler_t> (e2e::handler_group)
          .add_send<rm_provider::profile_command_handler_t> (e2e::handler_group);
        if (!options.embedded_registry_pub.empty () && !options.embedded_registry_router.empty ()) {
            framework.enable_registry (options.embedded_registry_pub,
                                       options.embedded_registry_router);
            for (const auto &peer : options.embedded_registry_peers) {
                framework.add_registry_peer (peer);
            }
        }
        for (const auto &endpoint : rm_provider::split_csv (options.registry_router)) {
            framework.use_discovery ().add_registry_endpoint (endpoint);
        }
        if (!options.api_endpoint.empty ()) {
            auto channel = framework.add_client_server_channel (e2e::api_channel);
            channel.enable_server (options.api_endpoint)
              .set_routing_id (zlink::routing_id_t::from (options.rid));
            if (options.server_weight) {
                channel.server_peer_weight (
                  zlink::peer_weight_t::value (static_cast<std::uint32_t> (*options.server_weight)));
            }
            if (options.max_message_size) {
                channel.server_max_message_size (
                  zlink::byte_size_t::bytes (static_cast<std::int64_t> (*options.max_message_size)));
            }
            channel.use_handler_group (e2e::handler_group);
        }
        if (!options.route_endpoint.empty ()) {
            framework.add_route_mesh (e2e::route_channel)
              .enable_server (options.route_endpoint)
              .set_routing_id (zlink::routing_id_t::from (options.rid))
              .add_request_handler<rm_provider::route_ping_handler_t,
                                   e2e::route_ping_t,
                                   e2e::route_pong_t> ("ScenarioRoutePing",
                                                       &rm_provider::route_ping_handler_t::handle);
        }
        if (!options.http_endpoint.empty ()) {
            framework.http ()
              .listen (options.http_endpoint)
              .map_health ("/health")
              .map_get<rm_provider::evidence_handler_t> ("/evidence")
              .map_post<rm_provider::server_weight_handler_t> ("/admin/server-weight");
        }
    });
    return app.run (argc, argv);
}
