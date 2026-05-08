/* SPDX-License-Identifier: MPL-2.0 */

#include "actor_sample_common.hpp"

static void join_only_dispatch (
  actor_sample_dispatch_state_t &state_,
  const zlink::spot_dispatch_info_t &info_)
{
    if (info_.event != zlink::spot_dispatch_event_t::actor_join_readable)
        return;

    auto request = state_.spot->recv_actor_join (ZLINK_DONTWAIT);
    assert (request.has_value ());
    zlink::message_t reply = zlink::message_t::from_string ("accepted");
    state_.spot->reply_actor_join (*request, true, reply);
}

int main ()
{
    zlink::context_t ctx;
    zlink::service::spot_node_t node (ctx);
    zlink::service::spot_t first_spot = node.create_spot ();
    zlink::service::spot_t second_spot = node.create_spot ();
    zlink::service::actor_t actor = node.create_actor ("single-player");
    zlink::stream_socket_t stream (ctx);
    zlink::routing_id_t session = sample_rid ("single-player-session");
    zlink::actor_ref_t ref = actor.ref ();
    stream.bind_actor (node, session, ref, std::chrono::milliseconds (1000));

    actor_sample_capture_t first_capture;
    actor_sample_dispatch_state_t first_state {
      &first_spot, &node, &actor, &first_capture};
    first_spot.on_dispatch_event (
      [&first_state] (zlink::service::spot_t &,
                      const zlink::spot_dispatch_info_t &info) {
          join_only_dispatch (first_state, info);
      });
    zlink::message_t join_first = zlink::message_t::from_string ("join-first");
    assert (actor.join (
      first_spot, join_first,
      [&] (zlink::request_result_t result,
           std::vector<zlink::message_t> parts) {
          actor_sample_join_reply (first_capture, result, std::move (parts));
      },
      ZLINK_DONTWAIT, std::chrono::milliseconds (1000)));
    assert (wait_until_flag (first_capture, &actor_sample_capture_t::joined));
    assert (first_capture.join_result == zlink::request_result_t::ok);

    zlink::message_t before = zlink::message_t::from_string ("before-");
    assert (stream.send_bound_actor (
      node, session, "single-player", before, ZLINK_DONTWAIT));
    actor.leave (first_spot);

    zlink::message_t between = zlink::message_t::from_string ("between-");
    assert (stream.send_bound_actor (
      node, session, "single-player", between, ZLINK_DONTWAIT));

    actor_sample_capture_t second_capture;
    actor_sample_dispatch_state_t second_state {&second_spot, &node,
                                                &actor, &second_capture};
    second_spot.on_dispatch_event (
      [&second_state] (zlink::service::spot_t &,
                       const zlink::spot_dispatch_info_t &info) {
          actor_sample_dispatch (second_state, info);
      });
    zlink::message_t join_second = zlink::message_t::from_string ("join-second");
    assert (node.join_actor (
      ref, second_spot.routing_id (), join_second,
      [&] (zlink::request_result_t result,
           std::vector<zlink::message_t> parts) {
          actor_sample_join_reply (second_capture, result, std::move (parts));
      },
      ZLINK_DONTWAIT, std::chrono::milliseconds (1000)));
    assert (wait_until_flag (second_capture, &actor_sample_capture_t::joined));
    assert (second_capture.join_result == zlink::request_result_t::ok);
    assert (wait_until_flag (second_capture, &actor_sample_capture_t::actor_read));
    assert (second_capture.payload == "before-between-");

    actor.leave (second_spot);
    actor.close ();
    return 0;
}
