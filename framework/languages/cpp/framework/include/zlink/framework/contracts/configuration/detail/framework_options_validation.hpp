/* SPDX-License-Identifier: FSL-1.1-ALv2 */
#pragma once

#include <zlink/framework/contracts/configuration/detail/framework_options_state.hpp>

#include <cmath>
#include <set>
#include <string>

namespace zlink::framework::detail
{

inline void validate_dispatch_options (const dispatch_options_t &options)
{
    if (std::isnan (options.diagnostics.sample_rate ()) || options.diagnostics.sample_rate () < 0.0
        || options.diagnostics.sample_rate () > 1.0) {
        throw framework_exception_t (
          framework_error_kind_t::request_protocol_error,
          "dispatch diagnostics sample rate must be between 0.0 and 1.0");
    }
}

inline void validate_framework_options (const framework_options_state_t &options,
                                        const handler_group_options_state_t &handler_groups)
{
    validate_dispatch_options (options.dispatch);
    if (options.has_location_store_instance && options.use_in_memory_location_stores) {
        throw framework_exception_t (
          framework_error_kind_t::request_protocol_error,
          "add_location_store registers every store role at once and cannot be combined with "
          "use_in_memory_location_stores.");
    }
    for (const auto &channel_name : options.client_server_channels) {
        if (!options.client_server_channels_with_server.contains (channel_name)
            && !options.client_server_channels_with_client.contains (channel_name)) {
            throw framework_exception_t (framework_error_kind_t::request_protocol_error,
                                         "client/server channel '" + channel_name
                                           + "' must enable server or client capability");
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
    for (const auto &channel_name : options.route_mesh_channels) {
        if (!options.route_mesh_channels_with_bind.contains (channel_name)
            && !options.route_mesh_channels_with_client.contains (channel_name)) {
            throw framework_exception_t (framework_error_kind_t::request_protocol_error,
                                         "route mesh channel '" + channel_name
                                           + "' must enable server or client capability");
        }
    }
    for (const auto &stream_node_name : options.stream_nodes) {
        if (!options.stream_nodes_with_bind.contains (stream_node_name)) {
            throw framework_exception_t (framework_error_kind_t::request_protocol_error,
                                         "STREAM node '" + stream_node_name
                                           + "' must configure a bind endpoint");
        }
        if (!options.stream_nodes_with_session.contains (stream_node_name)) {
            throw framework_exception_t (framework_error_kind_t::request_protocol_error,
                                         "STREAM node '" + stream_node_name
                                           + "' must register a packet session");
        }
    }
    for (const auto &channel_name : options.client_server_channels_with_server) {
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
