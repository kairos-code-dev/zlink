/* SPDX-License-Identifier: MPL-2.0 */
#pragma once
#include "../Support/scenario_context.hpp"

namespace zlink::framework::e2e::observability_ops::client::scenarios
{
inline void run_obs_a1_scenario (const verification_input_t &input)
{
    const auto session = read_lines (input, "sessionLog");
    const auto spot = read_lines (input, "spotLog");
    const auto session_ids = flow_ids (session);
    const auto spot_ids = flow_ids (spot);
    std::vector<std::string> shared;
    std::set_intersection (session_ids.begin (), session_ids.end (), spot_ids.begin (),
                           spot_ids.end (), std::back_inserter (shared));
    require (!shared.empty (), "OBS-A1 has no flow shared by Session and room Spot");
    require (has_line (session, "origin=application"),
             "OBS-A1 shared flow has no application origin");
}
} // namespace zlink::framework::e2e::observability_ops::client::scenarios
