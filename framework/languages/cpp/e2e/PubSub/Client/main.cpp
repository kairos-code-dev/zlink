/* SPDX-License-Identifier: MPL-2.0 */

#include "Scenarios/fanout_basic_delivery_scenario.hpp"
#include "Scenarios/late_subscriber_scenario.hpp"
#include "Scenarios/missing_message_name_scenario.hpp"
#include "Scenarios/publisher_restart_scenario.hpp"
#include "Scenarios/slow_subscriber_scenario.hpp"
#include "Scenarios/subscriber_reconnect_scenario.hpp"
#include "Scenarios/topic_filter_scenario.hpp"
#include "Support/client_support.hpp"

#include <exception>
#include <iostream>
#include <string>

namespace ps_client = zlink::framework::e2e::pubsub::client;

int main ()
{
    try {
        ps_client::touch_file (ps_client::env_or ("ZLINK_CPP_E2E_START_READY_FILE"));
        ps_client::wait_for_file (ps_client::env_or ("ZLINK_CPP_E2E_START_CONTINUE_FILE"));
        std::this_thread::sleep_for (std::chrono::milliseconds (1000));

        const auto scenario = ps_client::env_or ("ZLINK_CPP_E2E_SCENARIO", "basic");
        const auto publisher_url = ps_client::env_or ("ZLINK_CPP_E2E_PUBLISHER_URL");
        ps_client::ensure (!publisher_url.empty (), "ZLINK_CPP_E2E_PUBLISHER_URL is required");

        if (scenario == "basic") {
            ps_client::run_fanout_basic_delivery_scenario (publisher_url);
        } else if (scenario == "topic") {
            ps_client::run_topic_filter_scenario (publisher_url);
        } else if (scenario == "late") {
            ps_client::run_late_subscriber_scenario (publisher_url);
        } else if (scenario == "reconnect") {
            ps_client::run_subscriber_reconnect_scenario (publisher_url);
        } else if (scenario == "slow") {
            ps_client::run_slow_subscriber_scenario (publisher_url);
        } else if (scenario == "publisher-restart") {
            ps_client::run_publisher_restart_scenario (publisher_url);
        } else if (scenario == "negative") {
            ps_client::run_missing_message_name_scenario (publisher_url);
        } else {
            throw std::runtime_error ("unknown scenario " + scenario);
        }

        std::cout << "pubsub e2e result=passed\n";
        return 0;
    }
    catch (const std::exception &error) {
        std::cerr << "pubsub scenario failed: " << error.what () << "\n";
        return 1;
    }
}
