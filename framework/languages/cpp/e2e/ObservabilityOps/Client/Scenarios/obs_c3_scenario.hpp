/* SPDX-License-Identifier: MPL-2.0 */
#pragma once
#include "../Support/scenario_context.hpp"

namespace zlink::framework::e2e::observability_ops::client::scenarios
{
inline void run_obs_c3_scenario (const verification_input_t &input)
{
    const auto drained = read_json (input, "drainedEvidence");
    require (has_drain_state (drained, "drained"), "OBS-C3 workflow did not drain");
    const auto rooms = metrics_named (drained, "zlink.drain.rooms.drained");
    require (!rooms.empty (), "OBS-C3 rooms.drained instrument is missing");
    for (const auto &metric : rooms) {
        require (metric.at ("tags").value ("policy", "") == "release_and_recreate",
                 "OBS-C3 reported the wrong Spot drain policy");
    }
    const auto state = read_json (input, "recreate").value ("state", "");
    require (state == "created", "OBS-C3 peer reused the released owner row");
    const auto replayed = read_json (input, "replayed");
    require (replayed.value ("value", -1) == 7,
             "OBS-C3 recreated workflow did not replay persisted state");
}
} // namespace zlink::framework::e2e::observability_ops::client::scenarios
