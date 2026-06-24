/* SPDX-License-Identifier: MPL-2.0 */

#include "../Shared/registry_messaging_contracts.hpp"

#include <zlink/framework.hpp>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace e2e = zlink::framework::e2e::registry_messaging;

namespace
{

std::string env_or (const char *name, std::string fallback = {})
{
    if (const char *value = std::getenv (name); value != nullptr && *value != '\0') {
        return value;
    }
    return fallback;
}

void ensure (bool condition, const std::string &message)
{
    if (!condition) {
        throw std::runtime_error (message);
    }
}

void touch_file (const std::string &path)
{
    if (path.empty ()) {
        return;
    }
    std::ofstream file (path);
    file << "ready\n";
}

void wait_for_file (const std::string &path)
{
    if (path.empty ()) {
        return;
    }
    for (int attempt = 0; attempt < 200; ++attempt) {
        if (std::filesystem::exists (path)) {
            return;
        }
        std::this_thread::sleep_for (std::chrono::milliseconds (50));
    }
    throw std::runtime_error ("timed out waiting for " + path);
}

void configure_common_codecs (zlink::framework::codec_options_builder_t codecs)
{
    codecs.add_json ()
      .add_json<e2e::profile_request_t> ()
      .add_json<e2e::profile_reply_t> ()
      .add_json<e2e::profile_command_t> ()
      .add_json<e2e::route_ping_t> ()
      .add_json<e2e::route_pong_t> ();
}

class scenario_service_t final : public zlink::framework::hosted_service_t
{
  public:
    explicit scenario_service_t (zlink::framework::app_t &app) : _app (app) {}

    void start (zlink::framework::service_provider_t &services) override
    {
        try {
            auto &channels = services.get_required<zlink::framework::channel_client_t> ();
            auto &routes = services.get_required<zlink::framework::route_client_t> ();
            const auto scenario = env_or ("ZLINK_CPP_E2E_SCENARIO", "common");
            if (scenario == "common") {
                run_registry_client_server (channels);
                run_manual_client_server (channels);
                run_cross_channel_discovery (channels);
                run_message_size_variation (channels);
                run_route_mesh (routes);
            } else if (scenario == "scale-out") {
                run_scale_out (channels);
            } else if (scenario == "scale-in") {
                run_scale_in (channels);
            } else if (scenario == "failover") {
                run_failover (channels);
            } else if (scenario == "weighted") {
                run_weighted_distribution (channels);
            } else if (scenario == "max-size") {
                run_max_message_size_limit (channels);
            } else {
                throw std::runtime_error ("unknown scenario " + scenario);
            }
            passed = true;
        }
        catch (const std::exception &error) {
            std::cerr << "registry-messaging scenario failed: " << error.what () << "\n";
        }
        _app.stop ();
    }

    void stop () noexcept override {}

    bool passed = false;

