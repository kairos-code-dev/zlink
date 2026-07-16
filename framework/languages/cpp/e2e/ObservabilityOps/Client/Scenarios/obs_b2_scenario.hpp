/* SPDX-License-Identifier: MPL-2.0 */
#pragma once
#include "../Support/scenario_context.hpp"

namespace zlink::framework::e2e::observability_ops::client::scenarios
{
inline void run_obs_b2_scenario (const verification_input_t &input)
{
    const auto queue = read_json (input, "queueEvidence");
    const auto transfer = read_json (input, "transferEvidence");
    const auto depth = metrics_named (queue, "zlink.spot.queue.depth");
    const auto wait = metrics_named (queue, "zlink.spot.queue.wait.duration");
    require (!depth.empty () && !wait.empty (), "OBS-B2 Spot queue instruments are missing");
    double total = 0;
    double maximum = 0;
    for (const auto &metric : depth) {
        require (metric.at ("tags").value ("kind", "") == "user",
                 "OBS-B2 queue depth is not separated as kind=user");
        total += metric.at ("value").get<double> ();
        maximum = std::max (maximum, metric.at ("value").get<double> ());
    }
    require (maximum >= 1 && total == 0,
             "OBS-B2 room load was not observed or queue depth did not return to zero");
    require (std::all_of (wait.begin (), wait.end (), [] (const auto &metric) {
                 return metric.at ("kind") == "histogram"
                        && metric.at ("tags").value ("kind", "") == "user";
             }),
             "OBS-B2 queue wait samples are not kind=user histograms");

    const auto transfers = metrics_named (transfer, "zlink.actor.transfers");
    const auto durations = metrics_named (transfer, "zlink.actor.transfer.duration");
    const auto pending =
      metrics_named (transfer, "zlink.actor.transfer.pending_requests.count");
    require (metric_total (transfer, "zlink.actor.transfers") == 1,
             "OBS-B2 actor transfer counter is not exactly one");
    require (transfers.size () == 1 && durations.size () == 1
               && durations.front ().at ("kind") == "histogram"
               && durations.front ().at ("value").get<double> () > 0,
             "OBS-B2 transfer duration does not cover the completed transfer");
    require (pending.size () == 1 && pending.front ().at ("kind") == "histogram",
             "OBS-B2 pending request count was not recorded once before moving");
}
} // namespace zlink::framework::e2e::observability_ops::client::scenarios
