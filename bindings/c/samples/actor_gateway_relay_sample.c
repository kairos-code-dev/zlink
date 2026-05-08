/* SPDX-License-Identifier: MPL-2.0 */

#include "actor_sample_common.h"

static zlink_actor_admission_result_t accept_actor (void *node,
                                                   const char *actor_id,
                                                   const zlink_msg_t *parts,
                                                   size_t part_count,
                                                   void *userdata)
{
    (void) node;
    (void) actor_id;
    (void) parts;
    (void) part_count;
    (void) userdata;
    return ZLINK_ACTOR_ADMISSION_ACCEPT;
}

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
    assert (zlink_spot_node_actor_admission_handler (
              play_node, accept_actor, NULL)
            == ZLINK_HANDLER_OK);

    zlink_routing_id_t play_node_rid;
    assert (zlink_get_routing_id (play_node, &play_node_rid)
            == ZLINK_CONFIG_OK);
    zlink_msg_t create_message;
    make_message (&create_message, "spawn");
    zlink_actor_create_result_t create_result;
    assert (zlink_spot_node_create_remote_actor (
              gateway_node, &play_node_rid, "play-session-actor",
              &create_message, 1, &create_result, 1000)
            == ZLINK_REQUEST_OK);
    assert (create_result.status == ZLINK_ACTOR_CREATE_CREATED);

    void *stream = zlink_socket (ctx, ZLINK_SOCKET_STREAM);
    assert (stream != NULL);
    zlink_routing_id_t session;
    actor_sample_set_rid (&session, "gateway-session");
    assert (zlink_stream_bind_actor (gateway_node, stream, &session,
                                     &create_result.actor, 1000)
            == ZLINK_REQUEST_OK);

    zlink_routing_id_t play_spot_rid =
      actor_sample_find_spot_rid_not (play_node, NULL, 0);
    zlink_msg_t join;
    make_message (&join, "join-play");
    assert (zlink_spot_node_actor_join_spot (
              gateway_node, &create_result.actor, &play_node_rid,
              &play_spot_rid, &join, 1, actor_sample_join_reply, &capture,
              ZLINK_DONTWAIT, 1000)
            == ZLINK_SUBMIT_OK);
    assert (callback_signal_wait (&capture.join_signal, 2000));
    assert (capture.join_result == ZLINK_REQUEST_OK);

    zlink_msg_t frame;
    make_message (&frame, "client-input");
    assert (zlink_stream_send_bound_actor_part (
              gateway_node, stream, &session, "play-session-actor", &frame,
              ZLINK_DONTWAIT, ZLINK_PART_FINAL)
            == ZLINK_SUBMIT_OK);
    assert (callback_signal_wait (&capture.actor_signal, 2000));
    assert (strcmp (capture.payload, "client-input") == 0);

    assert (zlink_spot_node_actor_leave_spot (
              gateway_node, &create_result.actor, &play_spot_rid, 1000)
            == ZLINK_REQUEST_OK);
    assert (zlink_spot_node_actor_destroy (
              gateway_node, &create_result.actor, 1000)
            == ZLINK_REQUEST_OK);
    assert (zlink_close (stream) == ZLINK_CLOSE_OK);
    assert (zlink_spot_destroy (&play_spot) == ZLINK_CLOSE_OK);
    assert (zlink_spot_node_destroy (&play_node) == ZLINK_CLOSE_OK);
    assert (zlink_spot_node_destroy (&gateway_node) == ZLINK_CLOSE_OK);
    actor_sample_capture_destroy (&capture);
    assert (zlink_ctx_term (ctx) == ZLINK_CLOSE_OK);
    return 0;
}
