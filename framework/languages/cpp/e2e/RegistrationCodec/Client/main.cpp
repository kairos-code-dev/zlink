/* SPDX-License-Identifier: FSL-1.1-ALv2 */

#include "Scenarios/auto_registration_scenario.hpp"
#include "Scenarios/attribute_registration_scenario.hpp"
#include "Scenarios/codec_mismatch_scenario.hpp"
#include "Scenarios/manual_registration_scenario.hpp"
#include "Scenarios/rc_a4_di_lifecycle_scenario.hpp"
#include "Scenarios/rc_a5_filter_ordering_scenario.hpp"
#include "Scenarios/rc_a6_invalid_registration_scenario.hpp"
#include "Scenarios/rc_b1_json_codec_scenario.hpp"
#include "Scenarios/rc_b2_protobuf_codec_scenario.hpp"
#include "Scenarios/rc_b3_messagepack_codec_scenario.hpp"
#include "Scenarios/rc_b4_codec_coexistence_scenario.hpp"

#include <iostream>
#include <stdexcept>
#include <string>

namespace rc = zlink::framework::e2e::registration_codec;
namespace rc_client = zlink::framework::e2e::registration_codec::client;

int main (int argc, char **argv)
{
    (void) argc;
    (void) argv;

    try {
        const auto scenario = rc_client::env_or ("ZLINK_CPP_E2E_SCENARIO", "all");
        if (scenario == "b5" || scenario == "rc-b5") {
            rc_client::run_codec_mismatch_scenario ();
        } else {
            bool ran = false;
            auto wants = [&scenario] (const char *id) {
                return scenario == "all" || scenario == id;
            };
            if (wants ("rc-a1")) {
                ran = true;
                rc_client::run_auto_registration_scenario ();
            }
            if (wants ("rc-a2")) {
                ran = true;
                rc_client::run_attribute_registration_scenario ();
            }
            if (wants ("rc-a3")) {
                ran = true;
                rc_client::run_manual_registration_scenario ();
            }
            if (wants ("rc-a4")) {
                ran = true;
                rc_client::run_di_lifecycle_scenario ();
            }
            if (wants ("rc-a5")) {
                ran = true;
                rc_client::run_filter_ordering_scenario ();
            }
            if (wants ("rc-a6")) {
                ran = true;
                rc_client::run_invalid_registration_scenario ();
            }
            if (wants ("rc-b1") || wants ("rc-b2") || wants ("rc-b3")) {
                const auto roundtrip = rc_client::post_empty<rc::codec_roundtrip_scenario_res_t> (
                  rc_client::env_or ("ZLINK_CPP_E2E_HTTP_ENDPOINT"), "/codec/roundtrip");
                if (wants ("rc-b1")) {
                    ran = true;
                    rc_client::run_json_codec_scenario (roundtrip);
                }
                if (wants ("rc-b2")) {
                    ran = true;
                    rc_client::run_protobuf_codec_scenario (roundtrip);
                }
                if (wants ("rc-b3")) {
                    ran = true;
                    rc_client::run_messagepack_codec_scenario (roundtrip);
                }
            }
            if (wants ("rc-b4")) {
                ran = true;
                rc_client::run_codec_coexistence_scenario ();
            }
            if (!ran) {
                throw std::runtime_error ("unknown RegistrationCodec scenario: " + scenario);
            }
        }
    }
    catch (const std::exception &error) {
        std::cerr << "registration-codec scenario failed: " << error.what () << "\n";
        return 1;
    }
    return 0;
}
