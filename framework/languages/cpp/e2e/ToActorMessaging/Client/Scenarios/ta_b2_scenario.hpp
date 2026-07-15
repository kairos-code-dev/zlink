/* SPDX-License-Identifier: FSL-1.1-ALv2 */
#pragma once

#include "../Support/scenario_context.hpp"

namespace
{

/* TA-B2: the client owns the requests, failure classification, and evidence assertions. */
inline void run_ta_b2_scenario (zlink::http_client::client_t &actor,
                                zlink::http_client::client_t &caller)
{
    ensure_ready (actor, caller, "TA-B2", "ta-b2-stale");
    prepare_failure (caller, "TA-B2-prepare", "ta-b2-stale");
    require_location (caller, "TA-B2-location", "ta-b2-stale", "present");
    assert_failure (caller, "TA-B2-stale-location", "ta-b2-stale", "actor_location_stale", false);

    const auto evidence = actor.get ("/evidence").fetch<std::vector<e2e::actor_evidence_t>> ();
    require_no_evidence (evidence, "TA-B2-stale-location");
}

} // namespace
