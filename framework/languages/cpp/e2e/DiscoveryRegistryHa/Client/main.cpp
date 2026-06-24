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
    codecs.add_json ()
      .add_json<rm::profile_request_t> ()
      .add_json<rm::profile_reply_t> ()
      .add_json<rm::profile_command_t> ();
}

class scenario_service_t final : public zlink::framework::hosted_service_t
{
  public:
    explicit scenario_service_t (zlink::framework::app_t &app) : _app (app) {}

    void start (zlink::framework::service_provider_t &services) override
    {
        try {
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
          .trace_node_id ("cpp-dr-client");
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
