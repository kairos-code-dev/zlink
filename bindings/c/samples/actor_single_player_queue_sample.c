/* SPDX-License-Identifier: MPL-2.0 */

#include "actor_sample_common.h"

int main (void)
{
    void *ctx = zlink_ctx_new ();
    assert (ctx != NULL);
    void *node = zlink_spot_node_new (ctx, NULL);
    assert (node != NULL);
    void *first_spot = zlink_spot_new (node);
    assert (first_spot != NULL);
    zlink_routing_id_t first_spot_rid = actor_sample_find_spot_rid_not (node, NULL, 0);
    void *second_spot = zlink_spot_new (node);
    assert (second_spot != NULL);
    zlink_routing_id_t second_spot_rid = actor_sample_find_spot_rid_not (node, &first_spot_rid, 1);
    zlink_actor_ref_t actor;
    assert (zlink_spot_node_actor_new (node, "single-player", &actor) == ZLINK_CONFIG_OK);
    void *stream = zlink_socket (ctx, ZLINK_SOCKET_STREAM);
    assert (stream != NULL);
    zlink_routing_id_t session_rid;
    actor_sample_set_rid (&session_rid, "single-player-session");
    zlink_routing_id_t node_rid;
    assert (zlink_get_routing_id (node, &node_rid) == ZLINK_CONFIG_OK);

    actor_sample_capture_t first_capture;
    actor_sample_capture_init (&first_capture);
    first_capture.node = node;
    assert (zlink_spot_dispatch_event_handler (first_spot, actor_sample_join_only_dispatch,
                                               &first_capture)
            == ZLINK_HANDLER_OK);
    assert (zlink_stream_bind_actor (stream, &session_rid, &actor, actor_sample_reply,
                                     &first_capture, 1000)
            == ZLINK_SUBMIT_OK);
    assert (callback_signal_wait (&first_capture.join_signal, 2000));
    assert (first_capture.join_result == ZLINK_REQUEST_OK);
    actor_sample_capture_reset (&first_capture);
    zlink_msg_t join_first;
    make_message (&join_first, "join-first");
    assert (zlink_spot_node_actor_join_spot (node, &actor, &node_rid, &first_spot_rid, &join_first,
                                             1, actor_sample_join_reply, &first_capture,
                                             ZLINK_DONTWAIT, 1000)
            == ZLINK_SUBMIT_OK);
    assert (callback_signal_wait (&first_capture.join_signal, 2000));
    assert (first_capture.join_result == ZLINK_REQUEST_OK);

    zlink_msg_t before;
    make_message (&before, "before-");
    assert (zlink_stream_send_bound_actor_part (stream, &session_rid, "single-player", &before,
                                                ZLINK_DONTWAIT, ZLINK_PART_FINAL)
            == ZLINK_SUBMIT_OK);

    actor_sample_capture_reset (&first_capture);
    assert (zlink_spot_node_actor_leave_spot (node, &actor, &first_spot_rid, actor_sample_reply,
                                              &first_capture, 0)
            == ZLINK_SUBMIT_OK);
    assert (callback_signal_wait (&first_capture.join_signal, 2000));
    assert (first_capture.join_result == ZLINK_REQUEST_OK);

    zlink_msg_t between;
    make_message (&between, "between-");
    assert (zlink_stream_send_bound_actor_part (stream, &session_rid, "single-player", &between,
                                                ZLINK_DONTWAIT, ZLINK_PART_FINAL)
            == ZLINK_SUBMIT_OK);

    actor_sample_capture_t second_capture;
    actor_sample_capture_init (&second_capture);
    second_capture.node = node;
    assert (zlink_spot_dispatch_event_handler (second_spot, actor_sample_dispatch, &second_capture)
            == ZLINK_HANDLER_OK);
    zlink_msg_t join_second;
    make_message (&join_second, "join-second");
    assert (zlink_spot_node_actor_join_spot (node, &actor, &node_rid, &second_spot_rid,
                                             &join_second, 1, actor_sample_join_reply,
                                             &second_capture, ZLINK_DONTWAIT, 1000)
            == ZLINK_SUBMIT_OK);
    assert (callback_signal_wait (&second_capture.join_signal, 2000));
    assert (second_capture.join_result == ZLINK_REQUEST_OK);
    assert (callback_signal_wait (&second_capture.actor_signal, 2000));
    assert (strcmp (second_capture.payload, "before-between-") == 0);

    actor_sample_capture_reset (&second_capture);
    assert (zlink_spot_node_actor_leave_spot (node, &actor, &second_spot_rid, actor_sample_reply,
                                              &second_capture, 0)
            == ZLINK_SUBMIT_OK);
    assert (callback_signal_wait (&second_capture.join_signal, 2000));
    assert (second_capture.join_result == ZLINK_REQUEST_OK);
    actor_sample_capture_reset (&second_capture);
    assert (zlink_spot_node_actor_destroy (node, &actor, actor_sample_reply, &second_capture, 0)
            == ZLINK_SUBMIT_OK);
    assert (callback_signal_wait (&second_capture.join_signal, 2000));
    assert (second_capture.join_result == ZLINK_REQUEST_OK);
    printf ("[actor/single-player] queued payload: \"before-between-\" -> actor: \"%s\"\n",
            second_capture.payload);
    assert (zlink_close (stream) == ZLINK_CLOSE_OK);
    assert (zlink_spot_destroy (&second_spot) == ZLINK_CLOSE_OK);
    assert (zlink_spot_destroy (&first_spot) == ZLINK_CLOSE_OK);
    assert (zlink_spot_node_destroy (&node) == ZLINK_CLOSE_OK);
    actor_sample_capture_destroy (&second_capture);
    actor_sample_capture_destroy (&first_capture);
    assert (zlink_ctx_term (ctx) == ZLINK_CLOSE_OK);
    return 0;
}
