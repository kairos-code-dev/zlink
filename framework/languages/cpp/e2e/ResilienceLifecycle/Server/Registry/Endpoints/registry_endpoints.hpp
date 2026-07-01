/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../Configuration/registry_options.hpp"
#include "../Infrastructure/evidence_store.hpp"
#include "topology_entry_result.hpp"

#include "../../../Shared/resilience_lifecycle_messages.hpp"

#include <zlink/framework.hpp>

#include <algorithm>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace zlink::framework::e2e::resilience_lifecycle::registry
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

        std::vector<topology_entry_result_t> body;
        for (const auto &entry : topology.value ()) {
            body.push_back (to_topology_entry_result (entry));
        }

        zlink::framework::http_response_t response;
        response.body = nlohmann::json (body).dump ();
        return response;
    }

  private:
    registry_options_t _options;
};

class topology_wait_handler_t
{
  public:
    using dependency_types = zlink::framework::dependency_list_t<registry_options_t>;

    explicit topology_wait_handler_t (registry_options_t &options) : _options (options) {}

    zlink::framework::http_response_t handle (const zlink::framework::http_request_t &request)
    {
        const auto body = nlohmann::json::parse (request.body.empty () ? "{}" : request.body);
        const auto routing_id =
          body.value ("routingId", body.value ("routing_id", std::string{}));
        auto state = body.value ("state", std::string{"Ready"});
        if (state == "active") {
            state = "Ready";
        }
        const auto expected_count =
          body.value ("expectedCount", body.value ("expected_count", 1));
        const auto timeout_ms =
          std::clamp (body.value ("timeoutMilliseconds",
                                  body.value ("timeout_milliseconds", 30000)),
                      1,
                      30000);

        zlink::framework::registry_query_client_t query;
        auto connected = query.connect (_options.router_endpoint);
        if (!connected.has_value ()) {
            zlink::framework::http_response_t response;
            response.status = 503;
            response.body = R"({"error":"registry query connect failed"})";
            return response;
        }

        const auto deadline =
          std::chrono::steady_clock::now () + std::chrono::milliseconds (timeout_ms);
        while (std::chrono::steady_clock::now () < deadline) {
            zlink::framework::topology_filter_t filter;
            filter.name = api_channel;
            auto topology = query.topology (filter);
            if (topology.has_value ()) {
                std::vector<topology_entry_result_t> snapshot;
                for (const auto &entry : topology.value ()) {
                    snapshot.push_back (to_topology_entry_result (entry));
                }
                const auto count =
                  std::count_if (snapshot.begin (), snapshot.end (), [&] (const auto &entry) {
                      return entry.routing_id == routing_id && entry.state == state;
                  });
                if (count == expected_count) {
                    zlink::framework::http_response_t response;
                    response.body = nlohmann::json (snapshot).dump ();
                    return response;
                }
            }
            std::this_thread::sleep_for (std::chrono::milliseconds (250));
        }

        zlink::framework::http_response_t response;
        response.status = 504;
        response.body = nlohmann::json{{"error", "topology did not reach expected state"},
                                       {"routing_id", routing_id},
                                       {"state", state},
                                       {"expected_count", expected_count}}
                          .dump ();
        return response;
    }

  private:
    registry_options_t _options;
};

class shutdown_handler_t
{
  public:
    zlink::framework::http_response_t handle (const zlink::framework::http_request_t &)
    {
        std::thread ([] {
            std::this_thread::sleep_for (std::chrono::milliseconds (50));
            std::raise (SIGTERM);
        }).detach ();
        zlink::framework::http_response_t response;
        response.body = R"({"status":"stopping"})";
        return response;
    }
};

class evidence_handler_t
{
  public:
    using dependency_types = zlink::framework::dependency_list_t<evidence_store_t>;

    explicit evidence_handler_t (evidence_store_t &evidence) : _evidence (evidence) {}

    zlink::framework::http_response_t handle (const zlink::framework::http_request_t &)
    {
        zlink::framework::http_response_t response;
        response.body = nlohmann::json (_evidence.snapshot ()).dump ();
        return response;
    }

  private:
    evidence_store_t &_evidence;
};

class evidence_clear_handler_t
{
  public:
    using dependency_types = zlink::framework::dependency_list_t<evidence_store_t>;

    explicit evidence_clear_handler_t (evidence_store_t &evidence) : _evidence (evidence) {}

    zlink::framework::http_response_t handle (const zlink::framework::http_request_t &)
    {
        _evidence.clear ();
        zlink::framework::http_response_t response;
        response.body = R"({"status":"cleared"})";
        return response;
    }

  private:
    evidence_store_t &_evidence;
};

} // namespace zlink::framework::e2e::resilience_lifecycle::registry
