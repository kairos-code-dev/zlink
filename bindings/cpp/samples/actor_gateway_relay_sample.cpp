/* SPDX-License-Identifier: MPL-2.0 */

#include "actor_sample_common.hpp"

int main ()
{
    zlink::context_t ctx;
    zlink::service::spot_node_t gateway_node (ctx);
    zlink::service::spot_node_t play_node (ctx);
    zlink::service::spot_t play_spot = play_node.create_spot ();

    actor_sample_capture_t capture;
    zlink::routing_id_t play_node_rid = play_node.routing_id ();
    zlink::service::actor_t play_actor = gateway_node.create_actor ("play-session-actor");
    assert (play_actor.valid ());
    zlink::actor_ref_t concrete = play_actor.ref ();

    actor_sample_dispatch_state_t state{&play_spot, &play_node, &play_actor, &capture};
    play_spot.set_dispatch_handler (
      [&state] (zlink::service::spot_t &, const zlink::spot_dispatch_info_t &info) {
          actor_sample_dispatch (state, info);
      });

    actor_sample_stream_session_t stream_session (ctx);
    stream_session.stream.attach_actor_gateway (gateway_node);
    (void) stream_session.stream.bind_actor (stream_session.session, concrete)
      .timeout (std::chrono::milliseconds (1000))
      .async ()
      .get ();

    zlink::routing_id_t play_spot_rid = play_spot.routing_id ();
    zlink::message_t join = zlink::message_t::from ("join-play");
    assert (gateway_node.join_actor (concrete, play_node_rid, play_spot_rid)
              .message (join)
              .flags (zlink::recv_flags_t::dontwait)
              .timeout (std::chrono::milliseconds (1000))
              .submit ([&] (const zlink::actor_join_result_t &result,
                            std::vector<zlink::message_t> parts) {
                  actor_sample_join_reply (capture, result, std::move (parts));
              }));
    assert (wait_until_flag (capture, &actor_sample_capture_t::joined));
    assert (capture.join_result == zlink::request_result_t::ok);
    concrete = capture.joined_actor;

    zlink::message_t frame = zlink::message_t::from ("client-input");
    assert (stream_session.stream.send_bound_actor (stream_session.session, "play-session-actor")
              .message (frame)
              .flags (zlink::recv_flags_t::dontwait)
              .submit ());
    assert (wait_until_flag (capture, &actor_sample_capture_t::actor_read));
    assert (capture.payload == "client-input");

    (void) gateway_node.leave_actor (concrete, play_spot_rid)
      .timeout (std::chrono::milliseconds (1000))
      .async ()
      .get ();
    (void) gateway_node.destroy_actor (concrete)
      .timeout (std::chrono::milliseconds (1000))
      .async ()
      .get ();
    std::printf ("[actor/gateway] stream payload: \"client-input\" -> actor: \"%s\"\n",
                 capture.payload.c_str ());
    return 0;
}
