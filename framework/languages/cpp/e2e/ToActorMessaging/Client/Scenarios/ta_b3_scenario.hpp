/* SPDX-License-Identifier: FSL-1.1-ALv2 */
#pragma once

#include "../Support/scenario_context.hpp"

namespace
{

/* TA-B3: the client owns the requests, failure classification, and evidence assertions. */
inline void run_ta_b3_scenario (zlink::http_client::client_t &actor,
                                zlink::http_client::client_t &caller)
{
    prepare_failure (caller, "TA-B3-prepare", "ta-b3-disconnected");
    require_location (caller, "TA-B3-location", "ta-b3-disconnected", "present");
    assert_failure (caller, "TA-B3-route-not-connected", "ta-b3-disconnected",
                    "route_not_connected", false);
    ensure_ready (actor, caller, "TA-B3", "ta-b3");
    assert_call (caller, "TA-B3-route-restored", "ta-b3", "b3-request", "reply:b3-request", false);

    const auto evidence = actor.get ("/evidence").fetch<std::vector<e2e::actor_evidence_t>> ();
    require_no_evidence (evidence, "TA-B3-route-not-connected");
}

} // namespace
