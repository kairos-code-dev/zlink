/* SPDX-License-Identifier: FSL-1.1-ALv2 */
#pragma once

#include "../Support/scenario_context.hpp"

namespace
{

/* TA-A2: the client owns the requests, failure classification, and evidence assertions. */
inline void run_ta_a2_scenario (zlink::http_client::client_t &actor,
                                zlink::http_client::client_t &caller)
{
    ensure_ready (actor, caller, "TA-A2", "ta-a2");
    assert_call (caller, "TA-A2-unbound-send", "ta-a2", "a2-send", "sent", true);
    assert_call (caller, "TA-A2-unbound-request", "ta-a2", "a2-request", "reply:a2-request", false);

    const auto evidence = actor.get ("/evidence").fetch<std::vector<e2e::actor_evidence_t>> ();
    require_evidence (evidence, "TA-A2-unbound-send", "send");
}

} // namespace
