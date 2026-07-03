/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <zlink/Contracts/Core/routing_id.hpp>
#include <zlink/Contracts/Service/actor_models.hpp>
#include <zlink/framework/contracts/dispatch/task.hpp>

#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace zlink::framework
{

enum class location_auto_connect_type_t
{
    invalid = 0,
    route_mesh = 1,
    client_server = 2,
    dealer_mesh = 3,
    fanout = 4,
    spot_mesh = 5
};

enum class location_role_t : std::uint16_t
{
    invalid = 0,
    spot = 2,
    router = 3,
    dealer = 4,
    pub = 5,
    sub = 6
};

enum class route_kind_t
{
    invalid = 0,
    actor_session = 1,
    spot_name = 2,
    framework_route = 3
};

enum class location_kind_t
{
    peer = 1,
    spot = 2,
    actor = 3,
    route = 4
};

inline std::string to_canonical_string (location_auto_connect_type_t type)
{
    switch (type) {
        case location_auto_connect_type_t::route_mesh:
            return "route-mesh";
        case location_auto_connect_type_t::client_server:
            return "client-server";
        case location_auto_connect_type_t::dealer_mesh:
            return "dealer-mesh";
        case location_auto_connect_type_t::fanout:
            return "fanout";
        case location_auto_connect_type_t::spot_mesh:
            return "spot-mesh";
        default:
            throw std::invalid_argument ("unknown location auto-connect type");
    }
}

inline std::string to_canonical_string (location_role_t role)
{
    switch (role) {
        case location_role_t::router:
            return "router";
        case location_role_t::dealer:
            return "dealer";
        case location_role_t::pub:
            return "pub";
        case location_role_t::sub:
            return "sub";
        case location_role_t::spot:
            return "spot";
        default:
            throw std::invalid_argument ("unknown location role");
    }
}

inline bool try_parse_location_auto_connect_type (std::string_view value,
                                                  location_auto_connect_type_t &type) noexcept
{
    if (value == "route-mesh") {
        type = location_auto_connect_type_t::route_mesh;
    } else if (value == "client-server") {
        type = location_auto_connect_type_t::client_server;
    } else if (value == "dealer-mesh") {
        type = location_auto_connect_type_t::dealer_mesh;
    } else if (value == "fanout") {
        type = location_auto_connect_type_t::fanout;
    } else if (value == "spot-mesh") {
        type = location_auto_connect_type_t::spot_mesh;
    } else {
        type = location_auto_connect_type_t::invalid;
        return false;
    }
    return true;
}

inline bool try_parse_location_role (std::string_view value, location_role_t &role) noexcept
{
    if (value == "router") {
        role = location_role_t::router;
    } else if (value == "dealer") {
        role = location_role_t::dealer;
    } else if (value == "pub") {
        role = location_role_t::pub;
    } else if (value == "sub") {
        role = location_role_t::sub;
    } else if (value == "spot") {
        role = location_role_t::spot;
    } else {
        role = location_role_t::invalid;
        return false;
    }
    return true;
}

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
};

struct spot_location_t
{
    std::string mesh_name;
    zlink::routing_id_t spot_rid = zlink::routing_id_t::from (std::uint32_t{0});
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
    std::string actor_type;
    std::string actor_id;
    std::string actor_ref;
    zlink::routing_id_t node_rid = zlink::routing_id_t::from (std::uint32_t{0});
    std::int64_t generation = 0;
    zlink::spot_kind location_kind = zlink::spot_kind::invalid;
    std::optional<zlink::routing_id_t> spot_rid;
    zlink::spot_kind spot_kind = zlink::spot_kind::invalid;
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

struct spot_address_t
{
    std::string mesh_name;
    zlink::routing_id_t node_rid = zlink::routing_id_t::from (std::uint32_t{0});
    zlink::routing_id_t spot_rid = zlink::routing_id_t::from (std::uint32_t{0});
};

struct actor_location_key_t
{
    std::string actor_type;
    std::string actor_id;
};

struct route_location_key_t
{
    route_kind_t route_kind = route_kind_t::invalid;
    std::string route_key;
};

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
    std::optional<std::string> actor_type;
    std::optional<zlink::routing_id_t> node_rid;
    std::optional<zlink::routing_id_t> spot_rid;
    std::optional<zlink::spot_kind> location_kind;
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

enum class location_write_intent_t
{
    new_claim = 1,
    renew = 2,
    takeover = 3
};

enum class location_write_status_t
{
    stored = 1,
    ignored_stale = 2,
    rejected_conflict = 3,
    store_unavailable = 4
};

struct location_write_result_t
{
    location_write_status_t status = location_write_status_t::store_unavailable;
    std::int64_t generation = 0;
    std::chrono::system_clock::time_point updated_at{};

