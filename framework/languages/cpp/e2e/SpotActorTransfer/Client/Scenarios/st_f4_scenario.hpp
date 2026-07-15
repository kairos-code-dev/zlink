/* SPDX-License-Identifier: FSL-1.1-ALv2 */
#pragma once

#include "../Support/scenario_runner_support.hpp"

namespace
{

/* ST-F4: this file owns the scenario orchestration and its public assertions. */
inline void scenario_runner_t::run_st_f4_scenario ()
{
    const auto setup = transfer_for_straggler ("ST-F4", 104);
    send_ref (_nodes.a, setup.actor_id, setup.old_ref, {"ST-F4", "G1"});
    wait_evidence (_nodes.a, {"message_flow|" + setup.actor_id + "|straggler_forward|"});
    wait_evidence (_nodes.b, {"ST-F4|" + setup.actor_id + "|handoff_packet|G1"});

    wait_evidence (_nodes.a, {"message_flow|" + setup.actor_id + "|mapping_evicted|"});
    send_ref (_nodes.a, setup.actor_id, setup.old_ref, {"ST-F4", "G2"});
    wait_evidence (_nodes.a, {"message_flow|" + setup.actor_id + "|stale_fail_fast|"});
    require_no_contains (get_evidence (_nodes.b), "ST-F4|" + setup.actor_id + "|handoff_packet|G2",
                         "ST-F4 stale G2 send was automatically re-resolved and delivered.");
}

} // namespace
