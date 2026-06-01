/* SPDX-License-Identifier: MPL-2.0 */

#include "actor_sample_common.hpp"

static void join_only_dispatch (
  actor_sample_dispatch_state_t &state_,
  const zlink::spot_dispatch_info_t &info_)
{
    if (info_.event != zlink::spot_dispatch_event_t::actor_join_readable)
        return;

    auto request = state_.spot->recv_actor_join (zlink::recv_flags_t::dontwait);
    assert (request.has_value ());
    zlink::message_t reply = zlink::message_t::from ("accepted");
    state_.spot->reply_actor_join (*request, 0).message (reply).submit ();
}

int main ()
{
    zlink::context_t ctx;
    zlink::service::spot_node_t node (ctx);
    zlink::service::spot_t first_spot = node.create_spot ();
    zlink::service::spot_t second_spot = node.create_spot ();
    zlink::service::actor_t actor = node.create_actor ("single-player");
    zlink::actor_ref_t ref = actor.ref ();
    actor_sample_stream_session_t stream_session (ctx);
    stream_session.stream.attach_actor_gateway (node);
    (void) stream_session.stream.bind_actor (stream_session.session, ref)
      .timeout (std::chrono::milliseconds (1000))
      .submit_async ()
      .get ();

    actor_sample_capture_t first_capture;
    actor_sample_dispatch_state_t first_state {
      &first_spot, &node, &actor, &first_capture};
    first_spot.set_dispatch_handler (
      [&first_state] (zlink::service::spot_t &,
                      const zlink::spot_dispatch_info_t &info) {
          join_only_dispatch (first_state, info);
      });
    zlink::message_t join_first = zlink::message_t::from ("join-first");
    assert (actor.join (first_spot)
      .message (join_first)
      .flags (zlink::recv_flags_t::dontwait)
      .timeout (std::chrono::milliseconds (1000))
      .submit (
      [&] (const zlink::actor_join_result_t &result,
           std::vector<zlink::message_t> parts) {
          actor_sample_join_reply (first_capture, result, std::move (parts));
      }));
    assert (wait_until_flag (first_capture, &actor_sample_capture_t::joined));
    assert (first_capture.join_result == zlink::request_result_t::ok);

    zlink::message_t before = zlink::message_t::from ("before-");
    assert (stream_session.stream.send_bound_actor (
      stream_session.session, "single-player")
      .message (before)
      .flags (zlink::recv_flags_t::dontwait)
      .submit ());
    (void) actor.leave (first_spot).submit_async ().get ();

    zlink::message_t between = zlink::message_t::from ("between-");
    assert (stream_session.stream.send_bound_actor (
      stream_session.session, "single-player")
      .message (between)
      .flags (zlink::recv_flags_t::dontwait)
      .submit ());

    actor_sample_capture_t second_capture;
    actor_sample_dispatch_state_t second_state {&second_spot, &node,
                                                &actor, &second_capture};
    second_spot.set_dispatch_handler (
      [&second_state] (zlink::service::spot_t &,
                       const zlink::spot_dispatch_info_t &info) {
          actor_sample_dispatch (second_state, info);
      });
    zlink::message_t join_second = zlink::message_t::from ("join-second");
    assert (node.join_actor (ref, node.routing_id (), second_spot.routing_id ())
      .message (join_second)
      .flags (zlink::recv_flags_t::dontwait)
      .timeout (std::chrono::milliseconds (1000))
      .submit (
      [&] (const zlink::actor_join_result_t &result,
           std::vector<zlink::message_t> parts) {
          actor_sample_join_reply (second_capture, result, std::move (parts));
      }));
    assert (wait_until_flag (second_capture, &actor_sample_capture_t::joined));
    assert (second_capture.join_result == zlink::request_result_t::ok);
    assert (wait_until_flag (second_capture, &actor_sample_capture_t::actor_read));
    assert (second_capture.payload == "before-between-");

    (void) actor.leave (second_spot).submit_async ().get ();
    actor.close ();
    std::printf (
      "[actor/single-player] queued payload: \"before-between-\" -> actor: \"%s\"\n",
      second_capture.payload.c_str ());
    return 0;
}
