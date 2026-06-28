/* SPDX-License-Identifier: MPL-2.0 */

#include "../../RegistryMessaging/Shared/registry_messaging_contracts.hpp"

#include <zlink/framework.hpp>

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace rm = zlink::framework::e2e::registry_messaging;

namespace
{

std::string env_or (const char *name, std::string fallback = {})
{
    if (const char *value = std::getenv (name); value != nullptr && *value != '\0') {
        return value;
    }
    return fallback;
}

std::set<std::string> split_expected (const std::string &text)
{
    std::set<std::string> result;
    std::stringstream input (text);
    std::string item;
    while (std::getline (input, item, ',')) {
        if (!item.empty ()) {
            result.insert (item);
        }
    }
    return result;
}

std::set<std::string> split_keys (const std::string &text)
{
    std::set<std::string> result;
    std::stringstream input (text);
    std::string item;
    while (std::getline (input, item, '\n')) {
        if (!item.empty ()) {
            result.insert (item);
        }
    }
    return result;
}

std::vector<std::string> split_csv (const std::string &text)
{
    std::vector<std::string> result;
    std::stringstream input (text);
    std::string item;
    while (std::getline (input, item, ',')) {
        if (!item.empty ()) {
            result.push_back (item);
        }
    }
    return result;
}

void ensure (bool condition, const std::string &message)
{
    if (!condition) {
        throw std::runtime_error (message);
    }
}

void configure_codecs (zlink::framework::codec_options_builder_t codecs)
{
    codecs.add_json ();
    codecs.add_json<rm::profile_request_t,
                    rm::profile_reply_t,
                    rm::profile_command_t> ();
}

std::string role_name (zlink::framework::service_role_t role)
{
    switch (role) {
        case zlink::framework::service_role_t::client:
            return "client";
        case zlink::framework::service_role_t::server:
            return "server";
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

std::set<std::string> topology_keys (
  const std::vector<zlink::framework::topology_entry_t> &entries)
{
    std::set<std::string> result;
    for (const auto &entry : entries) {
        result.insert (entry.name + "|" + role_name (entry.role) + "|" + entry.endpoint + "|"
                       + (entry.routing_id ? entry.routing_id->to_string () : ""));
    }
    return result;
}

class scenario_service_t final : public zlink::framework::hosted_service_t
{
  public:
    explicit scenario_service_t (zlink::framework::app_t &app) : _app (app) {}

    void start (zlink::framework::service_provider_t &services) override
    {
        try {
            if (env_or ("ZLINK_CPP_E2E_TOPOLOGY_COMPARE") == "1") {
                run_topology_compare ();
                passed = true;
                std::cout << "scenario DR-D4 passed\n";
                _app.stop ();
                return;
            }
            auto expected = split_expected (env_or ("ZLINK_CPP_E2E_EXPECT_PROVIDERS"));
            const auto attempts = std::stoi (env_or ("ZLINK_CPP_E2E_REQUEST_ATTEMPTS", "40"));
            auto &channels = services.get_required<zlink::framework::channel_client_t> ();
            std::set<std::string> seen;
            for (int index = 0; index < attempts; ++index) {
                auto task =
                  channels
                    .request (rm::api_channel,
                              rm::profile_request_t{.value = "dr-" + std::to_string (index)})
                    .timeout (std::chrono::milliseconds (3000))
                    .async<rm::profile_reply_t> ();
                ensure (task.result ().has_value (),
                        "discovery registry request failed: "
                          + std::string (task.result ().error ()
                                           ? task.result ().error ()->what ()
                                          : "unknown"));
                seen.insert (task.result ().value ().provider_rid);
                if (!expected.empty ()) {
                    bool complete = true;
                    for (const auto &provider : expected) {
                        complete = complete && seen.contains (provider);
                    }
                    if (complete) {
                        break;
                    }
                }
            }
            for (const auto &provider : expected) {
                ensure (seen.contains (provider), "provider not reached: " + provider);
            }
            passed = true;
            std::cout << "providers_seen=";
            for (const auto &provider : seen) {
                std::cout << provider << ",";
            }
            std::cout << "\n";
        }
        catch (const std::exception &error) {
            std::cerr << "discovery-registry-ha client failed: " << error.what () << "\n";
        }
        _app.stop ();
    }

    void stop () noexcept override {}

    bool passed = false;

  private:
    void run_topology_compare ()
    {
        const auto expected_keys =
          split_keys (env_or ("ZLINK_CPP_E2E_EXPECT_TOPOLOGY_KEYS"));
        ensure (!expected_keys.empty (), "DR-D4 expected topology keys are required");
        zlink::framework::registry_query_client_t remote_client;
        auto connected = remote_client.connect (env_or ("ZLINK_CPP_E2E_REGISTRY_ROUTER"));
        ensure (static_cast<bool> (connected),
                "DR-D4 remote registry query connect failed");

        const auto deadline = std::chrono::steady_clock::now () + std::chrono::seconds (5);
        while (std::chrono::steady_clock::now () < deadline) {
            auto remote_topology = remote_client.topology ();
            ensure (static_cast<bool> (remote_topology),
                    "DR-D4 remote registry query failed");
            const auto remote_keys = topology_keys (remote_topology.value ());
            if (expected_keys == remote_keys) {
                remote_client.close ();
                return;
            }
            std::this_thread::sleep_for (std::chrono::milliseconds (50));
        }
        std::cerr << "DR-D4 expected topology keys:\n";
        for (const auto &key : expected_keys) {
            std::cerr << "  " << key << "\n";
        }
        auto remote_topology = remote_client.topology ();
        if (remote_topology) {
            std::cerr << "DR-D4 remote topology keys:\n";
            for (const auto &key : topology_keys (remote_topology.value ())) {
                std::cerr << "  " << key << "\n";
            }
        }
        remote_client.close ();
        throw std::runtime_error ("DR-D4 topology snapshots did not match");
    }

    zlink::framework::app_t &_app;
};

} // namespace

int main (int argc, char **argv)
{
    const auto registry_router = env_or ("ZLINK_CPP_E2E_REGISTRY_ROUTER");
    const auto log_dir = env_or ("ZLINK_CPP_E2E_LOG_DIR", "logs");
    auto app = zlink::framework::app_t::create ();
    auto scenario = std::make_unique<scenario_service_t> (app);
    auto *scenario_result = scenario.get ();
    app.logging ().use_file (log_dir + "/client.log").set_min_level (
      zlink::framework::log_level_t::debug);
    app.add_zlink_framework ([&] (zlink::framework::zlink_framework_options_t &options) {
        options.configure_dispatch ()
          .message_flow (zlink::framework::message_flow_log_mode_t::key_transitions)
          .trace_log_file (log_dir + "/client-flow.log")
          .trace_label ("cpp-dr-client");
        configure_codecs (options.codecs ());
        for (const auto &endpoint : split_csv (registry_router)) {
            options.use_discovery ().add_registry_endpoint (endpoint);
        }
        options.add_client_server_channel (rm::api_channel).enable_client ();
    });
    app.add_hosted_service (std::move (scenario));
    const auto exit_code = app.run (argc, argv);
    if (exit_code != 0 || !scenario_result->passed) {
        return 1;
    }
    std::cout << "discovery-registry-ha client result=passed\n";
    return 0;
}
