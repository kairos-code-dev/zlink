/* SPDX-License-Identifier: FSL-1.1-ALv2 */
#pragma once

#include "../Support/scenario_context.hpp"

namespace
{

/* TA-A1: the client owns the requests, failure classification, and evidence assertions. */
inline void run_ta_a1_scenario (zlink::http_client::client_t &actor,
                                zlink::http_client::client_t &caller)
{
    ensure_ready (actor, caller, "TA-A1", "ta-a1");
    assert_call (caller, "TA-A1-send", "ta-a1", "a1-send", "sent", true);
    assert_call (caller, "TA-A1-request", "ta-a1", "a1-request", "reply:a1-request", false);

    const auto evidence = actor.get ("/evidence").fetch<std::vector<e2e::actor_evidence_t>> ();
    require_evidence (evidence, "TA-A1-send", "send");
    require_evidence (evidence, "TA-A1-request", "request");
}

} // namespace
