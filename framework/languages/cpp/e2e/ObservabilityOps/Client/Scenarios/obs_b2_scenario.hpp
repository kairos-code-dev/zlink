/* SPDX-License-Identifier: MPL-2.0 */
#pragma once
#include "../Support/scenario_context.hpp"

namespace zlink::framework::e2e::observability_ops::client::scenarios
{
inline void run_obs_b2_scenario (const verification_input_t &input)
{
    const auto body = read_json (input, "playEvidence");
    const auto depth = metrics_named (body, "zlink.spot.queue.depth");
    const auto wait = metrics_named (body, "zlink.spot.queue.wait.duration");
    require (!depth.empty () && !wait.empty (), "OBS-B2 Spot queue instruments are missing");
    double total = 0;
    for (const auto &metric : depth) {
        total += metric.at ("value").get<double> ();
    }
    require (total == 0, "OBS-B2 Spot queue depth did not return to zero");
}
} // namespace zlink::framework::e2e::observability_ops::client::scenarios
