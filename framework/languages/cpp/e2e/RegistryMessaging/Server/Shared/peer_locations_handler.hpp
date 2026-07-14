/* SPDX-License-Identifier: FSL-1.1-ALv2 */
#pragma once

#include "../../Shared/registry_messaging_contracts.hpp"

#include <zlink/framework.hpp>

namespace zlink::framework::e2e::registry_messaging
{

class peer_locations_handler_t
{
  public:
    using dependency_types = dependency_list_t<location_runtime_query_t>;

    explicit peer_locations_handler_t (location_runtime_query_t &locations) :
        _locations (locations)
    {
    }

    http_response_t handle (const http_request_t &)
    {
        const auto peers =
          _locations
            .list_peer_locations (peer_location_filter_t{
              .auto_connect_type = location_auto_connect_type_t::client_server,
              .mesh_name = api_channel})
            .result ()
            .value ();
        auto payload = nlohmann::json::array ();
        for (const auto &peer : peers) {
            payload.push_back (nlohmann::json{
              {"mesh_name", peer.mesh_name},
              {"role", role_name (peer.role)},
              {"node_rid", peer.node_rid ? peer.node_rid->to_string () : std::string{}},
              {"endpoint", peer.endpoint}});
        }
        http_response_t response;
        response.body = payload.dump ();
        return response;
    }

  private:
    static std::string role_name (location_role_t role)
    {
        switch (role) {
            case location_role_t::spot:
                return "spot";
            case location_role_t::router:
                return "router";
            case location_role_t::dealer:
                return "dealer";
            case location_role_t::pub:
                return "pub";
            case location_role_t::sub:
                return "sub";
            default:
                return "invalid";
        }
    }

    location_runtime_query_t &_locations;
};

} // namespace zlink::framework::e2e::registry_messaging