  private:
    void run_registry_client_server (zlink::framework::channel_client_t &channels)
    {
        std::set<std::string> providers;
        for (int index = 0; index < 20 && providers.size () < 2; ++index) {
            auto task =
              channels
                .request (e2e::api_channel,
                          e2e::profile_request_t{.value = "auto-" + std::to_string (index)})
                .timeout (std::chrono::milliseconds (2000))
                .async<e2e::profile_reply_t> ();
            const auto &result = task.result ();
            ensure (result.has_value (),
                    "RM-A1 request failed: "
                      + std::string (result.error () ? result.error ()->what () : "unknown"));
            ensure (result.value ().value.rfind ("profile:auto-", 0) == 0,
                    "RM-A1 reply payload mismatch");
            providers.insert (result.value ().provider_rid);
        }
        ensure (!providers.empty (), "RM-A1 did not reach any provider");
        std::cout << "scenario RM-A1 passed\n";

        auto request = channels.request (e2e::api_channel, e2e::profile_request_t{.value = "c1"})
                         .timeout (std::chrono::milliseconds (2000))
                         .async<e2e::profile_reply_t> ();
        ensure (request.result ().has_value (), "RM-C1 request failed");
        ensure (request.result ().value ().value == "profile:c1", "RM-C1 reply mismatch");
        auto send =
          channels.send (e2e::api_channel, e2e::profile_command_t{.command_id = "cmd-c1"}).async ();
        ensure (send.result ().has_value (), "RM-C1 send failed");
        std::cout << "scenario RM-C1 passed\n";

        auto slow = channels.request (e2e::api_channel, e2e::profile_request_t{.value = "slow"})
                      .timeout (std::chrono::milliseconds (100))
                      .async<e2e::profile_reply_t> ();
        ensure (!slow.result ().has_value (), "RM-C4 slow request unexpectedly succeeded");
        auto after = channels.request (e2e::api_channel, e2e::profile_request_t{.value = "after"})
                       .timeout (std::chrono::milliseconds (2000))
                       .async<e2e::profile_reply_t> ();
        ensure (after.result ().has_value (), "RM-C4 post-timeout request failed");
        std::this_thread::sleep_for (std::chrono::milliseconds (1100));
        auto later = channels.request (e2e::api_channel, e2e::profile_request_t{.value = "later"})
                       .timeout (std::chrono::milliseconds (2000))
                       .async<e2e::profile_reply_t> ();
        ensure (later.result ().has_value (), "RM-C4 later request failed");
        std::cout << "scenario RM-C4 passed\n";

        auto missing =
          channels.request (e2e::api_channel, e2e::profile_request_t{.value = "missing"})
            .packet_name ("MissingProfileRequest")
            .timeout (std::chrono::milliseconds (2000))
            .async<e2e::profile_reply_t> ();
        ensure (!missing.result ().has_value (), "RM-C5 missing request unexpectedly succeeded");
        auto dropped =
          channels.send (e2e::api_channel, e2e::profile_command_t{.command_id = "missing-send"})
            .packet_name ("MissingProfileCommand")
            .async ();
        ensure (dropped.result ().has_value (), "RM-C5 missing send should complete as drop");
        auto normal = channels.request (e2e::api_channel, e2e::profile_request_t{.value = "normal"})
                        .timeout (std::chrono::milliseconds (2000))
                        .async<e2e::profile_reply_t> ();
        ensure (normal.result ().has_value (), "RM-C5 normal request after missing packet failed");
        std::cout << "scenario RM-C5 passed\n";
    }

    void run_manual_client_server (zlink::framework::channel_client_t &channels)
    {
        auto request =
          channels
            .request ("registry.messaging.api.manual", e2e::profile_request_t{.value = "manual"})
            .timeout (std::chrono::milliseconds (2000))
            .async<e2e::profile_reply_t> ();
        ensure (request.result ().has_value (), "RM-A2 request failed");
        ensure (request.result ().value ().provider_rid == "api-a",
                "RM-A2 did not use the requested provider endpoint");
        std::cout << "scenario RM-A2 passed\n";

        std::map<std::string, int> counts;
        for (int index = 0; index < 60; ++index) {
            auto task =
              channels
                .request ("registry.messaging.api.manual.multi",
                          e2e::profile_request_t{.value = "multi-" + std::to_string (index)})
                .timeout (std::chrono::milliseconds (2000))
                .async<e2e::profile_reply_t> ();
            ensure (task.result ().has_value (), "RM-C3 request failed");
            ++counts[task.result ().value ().provider_rid];
        }
        ensure (counts["api-a"] > 0 && counts["api-b"] > 0,
                "RM-C3 did not distribute to both providers");
        ensure (counts["api-a"] + counts["api-b"] == 60, "RM-C3 count mismatch");
        std::cout << "scenario RM-C3 passed\n";
    }

