/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_SPOT_RUNTIME_ROUTES_HPP_INCLUDED__
#define __ZLINK_SPOT_RUNTIME_ROUTES_HPP_INCLUDED__

#include "utils/mutex.hpp"

#include <map>
#include <string>
#include <vector>

namespace zlink
{
class spot_runtime_external_routes_t
{
  public:
    void set (const std::string &peer_endpoint_,
              const std::string &route_id_,
              const std::string &route_endpoint_);
    bool matches (const std::string &peer_endpoint_,
                  const std::string &route_id_,
                  const std::string &route_endpoint_) const;
    std::string erase (const std::string &peer_endpoint_);
    std::vector<std::string> clear ();
    size_t size () const;
    std::vector<std::string> route_ids_for_destination (
      const std::string &destination_node_rid_) const;

  private:
    mutable mutex_t sync;
    std::map<std::string, std::string> route_ids_by_endpoint;
    std::map<std::string, std::string> route_endpoints_by_endpoint;
};
}

#endif
