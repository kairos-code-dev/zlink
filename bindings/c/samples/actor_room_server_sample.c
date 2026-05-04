/* SPDX-License-Identifier: MPL-2.0 */

#include "actor_sample_common.h"

int main (void)
{
    void *ctx = zlink_ctx_new ();
    assert (ctx != NULL);
    void *node = zlink_spot_node_new (ctx, NULL);
    assert (node != NULL);
    void *spot = zlink_spot_new (node);
    assert (spot != NULL);
    void *actor = zlink_spot_node_actor_new (node, "room-player-1");
    assert (actor != NULL);

    actor_sample_capture_t capture;
    actor_sample_capture_init (&capture);
    assert (zlink_spot_dispatch_event_handler (spot, actor_sample_dispatch,
                                               &capture)
            == ZLINK_HANDLER_OK);

    zlink_msg_t join;
    make_message (&join, "enter-room");
    assert (zlink_actor_join_spot (actor, spot, &join,
                                   actor_sample_join_reply, &capture,
                                   ZLINK_DONTWAIT, 1000)
            == ZLINK_SUBMIT_OK);
    assert (callback_signal_wait (&capture.join_signal, 2000));
    assert (capture.join_result == ZLINK_REQUEST_OK);

    void *stream = zlink_socket (ctx, ZLINK_SOCKET_STREAM);
    assert (stream != NULL);
    zlink_routing_id_t session;
    actor_sample_set_rid (&session, "room-session");
    zlink_actor_ref_t ref;
    assert (zlink_actor_get_ref (actor, &ref) == ZLINK_CONFIG_OK);
    assert (zlink_stream_bind_actor (node, stream, &session, &ref, 1000)
            == ZLINK_REQUEST_OK);

    zlink_msg_t event;
    make_message (&event, "move:north");
    assert (zlink_stream_send_bound_actor_part (
              node, stream, &session, "room-player-1", &event,
              ZLINK_DONTWAIT, ZLINK_PART_FINAL)
            == ZLINK_SUBMIT_OK);
    assert (callback_signal_wait (&capture.actor_signal, 2000));
    assert (strcmp (capture.payload, "move:north") == 0);

    assert (zlink_actor_leave_spot (actor, spot) == ZLINK_CONFIG_OK);
    assert (zlink_actor_destroy (&actor, 0) == ZLINK_REQUEST_OK);
    assert (zlink_close (stream) == ZLINK_CLOSE_OK);
    assert (zlink_spot_destroy (&spot) == ZLINK_CLOSE_OK);
    assert (zlink_spot_node_destroy (&node) == ZLINK_CLOSE_OK);
    actor_sample_capture_destroy (&capture);
    assert (zlink_ctx_term (ctx) == ZLINK_CLOSE_OK);
    return 0;
}