    void run_weighted_distribution (zlink::framework::channel_client_t &channels)
    {
        std::map<std::string, int> counts;
        for (int index = 0; index < 240; ++index) {
            auto task =
              channels
                .request (e2e::api_channel,
                          e2e::profile_request_t{.value = "weighted-" + std::to_string (index)})
                .timeout (std::chrono::milliseconds (2000))
                .async<e2e::profile_reply_t> ();
            ensure (task.result ().has_value (), "RM-C7 weighted request failed");
            ++counts[task.result ().value ().provider_rid];
        }
        ensure (counts["api-a"] > 0 && counts["api-b"] > 0,
                "RM-C7 did not use both weighted providers");
        ensure (counts["api-a"] + counts["api-b"] == 240, "RM-C7 count mismatch");
        ensure (counts["api-a"] > counts["api-b"],
                "RM-C7 higher-weight provider was not preferred");
        std::cout << "scenario RM-C7 passed\n";
    }

    void run_cross_channel_discovery (zlink::framework::channel_client_t &channels)
    {
        auto api = channels.request (e2e::api_channel, e2e::profile_request_t{.value = "a6-api"})
                     .timeout (std::chrono::milliseconds (2000))
                     .async<e2e::profile_reply_t> ();
        ensure (api.result ().has_value (), "RM-A6 api channel request failed");
        ensure (api.result ().value ().provider_rid.rfind ("api-", 0) == 0,
                "RM-A6 api channel resolved a non-api provider");

        auto workflow =
          channels.request (e2e::workflow_channel, e2e::profile_request_t{.value = "a6-workflow"})
            .timeout (std::chrono::milliseconds (2000))
            .async<e2e::profile_reply_t> ();
        ensure (workflow.result ().has_value (), "RM-A6 workflow channel request failed");
        ensure (workflow.result ().value ().provider_rid == "workflow-a",
                "RM-A6 workflow channel resolved the wrong provider");
        std::cout << "scenario RM-A6 passed\n";
    }

    void run_message_size_variation (zlink::framework::channel_client_t &channels)
    {
        const std::vector<std::size_t> sizes = {8, 64 * 1024, 256 * 1024};
        for (const auto size : sizes) {
            const auto payload = std::string (size, static_cast<char> ('a' + (size % 23)));
            auto task =
              channels.request (e2e::api_channel, e2e::profile_request_t{.value = payload})
                .timeout (std::chrono::milliseconds (5000))
                .async<e2e::profile_reply_t> ();
            ensure (task.result ().has_value (),
                    "RM-C8 request failed for payload size " + std::to_string (size));
            ensure (task.result ().value ().value == "profile:" + payload,
                    "RM-C8 reply payload mismatch for payload size " + std::to_string (size));
        }
        std::cout << "scenario RM-C8 passed\n";
    }

    void run_max_message_size_limit (zlink::framework::channel_client_t &channels)
    {
        const auto oversized_payload = std::string (32 * 1024, 'x');
        auto oversized =
          channels
            .request ("registry.messaging.api.manual.max",
                      e2e::profile_request_t{.value = oversized_payload})
            .timeout (std::chrono::milliseconds (1000))
            .async<e2e::profile_reply_t> ();
        ensure (!oversized.result ().has_value (),
                "RM-C8 oversized request unexpectedly succeeded");

        auto normal =
          channels
            .request ("registry.messaging.api.manual.max",
                      e2e::profile_request_t{.value = "after-oversized"})
            .timeout (std::chrono::milliseconds (2000))
            .async<e2e::profile_reply_t> ();
        ensure (normal.result ().has_value (), "RM-C8 normal request after oversized failed");
        ensure (normal.result ().value ().value == "profile:after-oversized",
                "RM-C8 normal reply after oversized mismatch");
        std::cout << "scenario RM-C8-max passed\n";
    }

