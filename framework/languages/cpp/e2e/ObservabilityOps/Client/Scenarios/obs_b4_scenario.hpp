/* SPDX-License-Identifier: MPL-2.0 */
#pragma once
#include "../Support/scenario_context.hpp"

namespace zlink::framework::e2e::observability_ops::client::scenarios
{
inline void run_obs_b4_scenario (const verification_input_t &input)
{
    require (read_json (input, "offNodeEvidence").at ("metrics").empty (),
             "OBS-B4 reader-less node accumulated metric samples");
}
} // namespace zlink::framework::e2e::observability_ops::client::scenarios
