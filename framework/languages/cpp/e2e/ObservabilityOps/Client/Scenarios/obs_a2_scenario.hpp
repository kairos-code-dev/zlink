/* SPDX-License-Identifier: MPL-2.0 */
#pragma once
#include "../Support/scenario_context.hpp"

namespace zlink::framework::e2e::observability_ops::client::scenarios
{
inline void run_obs_a2_scenario (const verification_input_t &input)
{
    const auto lines = read_lines (input, "sessionLog");
    require (!flow_ids (lines, "phase=error").empty ()
               || !flow_ids (lines, "dispatch error").empty (),
             "OBS-A2 error line has no flow id");
}
} // namespace zlink::framework::e2e::observability_ops::client::scenarios
