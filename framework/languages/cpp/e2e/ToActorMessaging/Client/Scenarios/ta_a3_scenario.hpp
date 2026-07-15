/* SPDX-License-Identifier: FSL-1.1-ALv2 */
#pragma once

#include "../Support/scenario_context.hpp"

namespace
{

/* TA-A3: the client owns the requests, failure classification, and evidence assertions. */
inline void run_ta_a3_scenario (zlink::http_client::client_t &actor,
                                zlink::http_client::client_t &caller)
{
    assert_failure (caller, "TA-A3-before-bind", "ta-a3-missing", "actor_route_not_found", false);
    ensure_ready (actor, caller, "TA-A3", "ta-a3");
    assert_call (caller, "TA-A3-after-bind-send", "ta-a3", "a3-send", "sent", true);
    assert_call (caller, "TA-A3-after-bind-request", "ta-a3", "a3-request", "reply:a3-request",
                 false);

    const auto evidence = actor.get ("/evidence").fetch<std::vector<e2e::actor_evidence_t>> ();
    require_evidence (evidence, "TA-A3-after-bind-request", "request");
}

} // namespace