    void run_route_mesh (zlink::framework::route_client_t &routes)
    {
        auto to_b =
          routes
            .request (e2e::route_channel, zlink::routing_id_t::from (std::string ("api-b")),
                      e2e::route_ping_t{.value = "target-b"})
            .packet_name ("ScenarioRoutePing")
            .timeout (std::chrono::milliseconds (2000))
            .async<e2e::route_pong_t> ();
        ensure (to_b.result ().has_value (), "RM-C2 target request failed");
        ensure (to_b.result ().value ().target_rid == "api-b", "RM-C2 target rid mismatch");

        auto missing =
          routes
            .request (e2e::route_channel, zlink::routing_id_t::from (std::string ("api-missing")),
                      e2e::route_ping_t{.value = "missing"})
            .packet_name ("ScenarioRoutePing")
            .timeout (std::chrono::milliseconds (500))
            .async<e2e::route_pong_t> ();
        ensure (!missing.result ().has_value (), "RM-C2 missing rid unexpectedly succeeded");
        std::cout << "scenario RM-C2 passed\n";
    }

    void run_scale_out (zlink::framework::channel_client_t &channels)
    {
        for (int index = 0; index < 5; ++index) {
            auto task = channels
                          .request (e2e::api_channel,
                                    e2e::profile_request_t{.value = "scale-out-before-"
                                                                    + std::to_string (index)})
                          .timeout (std::chrono::milliseconds (2000))
                          .async<e2e::profile_reply_t> ();
            ensure (task.result ().has_value (), "RM-B1 initial request failed");
            ensure (task.result ().value ().provider_rid == "api-a",
                    "RM-B1 initial traffic should only use api-a");
        }

        touch_file (env_or ("ZLINK_CPP_E2E_READY_FILE"));
        wait_for_file (env_or ("ZLINK_CPP_E2E_CONTINUE_FILE"));

        std::set<std::string> providers;
        for (int index = 0; index < 80 && providers.size () < 2; ++index) {
            auto task = channels
                          .request (e2e::api_channel,
                                    e2e::profile_request_t{.value = "scale-out-after-"
                                                                    + std::to_string (index)})
                          .timeout (std::chrono::milliseconds (2000))
                          .async<e2e::profile_reply_t> ();
            ensure (task.result ().has_value (), "RM-B1 post-scale request failed");
            providers.insert (task.result ().value ().provider_rid);
        }
        ensure (providers.contains ("api-a") && providers.contains ("api-b"),
                "RM-B1 did not route to both providers after scale-out");
        std::cout << "scenario RM-B1 passed\n";
    }

    void run_scale_in (zlink::framework::channel_client_t &channels)
    {
        std::set<std::string> before;
        for (int index = 0; index < 80 && before.size () < 2; ++index) {
            auto task = channels
                          .request (e2e::api_channel,
                                    e2e::profile_request_t{.value = "scale-in-before-"
                                                                    + std::to_string (index)})
                          .timeout (std::chrono::milliseconds (2000))
                          .async<e2e::profile_reply_t> ();
            ensure (task.result ().has_value (), "RM-B2 initial request failed");
            before.insert (task.result ().value ().provider_rid);
        }
        ensure (before.contains ("api-a") && before.contains ("api-b"),
                "RM-B2 did not start with both providers");

        touch_file (env_or ("ZLINK_CPP_E2E_READY_FILE"));
        wait_for_file (env_or ("ZLINK_CPP_E2E_CONTINUE_FILE"));

        for (int index = 0; index < 20; ++index) {
            auto task = channels
                          .request (e2e::api_channel,
                                    e2e::profile_request_t{.value = "scale-in-after-"
                                                                    + std::to_string (index)})
                          .timeout (std::chrono::milliseconds (2000))
                          .async<e2e::profile_reply_t> ();
            ensure (task.result ().has_value (), "RM-B2 post-scale request failed");
            ensure (task.result ().value ().provider_rid == "api-a",
                    "RM-B2 routed to removed provider after scale-in");
        }
        std::cout << "scenario RM-B2 passed\n";
    }

