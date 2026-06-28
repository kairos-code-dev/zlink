/* SPDX-License-Identifier: MPL-2.0 */

#include "../Shared/pubsub_contracts.hpp"

#include <zlink/framework.hpp>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>

namespace e2e = zlink::framework::e2e::pubsub;

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

void configure_codecs (zlink::framework::codec_options_builder_t codecs)
{
    codecs.add_json ();
    codecs.add_json<e2e::event_notify_t> ();
}

class scenario_service_t final : public zlink::framework::hosted_service_t
{
  public:
    explicit scenario_service_t (zlink::framework::app_t &app) : _app (app) {}

    void start (zlink::framework::service_provider_t &services) override
    {
        try {
            auto &publisher = services.get_required<zlink::framework::publisher_t> ();
            touch_file (env_or ("ZLINK_CPP_E2E_START_READY_FILE"));
            wait_for_file (env_or ("ZLINK_CPP_E2E_START_CONTINUE_FILE"));
            std::this_thread::sleep_for (std::chrono::milliseconds (1000));
        const auto scenario = env_or ("ZLINK_CPP_E2E_SCENARIO", "basic");
        if (scenario == "basic") {
            run_basic (publisher);
        } else if (scenario == "topic") {
            run_topic (publisher);
        } else if (scenario == "late") {
            run_late (publisher);
        } else if (scenario == "reconnect") {
            run_reconnect (publisher);
        } else if (scenario == "slow") {
            run_slow_subscriber (publisher);
        } else if (scenario == "publisher-restart") {
            run_publisher_restart (publisher);
        } else if (scenario == "negative") {
            run_negative (publisher);
        } else {
                throw std::runtime_error ("unknown scenario " + scenario);
            }
            passed = true;
        }
        catch (const std::exception &error) {
            std::cerr << "pubsub scenario failed: " << error.what () << "\n";
        }
        _app.stop ();
    }

    void stop () noexcept override {}

    bool passed = false;

  private:
    void publish (zlink::framework::publisher_t &publisher,
                  const std::string &topic,
                  const std::string &value,
                  const std::string &packet_name = "")
    {
        auto call = publisher.publish (e2e::event_channel, topic, e2e::event_notify_t{value});
        if (!packet_name.empty ()) {
            call.packet_name (packet_name);
        }
        auto result = call.async ().result ();
        ensure (result.has_value (),
                "publish failed: "
                  + std::string (result.error () ? result.error ()->what () : "unknown"));
    }

    void run_basic (zlink::framework::publisher_t &publisher)
    {
        for (int index = 0; index < 5; ++index) {
            publish (publisher, e2e::topic_fanout, "warmup-" + std::to_string (index));
        }
        std::this_thread::sleep_for (std::chrono::milliseconds (500));
        for (int index = 0; index < 20; ++index) {
            publish (publisher, e2e::topic_fanout, "measure-" + std::to_string (index));
        }
        std::cout << "scenario PS-A1 passed\n";
    }

    void run_topic (zlink::framework::publisher_t &publisher)
    {
        for (int index = 0; index < 8; ++index) {
            publish (publisher, e2e::topic_alpha, "alpha-" + std::to_string (index));
            publish (publisher, e2e::topic_beta, "beta-" + std::to_string (index));
        }
        std::cout << "scenario PS-A2 passed\n";
    }

    void run_late (zlink::framework::publisher_t &publisher)
    {
        for (int index = 0; index < 5; ++index) {
            publish (publisher, e2e::topic_fanout, "before-late-" + std::to_string (index));
        }
        touch_file (env_or ("ZLINK_CPP_E2E_READY_FILE"));
        wait_for_file (env_or ("ZLINK_CPP_E2E_CONTINUE_FILE"));
        std::this_thread::sleep_for (std::chrono::milliseconds (500));
        for (int index = 0; index < 8; ++index) {
            publish (publisher, e2e::topic_fanout, "after-late-" + std::to_string (index));
        }
        std::cout << "scenario PS-A3 passed\n";
    }

    void run_reconnect (zlink::framework::publisher_t &publisher)
    {
        for (int index = 0; index < 5; ++index) {
            publish (publisher, e2e::topic_fanout, "before-reconnect-" + std::to_string (index));
        }
        touch_file (env_or ("ZLINK_CPP_E2E_READY_FILE"));
        wait_for_file (env_or ("ZLINK_CPP_E2E_CONTINUE_FILE"));
        for (int index = 0; index < 5; ++index) {
            publish (publisher, e2e::topic_fanout, "during-reconnect-" + std::to_string (index));
        }
        touch_file (env_or ("ZLINK_CPP_E2E_RESTART_READY_FILE"));
        wait_for_file (env_or ("ZLINK_CPP_E2E_RESTART_CONTINUE_FILE"));
        std::this_thread::sleep_for (std::chrono::milliseconds (500));
        for (int index = 0; index < 8; ++index) {
            publish (publisher, e2e::topic_fanout, "after-reconnect-" + std::to_string (index));
        }
        std::cout << "scenario PS-A4 passed\n";
    }

    void run_slow_subscriber (zlink::framework::publisher_t &publisher)
    {
        for (int index = 0; index < 16; ++index) {
            publish (publisher, e2e::topic_fanout, "slow-isolation-" + std::to_string (index));
        }
        std::cout << "scenario PS-B1 passed\n";
    }

    void run_publisher_restart (zlink::framework::publisher_t &publisher)
    {
        for (int index = 0; index < 8; ++index) {
            publish (publisher, e2e::topic_fanout,
                    "publisher-restart-" + std::to_string (index));
        }
        std::cout << "scenario PS-B2 passed\n";
    }

    void run_negative (zlink::framework::publisher_t &publisher)
    {
        publish (publisher, e2e::topic_fanout, "missing", "MissingEventNotify");
        std::this_thread::sleep_for (std::chrono::milliseconds (500));
        publish (publisher, e2e::topic_fanout, "after-missing");
        std::cout << "scenario PS-C1 passed\n";
    }

    zlink::framework::app_t &_app;
};

} // namespace

int main (int argc, char **argv)
{
    const auto log_dir = env_or ("ZLINK_CPP_E2E_LOG_DIR", "logs");
    const auto registry_router = env_or ("ZLINK_CPP_E2E_REGISTRY_ROUTER");
    const auto publisher_endpoint = env_or ("ZLINK_CPP_E2E_PUBLISHER_ENDPOINT");

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
          .trace_label ("cpp-ps-client");
        configure_codecs (options.codecs ());
        options.use_discovery ().add_registry_endpoint (registry_router);
        options.add_fanout_channel (e2e::event_channel).enable_publisher (publisher_endpoint);
    });
    app.add_hosted_service (std::move (scenario));
    const auto exit_code = app.run (argc, argv);
    if (exit_code != 0 || !scenario_result->passed) {
        return 1;
    }
    std::cout << "pubsub e2e result=passed\n";
    return 0;
}
