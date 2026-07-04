/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <zlink/framework/contracts/locations/resolvers.hpp>

namespace zlink::framework
{

class location_runtime_query_t
{
  public:
    virtual ~location_runtime_query_t () = default;
    virtual task_t<location_runtime_status_t> get_status () = 0;
    virtual task_t<std::vector<peer_location_t>>
    list_peer_locations (peer_location_filter_t filter) = 0;
    virtual task_t<location_page_t<spot_location_t>>
    list_spot_locations (spot_location_filter_t filter, location_page_request_t page = {}) = 0;
    virtual task_t<location_page_t<actor_location_t>>
    list_actor_locations (actor_location_filter_t filter, location_page_request_t page = {}) = 0;
    virtual task_t<location_page_t<route_location_t>>
    list_route_locations (route_location_filter_t filter, location_page_request_t page = {}) = 0;
    virtual task_t<location_page_t<location_topology_entry_t>>
    list_topology (location_topology_filter_t filter, location_page_request_t page = {}) = 0;
    virtual task_t<std::vector<location_service_summary_t>>
    list_service_summaries (location_service_summary_filter_t filter) = 0;
};

} // namespace zlink::framework