    static location_write_result_t stored (std::int64_t generation,
                                           std::chrono::system_clock::time_point updated_at)
    {
        return location_write_result_t{location_write_status_t::stored, generation,
                                       std::move (updated_at)};
    }
};

struct location_owner_token_t
{
    std::string owner_id;
    std::int64_t generation = 0;
};

struct location_watch_filter_t
{
    location_kind_t kind = location_kind_t::peer;
    std::optional<std::string> mesh_name;
    std::optional<route_kind_t> route_kind;
};

enum class location_change_type_t
{
    upserted = 1,
    removed = 2,
    expired = 3
};

struct location_changed_t
{
    location_kind_t kind = location_kind_t::peer;
    std::string location_key;
    location_change_type_t change_type = location_change_type_t::upserted;
    std::int64_t generation = 0;
    std::chrono::system_clock::time_point updated_at{};
};

struct location_change_stamp_scope_t
{
    location_kind_t kind = location_kind_t::peer;
    std::optional<std::string> mesh_name;
};

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
    std::optional<zlink::routing_id_t> spot_rid;
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

class peer_location_store_t
{
  public:
    virtual ~peer_location_store_t () = default;
    virtual task_t<location_write_result_t> update_peer (peer_location_t peer,
                                                         location_write_intent_t intent) = 0;
    virtual task_t<location_write_result_t> remove_peer (peer_location_key_t key,
                                                         location_owner_token_t owner) = 0;
    virtual task_t<std::int64_t> remove_peers_by_owner (std::string owner_id) = 0;
    virtual task_t<std::vector<peer_location_t>> list_peers (peer_location_filter_t filter) = 0;
};

class spot_location_store_t
{
  public:
    virtual ~spot_location_store_t () = default;
    virtual task_t<location_write_result_t> update_spot (spot_location_t spot,
                                                         location_write_intent_t intent) = 0;
    virtual task_t<location_write_result_t> remove_spot (spot_location_key_t key,
                                                         location_owner_token_t owner) = 0;
    virtual task_t<std::int64_t> remove_spots_by_owner (std::string owner_id) = 0;
    virtual task_t<std::optional<spot_location_t>> resolve_spot (spot_location_key_t key) = 0;
    virtual task_t<location_page_t<spot_location_t>>
    list_spots (spot_location_filter_t filter, location_page_request_t page = {}) = 0;
};

class actor_location_store_t
{
  public:
    virtual ~actor_location_store_t () = default;
    virtual task_t<location_write_result_t> update_actor (actor_location_t actor,
                                                          location_write_intent_t intent) = 0;
    virtual task_t<location_write_result_t> remove_actor (actor_location_key_t key,
                                                          location_owner_token_t owner) = 0;
    virtual task_t<std::int64_t> remove_actors_by_owner (std::string owner_id) = 0;
    virtual task_t<std::optional<actor_location_t>> resolve_actor (actor_location_key_t key) = 0;
    virtual task_t<location_page_t<actor_location_t>>
    list_actors (actor_location_filter_t filter, location_page_request_t page = {}) = 0;
};

class route_location_store_t
{
  public:
    virtual ~route_location_store_t () = default;
    virtual task_t<location_write_result_t> update_route (route_location_t route,
                                                          location_write_intent_t intent) = 0;
    virtual task_t<location_write_result_t> remove_route (route_location_key_t key,
                                                          location_owner_token_t owner) = 0;
    virtual task_t<std::int64_t> remove_routes_by_owner (std::string owner_id) = 0;
    virtual task_t<std::optional<route_location_t>> resolve_route (route_location_key_t key) = 0;
    virtual task_t<location_page_t<route_location_t>>
    list_routes (route_location_filter_t filter, location_page_request_t page = {}) = 0;
};

class owner_lease_store_t
{
  public:
    virtual ~owner_lease_store_t () = default;
    virtual task_t<location_write_result_t> renew_owner_lease (
      std::string owner_id, zlink::routing_id_t node_rid, std::chrono::milliseconds lease_ttl) = 0;
    virtual task_t<location_write_result_t> remove_owner_lease (std::string owner_id) = 0;
    virtual task_t<owner_lease_snapshot_t> list_owner_leases () = 0;
};

class location_store_t : public peer_location_store_t,
                         public spot_location_store_t,
                         public actor_location_store_t,
                         public route_location_store_t,
                         public owner_lease_store_t
{
  public:
    ~location_store_t () override = default;
};

using location_watch_callback_t = std::function<void (location_changed_t)>;

class location_watch_store_t
{
  public:
    virtual ~location_watch_store_t () = default;
    virtual task_t<void> watch_locations (location_watch_filter_t filter,
                                          location_watch_callback_t callback) = 0;
};

class location_change_stamp_store_t
{
  public:
    virtual ~location_change_stamp_store_t () = default;
    virtual task_t<std::int64_t> get_change_stamp (location_change_stamp_scope_t scope) = 0;
};

class peer_location_resolver_t
{
  public:
    virtual ~peer_location_resolver_t () = default;
    virtual task_t<std::vector<peer_location_t>> list_peers (peer_location_filter_t filter) = 0;
};

class spot_location_resolver_t
{
  public:
    virtual ~spot_location_resolver_t () = default;
    virtual task_t<std::optional<spot_address_t>>
    resolve_spot_address (std::string mesh_name, zlink::routing_id_t spot_rid) = 0;
};

class actor_location_resolver_t
{
  public:
    virtual ~actor_location_resolver_t () = default;
    virtual task_t<std::optional<spot_address_t>>
    resolve_actor_spot_address (std::string actor_type, std::string actor_id) = 0;
};

class route_location_resolver_t
{
  public:
    virtual ~route_location_resolver_t () = default;
    virtual task_t<std::optional<route_location_t>> resolve_route (route_location_key_t key) = 0;
};

class location_runtime_query_t
{
  public:
    virtual ~location_runtime_query_t () = default;
    virtual task_t<location_runtime_status_t> status () = 0;
    virtual task_t<std::vector<peer_location_t>> list_peers (peer_location_filter_t filter) = 0;
    virtual task_t<location_page_t<spot_location_t>>
    list_spots (spot_location_filter_t filter, location_page_request_t page = {}) = 0;
    virtual task_t<location_page_t<actor_location_t>>
    list_actors (actor_location_filter_t filter, location_page_request_t page = {}) = 0;
    virtual task_t<location_page_t<route_location_t>>
    list_routes (route_location_filter_t filter, location_page_request_t page = {}) = 0;
    virtual task_t<location_page_t<location_topology_entry_t>>
    list_topology (location_topology_filter_t filter, location_page_request_t page = {}) = 0;
    virtual task_t<std::vector<location_service_summary_t>>
    list_service_summaries (location_service_summary_filter_t filter) = 0;
};

struct location_options_t
{
    std::chrono::milliseconds heartbeat_interval{5000};
    std::chrono::milliseconds owner_lease_ttl{15000};
    std::chrono::milliseconds polling_interval{1000};
    int list_page_size = 1000;
    std::chrono::milliseconds store_failure_grace{30000};
};

} // namespace zlink::framework
