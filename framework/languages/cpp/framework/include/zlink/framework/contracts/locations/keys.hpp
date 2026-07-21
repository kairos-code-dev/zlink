/* SPDX-License-Identifier: FSL-1.1-ALv2 */
#pragma once

#include <zlink/Contracts/Core/routing_id.hpp>
#include <zlink/framework/contracts/locations/rows.hpp>

#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace zlink::framework
{

struct peer_location_key_t
{
    location_auto_connect_type_t auto_connect_type = location_auto_connect_type_t::invalid;
    std::string mesh_name;
    location_role_t role = location_role_t::invalid;
    std::optional<zlink::routing_id_t> node_rid;
    std::optional<std::string> endpoint;
};

struct spot_location_key_t
{
    std::string mesh_name;
    zlink::routing_id_t spot_rid = zlink::routing_id_t::from (std::uint32_t{0});
};

struct actor_location_key_t
{
    std::string mesh_name;
    std::string actor_id;
};

struct route_location_key_t
{
    route_kind_t route_kind = route_kind_t::invalid;
    std::string route_key;
};

using location_key_t =
  std::variant<peer_location_key_t, spot_location_key_t, actor_location_key_t, route_location_key_t>;

struct peer_location_filter_t
{
    std::optional<location_auto_connect_type_t> auto_connect_type;
    std::optional<std::string> mesh_name;
    std::optional<location_role_t> role;
    std::optional<zlink::routing_id_t> node_rid;
    std::optional<std::string> endpoint;
};

struct spot_location_filter_t
{
    std::optional<std::string> mesh_name;
    std::optional<std::string> spot_type;
    std::optional<zlink::routing_id_t> node_rid;
    std::optional<zlink::spot_kind> spot_kind;
};

struct actor_location_filter_t
{
    std::optional<std::string> mesh_name;
    std::optional<std::string> actor_type;
    std::optional<zlink::routing_id_t> owner_node_rid;
    std::optional<zlink::routing_id_t> spot_rid;
    std::optional<zlink::spot_kind> spot_kind;
};

struct route_location_filter_t
{
    std::optional<route_kind_t> route_kind;
    std::optional<zlink::routing_id_t> owner_node_rid;
    std::optional<std::string> owner_id;
};

struct location_page_request_t
{
    int page_size = 0;
    std::optional<std::string> continuation_token;
};

template <typename T> struct location_page_t
{
    std::vector<T> items;
    std::optional<std::string> continuation_token;
};

} // namespace zlink::framework
