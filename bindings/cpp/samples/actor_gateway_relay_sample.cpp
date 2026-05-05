/* SPDX-License-Identifier: MPL-2.0 */

#include "actor_sample_common.hpp"

static zlink_actor_admission_result_t accept_actor (
  void *, const char *, const zlink_msg_t *, void *)
{
    return ZLINK_ACTOR_ADMISSION_ACCEPT;
}

int main ()
{
    zlink::context_t ctx;
    zlink::service::spot_node_t gateway_node (ctx);
    zlink::service::spot_node_t play_node (ctx);
    zlink::service::spot_t play_spot = play_node.create_spot ();

    actor_sample_capture_t capture;
    play_node.on_actor_admission (&accept_actor);

    zlink::routing_id_t play_node_rid = play_node.routing_id ();
    zlink::message_t create_message = zlink::message_t::from_string ("spawn");
    zlink::actor_create_result_t created = gateway_node.create_remote_actor (
      play_node_rid, "play-session-actor", create_message,
      std::chrono::milliseconds (1000));
    assert (created.status == zlink::actor_create_status_t::created);

    zlink::service::actor_t local_actor =
      play_node.create_actor ("dispatch-helper-unused");
    local_actor.close ();
    zlink::actor_ref_t concrete = play_node.actor_lookup ("play-session-actor");

    actor_sample_dispatch_state_t state {&play_spot, NULL, &capture};
    zlink::service::actor_t joined_actor = play_node.create_actor ("local-probe");
    joined_actor.close ();
    state.actor = reinterpret_cast<zlink::service::actor_t *> (NULL);
    play_spot.on_dispatch_event ([] (void *,
                                     const zlink_spot_dispatch_info_t *info,
                                     void *userdata) {
        actor_sample_dispatch_state_t *state =
          static_cast<actor_sample_dispatch_state_t *> (userdata);
        if (info->event == ZLINK_SPOT_DISPATCH_EVENT_ACTOR_JOIN_READABLE) {
            auto request =
              state->spot->recv_actor_join (zlink::recv_flags_t::dontwait);
            assert (request.has_value ());
            zlink::message_t reply = zlink::message_t::from_string ("accepted");
            state->spot->reply_actor_join (request->first, true, reply);
            return;
        }
        if (info->event == ZLINK_SPOT_DISPATCH_EVENT_ACTOR_READABLE) {
            zlink_actor_recv_info_t recv_info;
            zlink_msg_t part;
            zlink_part_flag_t more = ZLINK_PART_FINAL;
            assert (zlink_actor_recv_part (
                      info->subject, &recv_info, &part, &more,
                      ZLINK_RECV_FLAGS_DONTWAIT)
                    == ZLINK_RECV_OK);
            zlink::message_t msg;
            assert (zlink_msg_move (msg.handle (), &part) == 0);
            std::lock_guard<std::mutex> lock (state->capture->mutex);
            state->capture->payload = msg.to_string ();
            state->capture->actor_read = true;
            state->capture->cv.notify_all ();
        }
    }, &state);

    zlink::routing_id_t play_spot_rid = play_spot.routing_id ();
    zlink::message_t join = zlink::message_t::from_string ("join-play");
    assert (gateway_node.join_actor (
      concrete, play_spot_rid, join,
      [&] (zlink::request_result_t result,
           std::vector<zlink::message_t> parts) {
          actor_sample_join_reply (capture, result, std::move (parts));
      },
      zlink::send_flags_t::dontwait, std::chrono::milliseconds (1000)));
    assert (wait_until_flag (capture, &actor_sample_capture_t::joined));
    assert (capture.join_result == zlink::request_result_t::ok);

    zlink::stream_socket_t stream (ctx);
    zlink::routing_id_t session = sample_rid ("gateway-session");
    stream.bind_actor (gateway_node, session, concrete,
                       std::chrono::milliseconds (1000));
    zlink::message_t frame = zlink::message_t::from_string ("client-input");
    assert (stream.send_bound_actor_part (
      gateway_node, session, "play-session-actor", frame,
      zlink::send_flags_t::dontwait));
    assert (wait_until_flag (capture, &actor_sample_capture_t::actor_read));
    assert (capture.payload == "client-input");

    gateway_node.leave_actor (concrete, play_spot_rid,
                              std::chrono::milliseconds (1000));
    gateway_node.destroy_remote_actor (concrete, std::chrono::milliseconds (1000));
    return 0;
}
