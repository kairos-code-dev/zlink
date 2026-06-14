/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "services/spot/runtime/spot_runtime_routes.hpp"

namespace zlink
{
void spot_runtime_external_routes_t::set (const std::string &peer_endpoint_,
                                          const std::string &route_id_,
                                          const std::string &route_endpoint_)
{
    if (peer_endpoint_.empty ())
        return;

    scoped_lock_t lock (sync);
    if (route_id_.empty ()) {
        route_ids_by_endpoint.erase (peer_endpoint_);
        route_endpoints_by_endpoint.erase (peer_endpoint_);
    } else {
        route_ids_by_endpoint[peer_endpoint_] = route_id_;
        route_endpoints_by_endpoint[peer_endpoint_] =
          route_endpoint_.empty () ? peer_endpoint_ : route_endpoint_;
    }
}

bool spot_runtime_external_routes_t::matches (const std::string &peer_endpoint_,
                                              const std::string &route_id_,
                                              const std::string &route_endpoint_) const
{
    if (peer_endpoint_.empty () || route_id_.empty ())
        return false;

    scoped_lock_t lock (sync);
    std::map<std::string, std::string>::const_iterator route_it =
      route_ids_by_endpoint.find (peer_endpoint_);
    if (route_it == route_ids_by_endpoint.end () || route_it->second != route_id_)
        return false;

    const std::string expected_endpoint =
      route_endpoint_.empty () ? peer_endpoint_ : route_endpoint_;
    std::map<std::string, std::string>::const_iterator endpoint_it =
      route_endpoints_by_endpoint.find (peer_endpoint_);
    return endpoint_it != route_endpoints_by_endpoint.end ()
           && endpoint_it->second == expected_endpoint;
}

std::string spot_runtime_external_routes_t::erase (const std::string &peer_endpoint_)
{
    scoped_lock_t lock (sync);
    std::string route_endpoint;
    std::map<std::string, std::string>::const_iterator endpoint_it =
      route_endpoints_by_endpoint.find (peer_endpoint_);
    if (endpoint_it != route_endpoints_by_endpoint.end ())
        route_endpoint = endpoint_it->second;
    route_ids_by_endpoint.erase (peer_endpoint_);
    route_endpoints_by_endpoint.erase (peer_endpoint_);
    return route_endpoint;
}

std::vector<std::string> spot_runtime_external_routes_t::clear ()
{
    std::vector<std::string> route_endpoints;
    scoped_lock_t lock (sync);
    for (std::map<std::string, std::string>::const_iterator it =
           route_endpoints_by_endpoint.begin ();
         it != route_endpoints_by_endpoint.end (); ++it) {
        if (!it->second.empty ())
            route_endpoints.push_back (it->second);
    }
    route_ids_by_endpoint.clear ();
    route_endpoints_by_endpoint.clear ();
    return route_endpoints;
}

size_t spot_runtime_external_routes_t::size () const
{
    scoped_lock_t lock (sync);
    return route_ids_by_endpoint.size ();
}

std::vector<std::string> spot_runtime_external_routes_t::route_ids_for_destination (
  const std::string &destination_node_rid_) const
{
    std::vector<std::string> route_ids;
    scoped_lock_t lock (sync);
    if (!destination_node_rid_.empty ())
        route_ids.push_back (destination_node_rid_);
    for (std::map<std::string, std::string>::const_iterator it =
           route_ids_by_endpoint.begin ();
         it != route_ids_by_endpoint.end (); ++it) {
        if (it->second == destination_node_rid_) {
            return route_ids;
        }
    }
    for (std::map<std::string, std::string>::const_iterator it =
           route_ids_by_endpoint.begin ();
         it != route_ids_by_endpoint.end (); ++it) {
        if (!it->second.empty ())
            route_ids.push_back (it->second);
    }
    return route_ids;
}
}
