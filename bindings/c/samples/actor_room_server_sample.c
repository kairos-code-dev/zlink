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
    zlink_actor_ref_t actor;
    assert (zlink_spot_node_actor_new (node, "room-player-1", &actor)
            == ZLINK_CONFIG_OK);
    zlink_routing_id_t node_rid;
    assert (zlink_get_routing_id (node, &node_rid) == ZLINK_CONFIG_OK);
    zlink_routing_id_t spot_rid;
    assert (zlink_get_routing_id (spot, &spot_rid) == ZLINK_CONFIG_OK);

    actor_sample_capture_t capture;
    actor_sample_capture_init (&capture);
    capture.node = node;
    assert (zlink_spot_dispatch_event_handler (spot, actor_sample_dispatch,
                                               &capture)
            == ZLINK_HANDLER_OK);

    zlink_msg_t join;
    make_message (&join, "enter-room");
    assert (zlink_spot_node_actor_join_spot (
              node, &actor, &node_rid, &spot_rid, &join, 1,
              actor_sample_join_reply, &capture, ZLINK_DONTWAIT, 1000)
            == ZLINK_SUBMIT_OK);
    assert (callback_signal_wait (&capture.join_signal, 2000));
    assert (capture.join_result == ZLINK_REQUEST_OK);

    actor_sample_capture_reset (&capture);
    assert (zlink_spot_node_actor_leave_spot (
              node, &actor, &spot_rid, actor_sample_reply, &capture, 0)
            == ZLINK_SUBMIT_OK);
    assert (callback_signal_wait (&capture.join_signal, 2000));
    assert (capture.join_result == ZLINK_REQUEST_OK);
    actor_sample_capture_reset (&capture);
    assert (zlink_spot_node_actor_destroy (
              node, &actor, actor_sample_reply, &capture, 0)
            == ZLINK_SUBMIT_OK);
    assert (callback_signal_wait (&capture.join_signal, 2000));
    assert (capture.join_result == ZLINK_REQUEST_OK);
    assert (zlink_spot_destroy (&spot) == ZLINK_CLOSE_OK);
    assert (zlink_spot_node_destroy (&node) == ZLINK_CLOSE_OK);
    actor_sample_capture_destroy (&capture);
    assert (zlink_ctx_term (ctx) == ZLINK_CLOSE_OK);
    return 0;
}
