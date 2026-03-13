/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_GATEWAY_RUNTIME_HPP_INCLUDED__
#define __ZLINK_GATEWAY_RUNTIME_HPP_INCLUDED__

#include "services/common/service_runtime_base.hpp"
#include "services/gateway/gateway.hpp"

#include "utils/clock.hpp"
#include "utils/atomic_counter.hpp"
#include "utils/stdint.hpp"

#include <map>
#include <set>
#include <string>

namespace zlink
{
class socket_base_t;
class gateway_t;

struct gateway_runtime_t
{
    explicit gateway_runtime_t (gateway_t *owner_);

    struct gateway_peer_report_t
    {
        std::string service_name;
        std::string peer_endpoint;
        zlink_routing_id_t peer_routing_id;
        uint32_t weight;
        uint64_t connected_since_ms;

        gateway_peer_report_t () : weight (0), connected_since_ms (0)
        {
            peer_routing_id.size = 0;
        }
    };

    gateway_t *owner;
    service_runtime_base_t lifecycle;
    void *monitor_socket;
    socket_base_t *router_socket;
    atomic_counter_t stop;
    uint64_t refresh_task_id;
    std::map<std::string, gateway_service_pool_t> pools;
    std::map<std::string, gateway_manual_route_t> manual_routes;
    std::string last_service_name;
    gateway_service_pool_t *last_pool;
    std::map<std::string, std::string> endpoint_to_service;
    std::map<std::string, std::string> routing_id_to_service;
    std::set<std::string> ready_endpoints;
    std::set<std::string> inflight_endpoints;
    std::map<std::string, std::string> inflight_rid_by_endpoint;
    std::map<std::string, uint64_t> rid_connect_not_before_ms;
    std::set<std::string> down_endpoints;
    std::map<std::string, uint64_t> down_until_ms;
    std::map<std::string, gateway_peer_report_t> ready_peer_reports;
    uint64_t next_gateway_peer_report_ms;
    bool force_refresh_all;
    std::set<std::string> pending_updates;
    clock_t clock;
};
}

#endif
