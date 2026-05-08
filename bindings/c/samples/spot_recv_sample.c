/* SPDX-License-Identifier: MPL-2.0 */

#include "sample_common.h"

int main (void)
{
    void *ctx = zlink_ctx_new ();
    assert (ctx != NULL);

    void *node = zlink_spot_node_new (ctx, NULL);
    assert (node != NULL);

    void *spot = zlink_spot_new (node);
    assert (spot != NULL);

    int rc = zlink_set_subscription (spot, k_spot_topic);
    assert (rc == 0);
    assert (wait_for_spot_node_subject_ready (node, 10000));

    zlink_msg_t outbound;
    make_message (&outbound, k_spot_payload);
    rc = zlink_publish (spot, k_spot_topic, &outbound, 1, 0);
    assert (rc == ZLINK_SUBMIT_OK);

    zlink_routing_id_t rid;
    zlink_msg_t *parts = NULL;
    size_t count = 0;
    char topic[256];
    size_t topic_len = 0;
    int attempts = 500;
    do {
        topic_len = sizeof (topic);
        rc = zlink_subscribe (spot, &rid, &parts, &count,
                              topic, &topic_len, ZLINK_DONTWAIT);
        if (rc == ZLINK_RECV_OK)
            break;
        assert (rc == ZLINK_RECV_NO_DATA);
        sample_pause_ms (10);
    } while (--attempts > 0);
    assert (rc == 0);
    assert (strcmp (topic, k_spot_topic) == 0);
    assert (count == 1);
    assert (zlink_msg_size (&parts[0]) == strlen (k_spot_payload));
    assert (memcmp (zlink_msg_data (&parts[0]), k_spot_payload,
                    strlen (k_spot_payload))
            == 0);
    printf ("[spot/recv] service: \"sample\" tick: 1 publish: \"%s/%s\" -> recv: \"%s/%.*s\"\n",
            k_spot_topic, k_spot_payload,
            topic, (int) zlink_msg_size (&parts[0]),
            (const char *) zlink_msg_data (&parts[0]));
    zlink_multipart_close (parts, count);

    zlink_spot_destroy (&spot);
    zlink_spot_node_destroy (&node);
    zlink_ctx_term (ctx);
    return 0;
}
