/* SPDX-License-Identifier: MPL-2.0 */

#include "actor_sample_common.hpp"

int main ()
{
    zlink::context_t ctx;
    zlink::service::spot_node_t node (ctx);
    zlink::service::spot_t spot = node.create_spot ();
    zlink::service::actor_t actor = node.create_actor ("room-player-1");

    actor_sample_capture_t capture;
    actor_sample_dispatch_state_t state{&spot, &node, &actor, &capture};
    spot.set_dispatch_handler (
      [&state] (zlink::service::spot_t &, const zlink::spot_dispatch_info_t &info) {
          actor_sample_dispatch (state, info);
      });

    actor_sample_stream_session_t stream_session (ctx);
    stream_session.stream.attach_actor_gateway (node);
    (void) stream_session.stream.bind_actor (stream_session.session, actor.ref ())
      .timeout (std::chrono::milliseconds (1000))
      .submit_async ()
      .get ();

    zlink::message_t join = zlink::message_t::from ("enter-room");
    assert (actor.join (spot)
              .message (join)
              .flags (zlink::recv_flags_t::dontwait)
              .timeout (std::chrono::milliseconds (1000))
              .submit ([&] (const zlink::actor_join_result_t &result,
                            std::vector<zlink::message_t> parts) {
                  actor_sample_join_reply (capture, result, std::move (parts));
              }));
    assert (wait_until_flag (capture, &actor_sample_capture_t::joined));
    assert (capture.join_result == zlink::request_result_t::ok);

    zlink::message_t event = zlink::message_t::from ("move:north");
    assert (stream_session.stream.send_bound_actor (stream_session.session, "room-player-1")
              .message (event)
              .flags (zlink::recv_flags_t::dontwait)
              .submit ());
    assert (wait_until_flag (capture, &actor_sample_capture_t::actor_read));
    assert (capture.payload == "move:north");

    (void) actor.leave (spot).submit_async ().get ();
    actor.close ();
    std::printf ("[actor/room] stream payload: \"move:north\" -> actor: \"%s\"\n",
                 capture.payload.c_str ());
    return 0;
}
