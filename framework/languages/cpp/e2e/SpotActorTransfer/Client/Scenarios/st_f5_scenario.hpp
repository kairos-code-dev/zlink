/* SPDX-License-Identifier: FSL-1.1-ALv2 */
#pragma once

#include "../Support/scenario_runner_support.hpp"

namespace
{

/* ST-F5: this file owns the scenario orchestration and its public assertions. */
inline void scenario_runner_t::run_st_f5_scenario ()
{
    const auto actor_id = "actor-message-follow-chain-" + unique_suffix ();
    const auto spot_b = "spot-map-chain-b-" + unique_suffix ();
    const auto spot_a_final = "spot-map-chain-a-final-" + unique_suffix ();
    create_spot (_nodes.b, spot_b);
    create_spot (_nodes.a, spot_a_final);
    create_actor (_nodes.a, actor_id, e2e::actor_type_stateful, 105);
    const auto old_ref_a = get_actor_ref (_nodes.a, actor_id);
    require (join_actor (_nodes.a, actor_id, {"ST-F5", spot_b}).accepted,
             "ST-F5 first transfer was rejected.");
    wait_evidence (
      _nodes.a,
      {"message_flow|" + actor_id + "|message_follow_registered|actor-b:" + spot_b});
    const auto old_ref_b = get_actor_ref (_nodes.b, actor_id);
    require (join_actor (_nodes.b, actor_id, {"ST-F5", spot_a_final}).accepted,
             "ST-F5 chained transfer was rejected.");
    wait_evidence (_nodes.b,
                   {"message_flow|" + actor_id
                    + "|message_follow_registered|actor-a:" + spot_a_final});
    require (message_follow_entries (_nodes.a, actor_id).size () == 1,
             "ST-F5 actor-a retained more than one Message Follow route.");
    require (message_follow_entries (_nodes.b, actor_id).size () == 1,
             "ST-F5 actor-b retained more than one Message Follow route.");

    send_ref (_nodes.a, actor_id, old_ref_a, {"ST-F5", "chain-to-final"});
    wait_evidence (_nodes.a, {"ST-F5|" + actor_id + "|handoff_packet|chain-to-final"});

    wait_evidence (
      _nodes.a, {"message_flow|" + actor_id + "|message_follow_route_removed|"});
    wait_evidence (
      _nodes.b, {"message_flow|" + actor_id + "|message_follow_route_removed|"});
    const auto stale = probe_ref (
      _nodes.a, actor_id, old_ref_a, {"ST-F5", "after-message-follow-removal"});
    require (
      !stale.succeeded
        && (stale.error_kind == "ActorLocationStale" || stale.error_kind == "ActorRouteNotFound"),
      "ST-F5 expected the removed Message Follow route to fail stale, got '"
        + stale.error_kind + "'.");
    const auto stale_b = probe_ref (
      _nodes.b, actor_id, old_ref_b, {"ST-F5", "after-message-follow-removal-b"});
    require (!stale_b.succeeded
               && (stale_b.error_kind == "ActorLocationStale"
                   || stale_b.error_kind == "ActorRouteNotFound"),
             "ST-F5 expected node-b Message Follow route removal, got '"
               + stale_b.error_kind + "'.");
    const auto evidence = get_evidence (_nodes.a);
    require_no_contains (
      evidence,
      "ST-F5|" + actor_id + "|packet_handler|after-message-follow-removal",
      "ST-F5 packet sent after Message Follow removal reached the target handler.");
}

} // namespace
