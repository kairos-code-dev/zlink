/* SPDX-License-Identifier: MPL-2.0 */
#pragma once
#include "../Support/scenario_context.hpp"

namespace zlink::framework::e2e::observability_ops::client::scenarios
{
inline void run_obs_c3_scenario (const verification_input_t &input)
{
    const auto natural = read_json (input, "naturalEvidence");
    require (has_drain_state (natural, "draining") && has_drain_state (natural, "drained"),
             "OBS-C3 drain-natural did not wait for the room's natural close");
    const auto natural_rooms = metrics_named (natural, "zlink.drain.rooms.drained");
    require (natural_rooms.size () == 1
               && natural_rooms.front ().at ("tags").value ("policy", "")
                    == "drain_natural"
               && natural_rooms.front ().at ("value").get<double> () == 1,
             "OBS-C3 drain-natural room count is not exactly one");

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
