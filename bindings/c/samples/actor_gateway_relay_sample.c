/* SPDX-License-Identifier: MPL-2.0 */

#include "actor_sample_common.h"

int main (void)
{
    void *ctx = zlink_ctx_new ();
    assert (ctx != NULL);
    void *gateway_node = zlink_spot_node_new (ctx, NULL);
    void *play_node = zlink_spot_node_new (ctx, NULL);
    assert (gateway_node != NULL);
    assert (play_node != NULL);
    void *play_spot = zlink_spot_new (play_node);
    assert (play_spot != NULL);

    actor_sample_capture_t capture;
    actor_sample_capture_init (&capture);
    capture.node = play_node;
    assert (zlink_spot_dispatch_event_handler (play_spot,
                                               actor_sample_dispatch,
                                               &capture)
            == ZLINK_HANDLER_OK);

    zlink_actor_ref_t actor;
    assert (zlink_spot_node_actor_new (
              gateway_node, "play-session-actor", &actor)
            == ZLINK_CONFIG_OK);
    void *stream = zlink_socket (ctx, ZLINK_SOCKET_STREAM);
    assert (stream != NULL);
    zlink_routing_id_t session_rid;
    actor_sample_set_rid (&session_rid, "gateway-session");
    zlink_routing_id_t play_node_rid;
    assert (zlink_get_routing_id (play_node, &play_node_rid)
            == ZLINK_CONFIG_OK);
    assert (zlink_stream_attach_actor_gateway (stream, gateway_node)
            == ZLINK_CONFIG_OK);
    assert (zlink_stream_bind_actor (stream, &session_rid, &actor,
                                     actor_sample_reply, &capture, 1000)
            == ZLINK_SUBMIT_OK);
    assert (callback_signal_wait (&capture.join_signal, 2000));
    assert (capture.join_result == ZLINK_REQUEST_OK);
    actor_sample_capture_reset (&capture);

    zlink_routing_id_t play_spot_rid =
      actor_sample_find_spot_rid_not (play_node, NULL, 0);
    zlink_msg_t join;
    make_message (&join, "join-play");
    assert (zlink_spot_node_actor_join_spot (
              gateway_node, &actor, &play_node_rid, &play_spot_rid, &join, 1,
              actor_sample_join_reply, &capture, ZLINK_DONTWAIT, 1000)
            == ZLINK_SUBMIT_OK);
    assert (callback_signal_wait (&capture.join_signal, 2000));
    assert (capture.join_result == ZLINK_REQUEST_OK);
    actor = capture.joined_actor;

    actor_sample_capture_reset_actor (&capture);
    zlink_msg_t frame;
    make_message (&frame, "client-input");
    assert (zlink_stream_send_bound_actor_part (
              stream, &session_rid, "play-session-actor", &frame,
              ZLINK_DONTWAIT, ZLINK_PART_FINAL)
            == ZLINK_SUBMIT_OK);
    assert (callback_signal_wait (&capture.actor_signal, 2000));
    assert (strcmp (capture.payload, "client-input") == 0);

    actor_sample_capture_reset (&capture);
    assert (zlink_spot_node_actor_leave_spot (
              gateway_node, &actor, &play_spot_rid, actor_sample_reply,
              &capture, 1000)
            == ZLINK_SUBMIT_OK);
    assert (callback_signal_wait (&capture.join_signal, 2000));
    assert (capture.join_result == ZLINK_REQUEST_OK);
    actor_sample_capture_reset (&capture);
    assert (zlink_spot_node_actor_destroy (
              gateway_node, &actor, actor_sample_reply, &capture, 1000)
            == ZLINK_SUBMIT_OK);
    assert (callback_signal_wait (&capture.join_signal, 2000));
    assert (capture.join_result == ZLINK_REQUEST_OK);
    printf ("[actor/gateway] stream payload: \"client-input\" -> actor: \"%s\"\n",
            capture.payload);
    assert (zlink_close (stream) == ZLINK_CLOSE_OK);
    assert (zlink_spot_destroy (&play_spot) == ZLINK_CLOSE_OK);
    assert (zlink_spot_node_destroy (&play_node) == ZLINK_CLOSE_OK);
    assert (zlink_spot_node_destroy (&gateway_node) == ZLINK_CLOSE_OK);
    actor_sample_capture_destroy (&capture);
    assert (zlink_ctx_term (ctx) == ZLINK_CLOSE_OK);
    return 0;
}
