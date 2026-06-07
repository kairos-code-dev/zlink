/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <zlink/framework/contracts/configuration/detail/framework_options_state.hpp>

#include <cmath>
#include <set>
#include <string>

namespace zlink::framework::detail
{

inline void validate_dispatch_options (const dispatch_options_t &options)
{
    if (options.unhandled.send == unhandled_dispatch_action_t::reply_error) {
        throw framework_exception_t (
          framework_error_kind_t::request_protocol_error,
          "unhandled send dispatch cannot use reply_error because send has no reply path");
    }
    if (options.unhandled.publish == unhandled_dispatch_action_t::reply_error) {
        throw framework_exception_t (
          framework_error_kind_t::request_protocol_error,
          "unhandled publish dispatch cannot use reply_error because publish has no reply path");
    }
    if (std::isnan (options.diagnostics.sample_rate) || options.diagnostics.sample_rate < 0.0
        || options.diagnostics.sample_rate > 1.0) {
        throw framework_exception_t (
          framework_error_kind_t::request_protocol_error,
          "dispatch diagnostics sample rate must be between 0.0 and 1.0");
    }
}

inline void validate_framework_options (const framework_options_state_t &options,
                                        const handler_group_options_state_t &handler_groups)
{
    if (!options.discovery_backed_capabilities.empty ()
        && options.registry_discovery_endpoints.empty ()) {
        throw framework_exception_t (framework_error_kind_t::request_protocol_error,
                                     *options.discovery_backed_capabilities.begin ()
                                       + " requires registry discovery or a manual endpoint");
    }
    for (const auto &channel_name : options.client_server_channels) {
        if (!options.client_server_channels_with_server.contains (channel_name)
            && !options.client_server_channels_with_client.contains (channel_name)) {
            throw framework_exception_t (framework_error_kind_t::request_protocol_error,
                                         "client/server channel '" + channel_name
                                           + "' must enable server or client capability");
        }
    }
    for (const auto &[channel_name, _] : options.client_server_spot_route_egress_targets) {
        if (!options.client_server_channels.contains (channel_name)) {
            throw framework_exception_t (framework_error_kind_t::request_protocol_error,
                                         "client/server channel '" + channel_name
                                           + "' routed SPOT egress is not registered");
        }
        if (!options.client_server_channels_with_client.contains (channel_name)) {
            throw framework_exception_t (framework_error_kind_t::request_protocol_error,
                                         "client/server channel '" + channel_name
                                           + "' routed SPOT egress requires client capability");
        }
    }
    for (const auto &channel_name : options.fanout_channels) {
        if (!options.fanout_channels_with_publisher.contains (channel_name)
            && !options.fanout_channels_with_subscriber.contains (channel_name)) {
            throw framework_exception_t (framework_error_kind_t::request_protocol_error,
                                         "fanout channel '" + channel_name
                                           + "' must enable publisher or subscriber capability");
        }
    }
    for (const auto &channel_name : options.dealer_mesh_channels) {
        if (!options.dealer_mesh_channels_with_peer_path.contains (channel_name)) {
            throw framework_exception_t (framework_error_kind_t::request_protocol_error,
                                         "dealer mesh channel '" + channel_name
                                           + "' requires a bind or connect endpoint");
        }
    }
    for (const auto &channel_name : options.route_mesh_channels) {
        if (!options.route_mesh_channels_with_bind.contains (channel_name)) {
            throw framework_exception_t (framework_error_kind_t::request_protocol_error,
                                         "route mesh channel '" + channel_name
                                           + "' requires a bind endpoint");
        }
    }
    for (const auto &spot_node_name : options.spot_nodes) {
        if (!options.spot_nodes_with_runtime_capability.contains (spot_node_name)) {
            throw framework_exception_t (framework_error_kind_t::request_protocol_error,
                                         "SPOT node '" + spot_node_name
                                           + "' must enable router or pub/sub capability");
        }
    }
    for (const auto &[spot_node_name, channel_names] : options.attached_channel_clients_by_node) {
        for (const auto &channel_name : channel_names) {
            if (!options.client_server_channels.contains (channel_name)) {
                throw framework_exception_t (framework_error_kind_t::request_protocol_error,
                                             "SPOT node '" + spot_node_name
                                               + "' attached channel client '" + channel_name
                                               + "' is not registered");
            }
            if (options.registry_discovery_endpoints.empty ()
                && !has_manual_connections (
                  options.attached_channel_client_manual_connections_by_node, spot_node_name,
                  channel_name)) {
                throw framework_exception_t (
                  framework_error_kind_t::request_protocol_error,
                  "SPOT node '" + spot_node_name + "' attached channel client '" + channel_name
                    + "' requires registry discovery or manual connections");
            }
        }
    }
    std::set<std::string> attached_publisher_channels;
    for (const auto &[spot_node_name, channel_names] : options.attached_publishers_by_node) {
        if (!options.spot_nodes_with_pub_sub.contains (spot_node_name)) {
            throw framework_exception_t (
              framework_error_kind_t::request_protocol_error,
              "SPOT node '" + spot_node_name
                + "' attaches publisher clients but must enable pub/sub capability");
        }
        for (const auto &channel_name : channel_names) {
            if (!attached_publisher_channels.insert (channel_name).second) {
                throw framework_exception_t (framework_error_kind_t::request_protocol_error,
                                             "duplicate SPOT publisher client channel '"
                                               + channel_name + "'");
            }
            if (!options.fanout_channels.contains (channel_name)) {
                throw framework_exception_t (framework_error_kind_t::request_protocol_error,
                                             "SPOT node '" + spot_node_name + "' publisher client '"
                                               + channel_name + "' is not registered");
            }
            if (!options.fanout_channels_with_publisher.contains (channel_name)) {
                throw framework_exception_t (framework_error_kind_t::request_protocol_error,
                                             "SPOT node '" + spot_node_name + "' publisher client '"
                                               + channel_name + "' requires publisher capability");
            }
        }
    }
    for (const auto &[spot_node_name, channel_names] :
         options.accepted_spot_route_channels_by_node) {
        if (!options.spot_nodes_with_router.contains (spot_node_name)) {
            throw framework_exception_t (framework_error_kind_t::request_protocol_error,
                                         "SPOT node '" + spot_node_name
                                           + "' accepts routes but must enable router capability");
        }
        for (const auto &channel_name : channel_names) {
            const auto regular_channel = options.client_server_channels.contains (channel_name);
            const auto route_channel = options.route_mesh_channels.contains (channel_name);
            if (regular_channel && route_channel) {
                throw framework_exception_t (
                  framework_error_kind_t::request_protocol_error,
                  "SPOT route channel '" + channel_name
                    + "' is ambiguous because both a client/server channel and a route mesh "
                      "channel use the same name");
            }
            if (options.fanout_channels.contains (channel_name)
                || options.dealer_mesh_channels.contains (channel_name)) {
                throw framework_exception_t (
                  framework_error_kind_t::request_protocol_error,
                  "SPOT route channel '" + channel_name
                    + "' must be a client/server channel or a route mesh channel");
            }
            if (!regular_channel && !route_channel) {
                throw framework_exception_t (framework_error_kind_t::request_protocol_error,
                                             "SPOT route channel '" + channel_name
                                               + "' is not registered");
            }
            if (options.registry_discovery_endpoints.empty ()
                && !has_manual_connections (options.accepted_spot_route_manual_connections_by_node,
                                            spot_node_name, channel_name)) {
                throw framework_exception_t (
                  framework_error_kind_t::request_protocol_error,
                  "SPOT route channel '" + channel_name + "' on node '" + spot_node_name
                    + "' requires registry discovery or manual connections");
            }
        }
    }
    for (const auto &channel_name : options.client_server_channels_with_server) {
        if (options.accepted_spot_route_channels.contains (channel_name)) {
            continue;
        }
        if (!handler_groups.channel_exposes_any (
              channel_name, {handler_group_kind_t::request, handler_group_kind_t::send})) {
            throw framework_exception_t (framework_error_kind_t::request_protocol_error,
                                         "client/server channel '" + channel_name
                                           + "' server must map a request or send handler group");
        }
    }
    for (const auto &channel_name : options.fanout_channels_with_subscriber) {
        if (!handler_groups.channel_exposes_any (channel_name, {handler_group_kind_t::publish})) {
            throw framework_exception_t (framework_error_kind_t::request_protocol_error,
                                         "fanout channel '" + channel_name
                                           + "' subscriber must map a publish handler group");
        }
    }
}

} // namespace zlink::framework::detail
