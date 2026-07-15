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
    require (state == "created" || state == "existing", "OBS-C3 peer did not recreate the room");
}
} // namespace zlink::framework::e2e::observability_ops::client::scenarios
