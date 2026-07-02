/* SPDX-License-Identifier: MPL-2.0 */

#include "Scenarios/auto_registration_scenario.hpp"
#include "Scenarios/codec_mismatch_scenario.hpp"
#include "Scenarios/manual_registration_scenario.hpp"
#include "Scenarios/rc_a4_di_lifecycle_scenario.hpp"
#include "Scenarios/rc_a5_filter_ordering_scenario.hpp"
#include "Scenarios/rc_b1_json_codec_scenario.hpp"
#include "Scenarios/rc_b2_protobuf_codec_scenario.hpp"
#include "Scenarios/rc_b3_messagepack_codec_scenario.hpp"
#include "Scenarios/rc_b4_codec_coexistence_scenario.hpp"

#include <iostream>

namespace rc = zlink::framework::e2e::registration_codec;
namespace rc_client = zlink::framework::e2e::registration_codec::client;

int main (int argc, char **argv)
{
    (void) argc;
    (void) argv;

    try {
        const auto scenario = rc_client::env_or ("ZLINK_CPP_E2E_SCENARIO", "all");
        if (scenario == "b5") {
            rc_client::run_codec_mismatch_scenario ();
        } else {
            rc_client::run_auto_registration_scenario ();
            rc_client::run_manual_registration_scenario ();
            rc_client::run_di_lifecycle_scenario ();
            rc_client::run_filter_ordering_scenario ();
            const auto roundtrip = rc_client::post_empty<rc::codec_roundtrip_scenario_res_t> (
              rc_client::env_or ("ZLINK_CPP_E2E_HTTP_ENDPOINT"), "/codec/roundtrip");
            rc_client::run_json_codec_scenario (roundtrip);
            rc_client::run_protobuf_codec_scenario (roundtrip);
            rc_client::run_messagepack_codec_scenario (roundtrip);
            rc_client::run_codec_coexistence_scenario ();
        }
    }
    catch (const std::exception &error) {
        std::cerr << "registration-codec scenario failed: " << error.what () << "\n";
        return 1;
    }
    return 0;
}