    void run_failover (zlink::framework::channel_client_t &channels)
    {
        auto first =
          channels.request (e2e::api_channel, e2e::profile_request_t{.value = "failover-before"})
            .timeout (std::chrono::milliseconds (2000))
            .async<e2e::profile_reply_t> ();
        ensure (first.result ().has_value (), "RM-A4 initial request failed");
        ensure (first.result ().value ().provider_rid == "api-a"
                  && first.result ().value ().instance_id == "api-a-v1",
                "RM-A4 initial provider mismatch");

        touch_file (env_or ("ZLINK_CPP_E2E_READY_FILE"));
        wait_for_file (env_or ("ZLINK_CPP_E2E_CONTINUE_FILE"));

        for (int index = 0; index < 20; ++index) {
            auto task = channels
                          .request (e2e::api_channel,
                                    e2e::profile_request_t{.value = "failover-after-"
                                                                    + std::to_string (index)})
                          .timeout (std::chrono::milliseconds (2000))
                          .async<e2e::profile_reply_t> ();
            ensure (task.result ().has_value (), "RM-A4 post-failover request failed");
            ensure (task.result ().value ().provider_rid == "api-a"
                      && task.result ().value ().instance_id == "api-a-v2",
                    "RM-A4 did not switch to replacement provider");
        }
        std::cout << "scenario RM-A4 passed\n";
    }

    zlink::framework::app_t &_app;
};

} // namespace

int main (int argc, char **argv)
{
    const auto log_dir = env_or ("ZLINK_CPP_E2E_LOG_DIR", "logs");
    const auto registry_router = env_or ("ZLINK_CPP_E2E_REGISTRY_ROUTER");
    const auto api_a_endpoint = env_or ("ZLINK_CPP_E2E_API_A_ENDPOINT");
    const auto api_b_endpoint = env_or ("ZLINK_CPP_E2E_API_B_ENDPOINT");
    const auto route_a_endpoint = env_or ("ZLINK_CPP_E2E_ROUTE_A_ENDPOINT");
    const auto route_b_endpoint = env_or ("ZLINK_CPP_E2E_ROUTE_B_ENDPOINT");

    auto app = zlink::framework::app_t::create ();
    auto scenario = std::make_unique<scenario_service_t> (app);
    auto *scenario_result = scenario.get ();
    app.logging ()
      .use_file (log_dir + "/client.log")
      .set_min_level (zlink::framework::log_level_t::debug);
    app.add_zlink_framework ([&] (zlink::framework::zlink_framework_options_t &options) {
        options.configure_dispatch ()
          .message_flow (zlink::framework::message_flow_log_mode_t::key_transitions)
          .trace_log_file (log_dir + "/client-flow.log")
          .trace_node_id ("cpp-rm-client");
        configure_common_codecs (options.codecs ());
        options.use_discovery ().add_registry_endpoint (registry_router);
        options.add_client_server_channel (e2e::api_channel).enable_client ();
        options.add_client_server_channel (e2e::workflow_channel).enable_client ();
        options.add_client_server_channel ("registry.messaging.api.manual")
          .enable_client (api_a_endpoint);
        options.add_client_server_channel ("registry.messaging.api.manual.multi")
          .enable_client (api_a_endpoint)
          .enable_client (api_b_endpoint);
        options.add_client_server_channel ("registry.messaging.api.manual.weighted")
          .enable_client (api_a_endpoint)
          .enable_client (api_b_endpoint);
        options.add_client_server_channel ("registry.messaging.api.manual.max")
          .enable_client (api_a_endpoint);
        options.add_route_mesh (e2e::route_channel)
          .enable_server (env_or ("ZLINK_CPP_E2E_CLIENT_ROUTE_ENDPOINT"))
          .set_routing_id (zlink::routing_id_t::from (std::string ("client")))
          .enable_client (route_a_endpoint)
          .enable_client (route_b_endpoint);
    });
    app.add_hosted_service (std::move (scenario));
    const auto exit_code = app.run (argc, argv);
    if (exit_code != 0 || !scenario_result->passed) {
        return 1;
    }
    std::cout << "registry-messaging e2e result=passed\n";
    return 0;
}
