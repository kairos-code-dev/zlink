/* SPDX-License-Identifier: MPL-2.0 */

#include "actor_sample_common.hpp"

int main ()
{
    zlink::context_t ctx;
    zlink::service::spot_node_t node (ctx);
    zlink::service::spot_t spot = node.create_spot ();
    zlink::service::actor_t actor = node.create_actor ("room-player-1");

    actor_sample_capture_t capture;
    actor_sample_dispatch_state_t state {&spot, &node, &capture};
    spot.on_dispatch_event (&actor_sample_dispatch, &state);

    zlink::stream_socket_t stream (ctx);
    zlink::routing_id_t session = sample_rid ("room-session");
    stream.bind_actor (node, session, actor.ref (), std::chrono::milliseconds (1000));

    zlink::message_t join = zlink::message_t::from_string ("enter-room");
    assert (actor.join (
      spot, join,
      [&] (zlink::request_result_t result,
           std::vector<zlink::message_t> parts) {
          actor_sample_join_reply (capture, result, std::move (parts));
      },
      zlink::send_flags_t::dontwait, std::chrono::milliseconds (1000)));
    assert (wait_until_flag (capture, &actor_sample_capture_t::joined));
    assert (capture.join_result == zlink::request_result_t::ok);

    zlink::message_t event = zlink::message_t::from_string ("move:north");
    assert (stream.send_bound_actor_part (
      node, session, "room-player-1", event, zlink::send_flags_t::dontwait));
    assert (wait_until_flag (capture, &actor_sample_capture_t::actor_read));
    assert (capture.payload == "move:north");

    actor.leave (spot);
    actor.close ();
    return 0;
}
