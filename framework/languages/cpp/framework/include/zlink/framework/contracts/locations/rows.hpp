/* SPDX-License-Identifier: FSL-1.1-ALv2 */
#pragma once

#include <zlink/Contracts/Core/routing_id.hpp>
#include <zlink/framework/contracts/actors/actor.hpp>
#include <zlink/framework/contracts/locations/spot_kind.hpp>
#include <zlink/framework/contracts/locations/values.hpp>

#include <chrono>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace zlink::framework
{

struct peer_location_t
{
    location_auto_connect_type_t auto_connect_type = location_auto_connect_type_t::invalid;
    std::string mesh_name;
    std::optional<zlink::routing_id_t> node_rid;
    location_role_t role = location_role_t::invalid;
    std::string endpoint;
    std::uint32_t weight = 0;
    std::int64_t value = 0;
    std::map<std::string, std::string> metadata;
    std::vector<std::string> capabilities;
    std::string owner_id;
    std::int64_t generation = 0;
    std::chrono::system_clock::time_point updated_at{};
    /* Typed draining marker (graceful-drain-handoff §3.1): keeps the
     * connection while excluding this peer from placement decisions. Lease
     * renew and registration writers preserve the current value. */
    bool draining = false;
};

struct spot_location_t
{
    std::string mesh_name;
    zlink::routing_id_t spot_rid = zlink::routing_id_t::from (std::uint32_t{0});
    std::uint64_t spot_generation = 0;
    std::optional<std::string> spot_type;
    zlink::routing_id_t node_rid = zlink::routing_id_t::from (std::uint32_t{0});
    zlink::spot_kind spot_kind = zlink::spot_kind::invalid;
    std::optional<std::string> route_endpoint;
    std::string owner_id;
    std::int64_t generation = 0;
    std::chrono::system_clock::time_point updated_at{};
};

struct actor_location_t
{
    std::string mesh_name;
    std::string actor_id;
    std::string actor_type;
    actor_ref_t actor_ref;
    zlink::routing_id_t owner_node_rid = zlink::routing_id_t::from (std::uint32_t{0});
    std::uint64_t owner_node_generation = 0;
    zlink::routing_id_t spot_rid = zlink::routing_id_t::from (std::uint32_t{0});
    std::uint64_t spot_generation = 0;
    zlink::spot_kind spot_kind = zlink::spot_kind::invalid;
    std::uint64_t membership_epoch = 0;
    std::string owner_id;
    std::chrono::system_clock::time_point updated_at{};
};

struct route_location_t
{
    route_kind_t route_kind = route_kind_t::invalid;
    std::string route_key;
    zlink::routing_id_t owner_node_rid = zlink::routing_id_t::from (std::uint32_t{0});
    std::string owner_id;
    std::int64_t generation = 0;
    std::vector<std::uint8_t> value;
    std::chrono::system_clock::time_point updated_at{};
};

struct owner_lease_t
{
    std::string owner_id;
    zlink::routing_id_t node_rid = zlink::routing_id_t::from (std::uint32_t{0});
    std::chrono::system_clock::time_point lease_expires_at{};
    std::chrono::system_clock::time_point updated_at{};
};

struct owner_lease_snapshot_t
{
    std::vector<owner_lease_t> leases;
    std::chrono::system_clock::time_point store_now{};
};

} // namespace zlink::framework
