/* SPDX-License-Identifier: MPL-2.0 */
#pragma once
#include "../Support/scenario_context.hpp"

namespace zlink::framework::e2e::observability_ops::client::scenarios
{
inline void run_obs_a3_scenario (const verification_input_t &input)
{
    const auto downstream = read_lines (input, "downstreamLog");
    require (!flow_ids (downstream, "origin=application").empty (),
             "OBS-A3 downstream flow was not propagated");
    for (const auto &line : read_optional_lines (input, "offNodeLog")) {
        require (line.find ("flow=") == std::string::npos,
                 "OBS-A3 tracing-off node emitted a flow line");
    }
}
} // namespace zlink::framework::e2e::observability_ops::client::scenarios
