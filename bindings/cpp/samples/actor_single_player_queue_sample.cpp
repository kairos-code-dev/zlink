/* SPDX-License-Identifier: MPL-2.0 */

#include "actor_sample_common.hpp"

static void join_only_dispatch (void *,
                                const zlink_spot_dispatch_info_t *info_,
                                void *userdata_)
{
    actor_sample_dispatch_state_t *state =
      static_cast<actor_sample_dispatch_state_t *> (userdata_);
    if (info_->event != ZLINK_SPOT_DISPATCH_EVENT_ACTOR_JOIN_READABLE)
        return;

    auto request = state->spot->recv_actor_join (zlink::recv_flags_t::dontwait);
    assert (request.has_value ());
    zlink::message_t reply = zlink::message_t::from_string ("accepted");
    state->spot->reply_actor_join (request->first, true, reply);
}

int main ()
{
    zlink::context_t ctx;
    zlink::service::spot_node_t node (ctx);
    zlink::service::spot_t first_spot = node.create_spot ();
    zlink::service::spot_t second_spot = node.create_spot ();
    zlink::service::actor_t actor = node.create_actor ("single-player");

    actor_sample_capture_t first_capture;
    actor_sample_dispatch_state_t first_state {&first_spot, &actor, &first_capture};
    first_spot.on_dispatch_event (&join_only_dispatch, &first_state);
    zlink::message_t join_first = zlink::message_t::from_string ("join-first");
    assert (actor.join (
      first_spot, join_first,
      [&] (zlink::request_result_t result,
           std::vector<zlink::message_t> parts) {
          actor_sample_join_reply (first_capture, result, std::move (parts));
      },
      zlink::send_flags_t::dontwait, std::chrono::milliseconds (1000)));
    assert (wait_until_flag (first_capture, &actor_sample_capture_t::joined));
    assert (first_capture.join_result == zlink::request_result_t::ok);

    zlink::stream_socket_t stream (ctx);
    zlink::routing_id_t session = sample_rid ("single-player-session");
    zlink::actor_ref_t ref = actor.ref ();
    stream.bind_actor (node, session, ref, std::chrono::milliseconds (1000));

    zlink::message_t before = zlink::message_t::from_string ("before-");
    assert (stream.send_bound_actor_part (
      node, session, "single-player", before, zlink::send_flags_t::dontwait));
    actor.leave (first_spot);

    zlink::message_t between = zlink::message_t::from_string ("between-");
    assert (stream.send_bound_actor_part (
      node, session, "single-player", between, zlink::send_flags_t::dontwait));

    actor_sample_capture_t second_capture;
    actor_sample_dispatch_state_t second_state {&second_spot, &actor,
                                                &second_capture};
    second_spot.on_dispatch_event (&actor_sample_dispatch, &second_state);
    zlink::message_t join_second = zlink::message_t::from_string ("join-second");
    assert (node.join_actor (
      ref, second_spot.routing_id (), join_second,
      [&] (zlink::request_result_t result,
           std::vector<zlink::message_t> parts) {
          actor_sample_join_reply (second_capture, result, std::move (parts));
      },
      zlink::send_flags_t::dontwait, std::chrono::milliseconds (1000)));
    assert (wait_until_flag (second_capture, &actor_sample_capture_t::joined));
    assert (second_capture.join_result == zlink::request_result_t::ok);
    assert (wait_until_flag (second_capture, &actor_sample_capture_t::actor_read));
    assert (second_capture.payload == "before-between-");

    actor.leave (second_spot);
    actor.close ();
    return 0;
}
