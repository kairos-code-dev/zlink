/* SPDX-License-Identifier: FSL-1.1-ALv2 */
#pragma once

#include <zlink/framework/contracts/dispatch/task.hpp>
#include <zlink/framework/contracts/locations/diagnostics.hpp>
#include <zlink/framework/contracts/locations/maintenance_stores.hpp>
#include <zlink/framework/contracts/locations/watch.hpp>
#include <zlink/framework/contracts/locations/writes.hpp>

namespace zlink::framework
{

class peer_location_store_t
{
  public:
    virtual ~peer_location_store_t () = default;
    virtual task_t<location_write_result_t> update_peer (peer_location_t peer,
                                                         location_write_intent_t intent) = 0;
    virtual task_t<location_write_result_t> remove_peer (peer_location_key_t key,
                                                         location_owner_token_t owner) = 0;
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
    virtual task_t<std::optional<route_location_t>> resolve_route (route_location_key_t key) = 0;
    virtual task_t<location_page_t<route_location_t>>
    list_routes (route_location_filter_t filter, location_page_request_t page = {}) = 0;
};

class owner_lease_store_t
{
  public:
    virtual ~owner_lease_store_t () = default;
    virtual task_t<owner_lease_renewal_t> renew_owner_lease (
      std::string owner_id, zlink::routing_id_t node_rid, std::chrono::milliseconds lease_ttl) = 0;
    virtual task_t<bool> remove_owner_lease (std::string owner_id) = 0;
    virtual task_t<owner_lease_snapshot_t> list_owner_leases () = 0;
};

class location_store_t : public peer_location_store_t,
                         public spot_location_store_t,
                         public actor_location_store_t,
                         public route_location_store_t,
                         public owner_lease_store_t,
                         public authority_store_t,
                         public object_creation_store_t,
                         public relocation_capacity_store_t
{
  public:
    ~location_store_t () override = default;
    virtual task_t<std::int64_t> remove_all_by_owner (std::string owner_id) = 0;
};

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

} // namespace zlink::framework
