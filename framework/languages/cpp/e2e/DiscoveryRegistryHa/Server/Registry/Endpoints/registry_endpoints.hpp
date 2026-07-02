/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../Configuration/registry_options.hpp"

#include "../../../Shared/discovery_registry_ha_contracts.hpp"

#include <zlink/framework.hpp>

#include <chrono>
#include <algorithm>
#include <thread>

namespace zlink::framework::e2e::discovery_registry_ha::registry
{

inline int ready_provider_count (const registry_options_t &options)
{
    zlink::framework::registry_query_client_t query;
    auto connected = query.connect (options.router_endpoint);
    if (!connected.has_value ()) {
        return 0;
    }

    zlink::framework::topology_filter_t filter;
    filter.name = api_channel;
    auto topology = query.topology (filter);
    if (!topology.has_value ()) {
        return 0;
    }

    int ready = 0;
    for (const auto &entry : topology.value ()) {
        if (entry.kind == zlink::framework::service_kind_t::channel
            && entry.role == zlink::framework::service_role_t::server && entry.name == api_channel
            && entry.state == zlink::framework::topology_state_t::active) {
            ++ready;
        }
    }
    return ready;
}

inline std::string topology_state_name (zlink::framework::topology_state_t state)
{
    switch (state) {
    case zlink::framework::topology_state_t::active:
        return "active";
    default:
        return "unknown";
    }
}

inline std::string service_role_name (zlink::framework::service_role_t role)
{
    switch (role) {
    case zlink::framework::service_role_t::server:
        return "server";
    case zlink::framework::service_role_t::client:
        return "client";
    case zlink::framework::service_role_t::publisher:
        return "publisher";
    case zlink::framework::service_role_t::subscriber:
        return "subscriber";
    case zlink::framework::service_role_t::spot_node:
        return "spot_node";
    case zlink::framework::service_role_t::stream_endpoint:
        return "stream_endpoint";
    default:
        return "unknown";
    }
}

inline topology_snapshot_t normalized_topology_snapshot (const registry_options_t &options)
{
    zlink::framework::registry_query_client_t query;
    auto connected = query.connect (options.router_endpoint);
    if (!connected.has_value ()) {
        throw std::runtime_error ("registry topology query connect failed");
    }

    zlink::framework::topology_filter_t filter;
    filter.name = api_channel;
    auto topology = query.topology (filter);
    if (!topology.has_value ()) {
        throw std::runtime_error ("registry topology query failed");
    }

    topology_snapshot_t snapshot;
    for (const auto &entry : topology.value ()) {
        const auto routing_id = entry.routing_id ? entry.routing_id->to_string () : "";
        snapshot.entries.push_back (entry.name + "|" + routing_id + "|" + entry.endpoint
                                    + "|" + topology_state_name (entry.state) + "|"
                                    + service_role_name (entry.role));
    }
    std::sort (snapshot.entries.begin (), snapshot.entries.end ());
    return snapshot;
}

class topology_ready_wait_handler_t
{
  public:
    using dependency_types = zlink::framework::dependency_list_t<registry_options_t>;
    using request_type = topology_ready_wait_req_t;
    using reply_type = operation_status_t;

    explicit topology_ready_wait_handler_t (registry_options_t &options) : _options (options) {}

    operation_status_t handle (const topology_ready_wait_req_t &request)
    {
        const auto timeout = std::chrono::milliseconds (
          request.timeout_milliseconds <= 0 ? 1 : request.timeout_milliseconds);
        const auto deadline = std::chrono::steady_clock::now () + timeout;
        while (std::chrono::steady_clock::now () < deadline) {
            if (ready_provider_count (_options) >= request.ready_count) {
                return {.status = "ready"};
            }
            std::this_thread::sleep_for (std::chrono::milliseconds (100));
        }
        throw std::runtime_error ("timed out waiting for registry topology readiness");
    }

  private:
    registry_options_t _options;
};

class member_endpoint_wait_handler_t
{
  public:
    using dependency_types = zlink::framework::dependency_list_t<registry_options_t>;
    using request_type = member_endpoint_wait_req_t;
    using reply_type = operation_status_t;

    explicit member_endpoint_wait_handler_t (registry_options_t &options) : _options (options) {}

    operation_status_t handle (const member_endpoint_wait_req_t &request)
    {
        const auto timeout = std::chrono::milliseconds (
          request.timeout_milliseconds <= 0 ? 1 : request.timeout_milliseconds);
        const auto deadline = std::chrono::steady_clock::now () + timeout;
        zlink::framework::registry_query_client_t query;
        const auto connected = query.connect (_options.router_endpoint);
        if (!connected.has_value ()) {
            throw std::runtime_error ("registry member query connect failed");
        }
        while (std::chrono::steady_clock::now () < deadline) {
            const auto members = query.member_peers (api_channel);
            if (members.has_value ()) {
                for (const auto &member : members.value ()) {
                    if (member.endpoint == request.endpoint) {
                        return {.status = "ready"};
                    }
                }
            }
            std::this_thread::sleep_for (std::chrono::milliseconds (100));
        }
        throw std::runtime_error ("timed out waiting for registry member endpoint");
    }

  private:
    registry_options_t _options;
};

class registry_peer_count_wait_handler_t
{
  public:
    using dependency_types = zlink::framework::dependency_list_t<registry_options_t>;
    using request_type = registry_peer_count_wait_req_t;
    using reply_type = operation_status_t;

    explicit registry_peer_count_wait_handler_t (registry_options_t &options) : _options (options)
    {
    }

    operation_status_t handle (const registry_peer_count_wait_req_t &request)
    {
        const auto timeout = std::chrono::milliseconds (
          request.timeout_milliseconds <= 0 ? 1 : request.timeout_milliseconds);
        const auto deadline = std::chrono::steady_clock::now () + timeout;
        zlink::framework::registry_query_client_t query;
        const auto connected = query.connect (_options.router_endpoint);
        if (!connected.has_value ()) {
            throw std::runtime_error ("registry status query connect failed");
        }
        while (std::chrono::steady_clock::now () < deadline) {
            const auto status = query.status ();
            if (status.has_value ()
                && status.value ().connected_peer_count
                     == static_cast<std::size_t> (request.connected_peer_count)) {
                return {.status = "ready"};
            }
            std::this_thread::sleep_for (std::chrono::milliseconds (100));
        }
        throw std::runtime_error ("timed out waiting for registry peer count");
    }

  private:
    registry_options_t _options;
};

class topology_snapshot_handler_t
{
  public:
    using dependency_types = zlink::framework::dependency_list_t<registry_options_t>;

    explicit topology_snapshot_handler_t (registry_options_t &options) : _options (options) {}

    zlink::framework::http_response_t handle (const zlink::framework::http_request_t &)
    {
        zlink::framework::http_response_t response;
        response.body = nlohmann::json (normalized_topology_snapshot (_options)).dump ();
        return response;
    }

  private:
    registry_options_t _options;
};

} // namespace zlink::framework::e2e::discovery_registry_ha::registry
