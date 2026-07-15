/* SPDX-License-Identifier: MPL-2.0 */
#pragma once
#include "../Support/scenario_context.hpp"

namespace zlink::framework::e2e::observability_ops::client::scenarios
{
inline void run_obs_b3_scenario (const verification_input_t &input)
{
    const auto published =
      metrics_named (read_json (input, "publisherEvidence"), "zlink.fanout.published");
    const auto received =
      metrics_named (read_json (input, "subscriberEvidence"), "zlink.fanout.received");
    require (!published.empty () && !received.empty (), "OBS-B3 fanout instruments are missing");
    for (const auto &metric : published) {
        require (metric.at ("tags").value ("topic", "") == "obs.projection",
                 "OBS-B3 publisher topic label is not closed");
    }
}
} // namespace zlink::framework::e2e::observability_ops::client::scenarios
