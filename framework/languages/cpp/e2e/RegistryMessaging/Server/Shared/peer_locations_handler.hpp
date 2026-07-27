/* SPDX-License-Identifier: FSL-1.1-ALv2 */
#pragma once

#include "../../Shared/registry_messaging_contracts.hpp"

#include <zlink/framework.hpp>

namespace zlink::framework::e2e::registry_messaging
{

class peer_locations_handler_t
{
  public:
    using dependency_types =
      dependency_list_t<location_runtime_query_t, location_readiness_t>;

    peer_locations_handler_t (location_runtime_query_t &locations,
                              location_readiness_t &readiness) :
        _locations (locations),
        _readiness (readiness)
    {
    }

    http_response_t handle (const http_request_t &request)
    {
        auto mesh_name = std::string (api_channel);
        const auto requested_mesh = request.query_values.find ("mesh");
        if (requested_mesh != request.query_values.end () && !requested_mesh->second.empty ()) {
            mesh_name = requested_mesh->second;
        }
        const auto peers = _locations.list_mesh_node_descriptors (mesh_name).result ().value ();
        auto payload = nlohmann::json::array ();
        for (const auto &peer : peers.items) {
            const auto ready =
              _readiness
                .is_peer_ready (mesh_name, location_role_t::router, peer.rid)
                .result ()
                .value ();
            payload.push_back (nlohmann::json{
              {"mesh_name", peer.mesh_name},
              {"role", "router"},
              {"node_rid", peer.rid.to_string ()},
              {"endpoint", peer.endpoint},
              {"ready", ready}});
        }
        http_response_t response;
        response.body = payload.dump ();
        return response;
    }

  private:
    location_runtime_query_t &_locations;
    location_readiness_t &_readiness;
};

} // namespace zlink::framework::e2e::registry_messaging
