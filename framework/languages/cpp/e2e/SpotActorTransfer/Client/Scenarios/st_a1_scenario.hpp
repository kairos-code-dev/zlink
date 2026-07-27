/* SPDX-License-Identifier: FSL-1.1-ALv2 */
#pragma once

#include "../Support/scenario_runner_support.hpp"

namespace
{

/* ST-A1: this file owns the scenario orchestration and its public assertions. */
inline void scenario_runner_t::run_st_a1_scenario ()
{
    const auto actor_id = "actor-local-ok-" + unique_suffix ();
    const auto spot_id = "spot-local-ok-" + unique_suffix ();
    const auto spot = create_spot (_nodes.a, spot_id);
    auto &target =
      spot.node_rid == "actor-a"
        ? _nodes.a
        : _nodes.b;
    create_actor (_nodes.a, actor_id, e2e::actor_type_stateful, 11);

    const auto join = join_actor (_nodes.a, actor_id, {"ST-A1", spot_id});
    require (join.accepted, "ST-A1 join was rejected.");

    const auto probe = probe_actor (_nodes.a, actor_id, {"ST-A1", "after-joined"});
    require (probe.node_rid == spot.node_rid,
             "ST-A1 probe expected " + spot.node_rid + ", got " + probe.node_rid);
    require (probe.spot_id == spot_id, "ST-A1 probe did not reach target spot.");

    if (spot.node_rid == "actor-a") {
        assert_evidence_sequence (
          target,
          {"ST-A1|" + actor_id + "|admission|spot=" + spot_id,
           "transfer|" + actor_id + "|leave|11",
           "transfer|" + actor_id + "|joined|" + spot_id + ":11",
           "ST-A1|" + actor_id + "|location_committed|" + spot_id});
    } else {
        wait_evidence (
          _nodes.a,
          {"transfer|" + actor_id + "|leave|11"});
        assert_evidence_sequence (
          target,
          {"ST-A1|" + actor_id + "|admission|spot=" + spot_id,
           "transfer|" + actor_id + "|transfer_in|11",
           "transfer|" + actor_id + "|joined|" + spot_id + ":11",
           "ST-A1|" + actor_id + "|location_committed|" + spot_id});
    }
    wait_evidence (_nodes.a, {"ST-A1|" + actor_id + "|success_reply|" + spot_id});
    wait_evidence (target, {"ST-A1|" + actor_id + "|packet_handler|after-joined"});
}

} // namespace
