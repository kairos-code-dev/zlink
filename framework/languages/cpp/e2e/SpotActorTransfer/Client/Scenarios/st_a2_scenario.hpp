/* SPDX-License-Identifier: FSL-1.1-ALv2 */
#pragma once

#include "../Support/scenario_runner_support.hpp"

namespace
{

/* ST-A2: this file owns the scenario orchestration and its public assertions. */
inline void scenario_runner_t::run_st_a2_scenario ()
{
    const auto actor_id = "actor-local-reject-" + unique_suffix ();
    const auto spot_rid = "spot-local-reject-" + unique_suffix ();
    create_spot (_nodes.a, spot_rid, "reject");
    create_actor (_nodes.a, actor_id, e2e::actor_type_stateful, 12);

    const auto join = join_actor (_nodes.a, actor_id, {"ST-A2", spot_rid, "reject"});
    require (!join.accepted, "ST-A2 join should have been rejected.");

    const auto evidence =
      wait_evidence (_nodes.a, {"ST-A2|" + actor_id + "|admission|spot=" + spot_rid});
    require_no_contains (evidence, "transfer|" + actor_id + "|joined|" + spot_rid,
                         "ST-A2 joined side effect should not exist.");
}

} // namespace
