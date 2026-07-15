/* SPDX-License-Identifier: MPL-2.0 */
#pragma once
#include "../Support/scenario_context.hpp"

namespace zlink::framework::e2e::observability_ops::client::scenarios
{
inline void run_obs_a4_scenario (const verification_input_t &input)
{
    const auto publisher = flow_ids (read_lines (input, "publisherLog"), "phase=sent");
    const auto subscriber = flow_ids (read_lines (input, "subscriberLog"), "spot_subscription");
    std::vector<std::string> shared;
    std::set_intersection (publisher.begin (), publisher.end (), subscriber.begin (),
                           subscriber.end (), std::back_inserter (shared));
    require (!shared.empty (), "OBS-A4 subscriber did not preserve publish flow");
    require (has_line (read_lines (input, "timerLog"), "origin=timer"),
             "OBS-A4 timer publish has no timer origin");
}
} // namespace zlink::framework::e2e::observability_ops::client::scenarios
