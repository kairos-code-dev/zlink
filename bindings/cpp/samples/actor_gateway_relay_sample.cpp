/* SPDX-License-Identifier: MPL-2.0 */

#include "actor_sample_common.hpp"

int main ()
{
    zlink::context_t ctx;
    zlink::service::spot_node_t gateway_node (ctx);
    zlink::service::spot_node_t play_node (ctx);
    zlink::service::spot_t play_spot = play_node.create_spot ();

    actor_sample_capture_t capture;
    play_node.on_actor_admission (
      [] (const std::string &, const zlink::message_t &) {
          return zlink::actor_admission_result_t::accept;
      });

    zlink::routing_id_t play_node_rid = play_node.routing_id ();
    zlink::service::actor_t play_actor =
      play_node.create_actor ("play-session-actor");
    assert (play_actor.valid ());
    zlink::actor_ref_t concrete = play_actor.ref ();

    actor_sample_dispatch_state_t state {
      &play_spot, &play_node, &play_actor, &capture};
    play_spot.on_dispatch_event (
      [&state] (zlink::service::spot_t &,
                const zlink::spot_dispatch_info_t &info) {
          actor_sample_dispatch (state, info);
      });

    zlink::stream_socket_t stream (ctx);
    zlink::routing_id_t session = sample_rid ("gateway-session");
    stream.bind_actor (gateway_node, session, concrete,
                       std::chrono::milliseconds (1000));

    zlink::routing_id_t play_spot_rid = play_spot.routing_id ();
    zlink::message_t join = zlink::message_t::from_string ("join-play");
    assert (gateway_node.join_actor (
      concrete, play_node_rid, play_spot_rid, join,
      [&] (zlink::request_result_t result,
           std::vector<zlink::message_t> parts) {
          actor_sample_join_reply (capture, result, std::move (parts));
      },
      zlink::send_flags_t::dontwait, std::chrono::milliseconds (1000)));
    assert (wait_until_flag (capture, &actor_sample_capture_t::joined));
    assert (capture.join_result == zlink::request_result_t::ok);

    zlink::message_t frame = zlink::message_t::from_string ("client-input");
    assert (stream.send_bound_actor_part (
      gateway_node, session, "play-session-actor", frame,
      zlink::send_flags_t::dontwait));
    assert (wait_until_flag (capture, &actor_sample_capture_t::actor_read));
    assert (capture.payload == "client-input");

    gateway_node.leave_actor (concrete, play_spot_rid,
                              std::chrono::milliseconds (1000));
    gateway_node.destroy_actor (concrete, std::chrono::milliseconds (1000));
    return 0;
}
