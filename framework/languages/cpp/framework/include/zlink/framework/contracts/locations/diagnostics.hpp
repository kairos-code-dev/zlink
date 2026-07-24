/* SPDX-License-Identifier: FSL-1.1-ALv2 */
#pragma once

#include <zlink/framework/contracts/locations/keys.hpp>

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>

namespace zlink::framework
{

struct location_runtime_status_t
{
    bool store_healthy = false;
    bool watch_enabled = false;
    std::chrono::milliseconds polling_interval{0};
    std::optional<std::chrono::system_clock::time_point> last_refresh_at;
    std::optional<std::string> last_error;
    bool owner_lease_healthy = false;
    std::optional<std::chrono::system_clock::time_point> owner_lease_renewed_at;
};

enum class location_topology_state_t
{
    discovered = 1,
    connecting = 2,
    ready = 3,
    lost = 4,
    error = 5,
    stopped = 6
};

struct location_topology_filter_t
{
    std::optional<location_kind_t> kind;
    std::optional<std::string> mesh_name;
    std::optional<location_role_t> role;
    std::optional<zlink::routing_id_t> node_rid;
    std::optional<location_topology_state_t> state;
};

struct location_topology_entry_t
{
    location_kind_t kind = location_kind_t::peer;
    std::optional<std::string> mesh_name;
    std::optional<location_role_t> role;
    std::optional<zlink::routing_id_t> node_rid;
    std::optional<std::string> spot_id;
    std::optional<std::string> actor_id;
    std::optional<std::string> endpoint;
    location_topology_state_t state = location_topology_state_t::discovered;
    std::uint32_t desired_count = 0;
    std::uint32_t ready_count = 0;
    int error_code = 0;
    std::chrono::system_clock::time_point updated_at{};
};

struct location_service_summary_filter_t
{
    std::optional<std::string> mesh_name;
    std::optional<location_auto_connect_type_t> auto_connect_type;
    std::optional<location_role_t> role;
};

struct location_service_summary_t
{
    std::string mesh_name;
    location_auto_connect_type_t auto_connect_type = location_auto_connect_type_t::invalid;
    location_role_t role = location_role_t::invalid;
    std::uint32_t total_count = 0;
    std::uint32_t ready_count = 0;
    std::uint32_t lost_count = 0;
    std::uint32_t error_count = 0;
};

} // namespace zlink::framework
