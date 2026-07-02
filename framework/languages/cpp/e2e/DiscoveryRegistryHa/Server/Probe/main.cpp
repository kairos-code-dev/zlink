/* SPDX-License-Identifier: MPL-2.0 */

#include "Configuration/probe_options.hpp"

#include "../../Shared/discovery_registry_ha_contracts.hpp"

#include <zlink/framework.hpp>

#include <algorithm>
#include <chrono>
#include <csignal>
#include <memory>
#include <thread>

namespace drha = zlink::framework::e2e::discovery_registry_ha;
namespace drha_probe = zlink::framework::e2e::discovery_registry_ha::probe;

namespace
{

std::string topology_state_name (zlink::framework::topology_state_t state)
{
    switch (state) {
    case zlink::framework::topology_state_t::active:
        return "active";
    default:
        return "unknown";
    }
}

std::string service_role_name (zlink::framework::service_role_t role)
{
    switch (role) {
    case zlink::framework::service_role_t::server:
        return "server";
    case zlink::framework::service_role_t::client:
        return "client";
    case zlink::framework::service_role_t::publisher:
        return "publisher";
    case zlink::framework::service_role_t::subscriber:
        return "subscriber";
    case zlink::framework::service_role_t::spot_node:
        return "spot_node";
    case zlink::framework::service_role_t::stream_endpoint:
        return "stream_endpoint";
    default:
        return "unknown";
    }
}

class probe_state_t
{
  public:
    explicit probe_state_t (std::string registry_router_endpoint) :
        _registry_router_endpoint (std::move (registry_router_endpoint))
    {
    }

    drha::topology_snapshot_t topology_snapshot () const
    {
        zlink::framework::registry_query_client_t query;
        auto connected = query.connect (_registry_router_endpoint);
        if (!connected.has_value ()) {
            throw std::runtime_error ("probe registry query connect failed");
        }
        zlink::framework::topology_filter_t filter;
        filter.name = drha::api_channel;
        auto topology = query.topology (filter);
        if (!topology.has_value ()) {
            throw std::runtime_error ("probe registry topology query failed");
        }
        drha::topology_snapshot_t snapshot;
        for (const auto &entry : topology.value ()) {
            const auto routing_id = entry.routing_id ? entry.routing_id->to_string () : "";
            snapshot.entries.push_back (entry.name + "|" + routing_id + "|" + entry.endpoint
                                        + "|" + topology_state_name (entry.state) + "|"
                                        + service_role_name (entry.role));
        }
        std::sort (snapshot.entries.begin (), snapshot.entries.end ());
        return snapshot;
    }

  private:
    std::string _registry_router_endpoint;
};

class topology_snapshot_handler_t
{
  public:
    using dependency_types = zlink::framework::dependency_list_t<probe_state_t>;

    explicit topology_snapshot_handler_t (probe_state_t &state) : _state (state) {}

    zlink::framework::http_response_t handle (const zlink::framework::http_request_t &)
    {
        zlink::framework::http_response_t response;
        response.body = nlohmann::json (_state.topology_snapshot ()).dump ();
        return response;
    }

  private:
    probe_state_t &_state;
};

class topology_ready_wait_handler_t
{
  public:
    using dependency_types = zlink::framework::dependency_list_t<probe_state_t>;
    using request_type = drha::topology_ready_wait_req_t;
    using reply_type = drha::operation_status_t;

    explicit topology_ready_wait_handler_t (probe_state_t &state) : _state (state) {}

    drha::operation_status_t handle (const drha::topology_ready_wait_req_t &request)
    {
        const auto timeout = std::chrono::milliseconds (
          request.timeout_milliseconds <= 0 ? 1 : request.timeout_milliseconds);
        const auto deadline = std::chrono::steady_clock::now () + timeout;
        while (std::chrono::steady_clock::now () < deadline) {
            if (static_cast<int> (_state.topology_snapshot ().entries.size ()) >= request.ready_count) {
                return {.status = "ready"};
            }
            std::this_thread::sleep_for (std::chrono::milliseconds (100));
        }
        throw std::runtime_error ("timed out waiting for probe topology readiness");
    }

  private:
    probe_state_t &_state;
};

class shutdown_handler_t
{
  public:
    zlink::framework::http_response_t handle (const zlink::framework::http_request_t &)
    {
        std::thread ([] {
            std::this_thread::sleep_for (std::chrono::milliseconds (20));
            std::raise (SIGTERM);
        }).detach ();
        zlink::framework::http_response_t response;
        response.body = R"({"status":"stopping"})";
        return response;
    }
};

} // namespace

int main (int argc, char **argv)
{
    const auto options = drha_probe::read_probe_options ();
    auto app = zlink::framework::app_t::create ();
    app.logging ()
      .use_file (options.log_dir + "/probe.log")
      .set_min_level (zlink::framework::log_level_t::debug);
    app.add_zlink_framework ([&] (zlink::framework::zlink_framework_options_t &framework) {
        framework.services ().add_singleton<probe_state_t> (
          std::make_unique<probe_state_t> (options.registry_router_endpoint));
        framework.http ()
          .listen (options.http_endpoint)
          .map_health ("/health")
          .map_get<topology_snapshot_handler_t> ("/registry/topology")
          .map_post<topology_ready_wait_handler_t> ("/registry/topology/wait")
          .map_post<shutdown_handler_t> ("/shutdown");
    });
    return app.run (argc, argv);
}
