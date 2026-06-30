/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../Configuration/registry_options.hpp"

#include "../../../Shared/registry_messaging_contracts.hpp"

#include <zlink/framework.hpp>

#include <utility>

namespace zlink::framework::e2e::registry_messaging::registry
{

class topology_handler_t
{
  public:
    using dependency_types = zlink::framework::dependency_list_t<registry_options_t>;

    explicit topology_handler_t (registry_options_t &options) : _options (options) {}

    zlink::framework::http_response_t handle (const zlink::framework::http_request_t &)
    {
        zlink::framework::registry_query_client_t query;
        auto connected = query.connect (_options.router_endpoint);
        if (!connected.has_value ()) {
            zlink::framework::http_response_t response;
            response.status = 503;
            response.body = R"({"error":"registry query connect failed"})";
            return response;
        }

        zlink::framework::topology_filter_t filter;
        filter.name = api_channel;
        auto topology = query.topology (filter);
        if (!topology.has_value ()) {
            zlink::framework::http_response_t response;
            response.status = 503;
            response.body = R"({"error":"registry topology query failed"})";
            return response;
        }

        nlohmann::json body = nlohmann::json::array ();
        for (const auto &entry : topology.value ()) {
            body.push_back ({{"node_name", entry.node_name},
                             {"kind", entry.kind == zlink::framework::service_kind_t::channel
                                        ? "channel"
                                        : "other"},
                             {"role", entry.role == zlink::framework::service_role_t::server
                                        ? "server"
                                        : "other"},
                             {"name", entry.name},
                             {"state", entry.state == zlink::framework::topology_state_t::active
                                         ? "active"
                                         : "other"},
                             {"endpoint", entry.endpoint},
                             {"routing_id", entry.routing_id ? entry.routing_id->to_string () : ""}});
        }

        zlink::framework::http_response_t response;
        response.body = body.dump ();
        return response;
    }

  private:
    registry_options_t _options;
};

} // namespace zlink::framework::e2e::registry_messaging::registry
