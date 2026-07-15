/* SPDX-License-Identifier: FSL-1.1-ALv2 */
#pragma once

#include "../Support/scenario_context.hpp"

namespace
{

/* TA-A4: the client owns the requests, failure classification, and evidence assertions. */
inline void run_ta_a4_scenario (zlink::http_client::client_t &actor,
                                zlink::http_client::client_t &caller)
{
    ensure_ready (actor, caller, "TA-A4", "ta-a4");
    assert_call (caller, "TA-A4-disconnected-send", "ta-a4", "a4-send", "sent", true);
    assert_call (caller, "TA-A4-disconnected-request", "ta-a4", "a4-request", "reply:a4-request",
                 false);
    assert_failure (caller, "TA-A4-destroyed", "ta-a4-destroyed", "actor_route_not_found", false);

    const auto evidence = actor.get ("/evidence").fetch<std::vector<e2e::actor_evidence_t>> ();
    require_evidence (evidence, "TA-A4-disconnected-send", "send");
}

} // namespace
