/* SPDX-License-Identifier: MPL-2.0 */

/* Config 11 trigger client. Scenarios:
 *   flow  — OBS-A1: one STREAM request whose connector-generated flow id
 *           threads connector -> session inbound -> room-spot dispatch.
 *   error — OBS-A2: a packet without a handler so the dispatch error line
 *           carries the same flow id as the healthy lines.
 * The trigger only uses the public connector surface. */

#include "../Shared/observability_contracts.hpp"

#include <zlink/stream_connector.hpp>
#include <zlink/stream_connector/codecs/auto_codec.hpp>

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

namespace obs = zlink::framework::e2e::observability_ops;

namespace
{

std::string env_or (const char *name, const std::string &fallback = {})
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

} // namespace

int main (int argc, char **argv)
{
    try {
        const std::string scenario = argc > 1 ? argv[1] : "flow";
        const auto stream_endpoint = env_or ("ZLINK_CPP_E2E_STREAM_ENDPOINT");
        const auto spot_rid = env_or ("ZLINK_CPP_E2E_SPOT_RID", "obs-room-1");
        ensure (!stream_endpoint.empty (), "ZLINK_CPP_E2E_STREAM_ENDPOINT is required");

        zlink::stream_connector::connector_options_t options;
        options.endpoint = stream_endpoint;
        options.connect_timeout = std::chrono::milliseconds (3000);
        options.request_timeout = std::chrono::milliseconds (10000);
        options.dispatch_mode = zlink::stream_connector::dispatch_mode_t::immediate;
        auto client = zlink::stream_connector::connector_factory_t::create (options);
        auto connected = client.connect ();
        ensure (static_cast<bool> (connected), "ObservabilityOps stream connect failed");

        if (scenario == "flow") {
            auto reply =
              client
                .request (obs::obs_action_req_t{.spot_rid = spot_rid,
                                                .marker = "obs-a1",
                                                .value = 7})
                .packet_name (obs::obs_action_req_t::packet_name)
                .timeout (std::chrono::milliseconds (10000))
                .submit<obs::obs_action_res_t> ();
            ensure (static_cast<bool> (reply), "OBS-A1 ObsActionReq failed");
            ensure (reply.value ().value == 7, "OBS-A1 unexpected room value");
            std::cout << "scenario OBS-A1 trigger passed" << std::endl;
            return 0;
        }
        if (scenario == "error") {
            auto failed = client.request (obs::obs_unknown_req_t{.marker = "obs-a2"})
                            .packet_name (obs::obs_unknown_req_t::packet_name)
                            .timeout (std::chrono::milliseconds (3000))
                            .submit<obs::obs_action_res_t> ();
            ensure (!failed, "OBS-A2 expected the unknown packet to fail");
            std::cout << "scenario OBS-A2 trigger passed" << std::endl;
            return 0;
        }
        throw std::runtime_error ("unknown ObservabilityOps trigger scenario: " + scenario);
    }
    catch (const std::exception &error) {
        std::cerr << "observability-ops trigger failed: " << error.what () << std::endl;
        return 1;
    }
}
