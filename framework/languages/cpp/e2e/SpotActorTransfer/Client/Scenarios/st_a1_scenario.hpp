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
    const auto node_a_store_before =
      get_relocation_store_activity (_nodes.a);
    const auto node_b_store_before =
      get_relocation_store_activity (_nodes.b);
    const auto spot = create_spot (_nodes.a, spot_id);
    require (spot.node_rid == "actor-a",
             "ST-A1 target Spot must be created on actor-a, got " + spot.node_rid);
    const auto actor =
      create_actor (_nodes.a, actor_id, e2e::actor_type_stateful, 11);
    require (actor.node_rid == "actor-a",
             "ST-A1 source Actor must be created on actor-a, got " + actor.node_rid);

    const auto join = join_actor (_nodes.a, actor_id, {"ST-A1", spot_id});
    require (join.accepted, "ST-A1 join was rejected.");

    const auto probe = probe_actor (_nodes.a, actor_id, {"ST-A1", "after-joined"});
    require (probe.node_rid == spot.node_rid,
             "ST-A1 probe expected " + spot.node_rid + ", got " + probe.node_rid);
    require (probe.spot_id == spot_id, "ST-A1 probe did not reach target spot.");

    assert_evidence_sequence (
      _nodes.a,
      {"ST-A1|" + actor_id + "|admission|spot=" + spot_id,
       "ST-A1|" + actor_id + "|authority_committed|" + spot_id,
       "transfer|" + actor_id + "|leave|11",
       "transfer|" + actor_id + "|joined|" + spot_id + ":11",
       "ST-A1|" + actor_id + "|success_reply|" + spot_id});
    wait_evidence (_nodes.a, {"ST-A1|" + actor_id + "|packet_handler|after-joined"});

    require (
      get_relocation_store_activity (_nodes.a) == node_a_store_before
        && get_relocation_store_activity (_nodes.b) == node_b_store_before,
      "ST-A1 local join accessed the Relocation Store.");
    for (const auto *node : {&_nodes.a, &_nodes.b}) {
        for (const auto &entry : get_evidence (*node)) {
            if (entry.actor_id != actor_id) {
                continue;
            }
            require (entry.kind.find ("message_follow") == std::string::npos,
                     "ST-A1 local join used Message Follow.");
            require (entry.kind != "transfer_out" && entry.kind != "transfer_in",
                     "ST-A1 local join used the remote relocation adapter.");
        }
    }
}

} // namespace
