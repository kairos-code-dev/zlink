/* SPDX-License-Identifier: MPL-2.0 */

#include "sample_common.h"

int main (void)
{
    void *ctx = zlink_ctx_new ();
    assert (ctx != NULL);

    void *pub_node = zlink_spot_node_new (ctx);
    void *sub_node = zlink_spot_node_new (ctx);
    assert (pub_node != NULL);
    assert (sub_node != NULL);

    void *pub_spot = zlink_spot_new (pub_node);
    void *sub_spot = zlink_spot_new (sub_node);
    assert (pub_spot != NULL);
    assert (sub_spot != NULL);

    int rc = zlink_spot_node_bind (pub_node, "tcp://127.0.0.1:0");
    assert (rc == 0);
    zlink_spot_node_status_t status;
    rc = zlink_spot_node_status_snapshot (pub_node, &status);
    assert (rc == 0);
    assert (strlen (status.local_endpoint) > 0);
    rc = zlink_spot_node_connect_peer (sub_node, status.local_endpoint);
    assert (rc == 0);

    rc = zlink_set_subscription (sub_spot, k_spot_topic);
    assert (rc == 0);
    assert (wait_for_spot_node_subject_ready (sub_node, 10000));

    zlink_msg_t outbound;
    make_message (&outbound, k_spot_payload);
    rc = zlink_publish (pub_spot, k_spot_topic, &outbound, 1, 0);
    assert (rc == 0);

    zlink_routing_id_t rid;
    zlink_msg_t *parts = NULL;
    size_t count = 0;
    char topic[256];
    size_t topic_len = sizeof (topic);
    rc = zlink_subscribe (sub_spot, &rid, &parts, &count,
                          topic, &topic_len, 0);
    assert (rc == 0);
    assert (strcmp (topic, k_spot_topic) == 0);
    assert (count == 1);
    assert (zlink_msg_size (&parts[0]) == strlen (k_spot_payload));
    assert (memcmp (zlink_msg_data (&parts[0]), k_spot_payload,
                    strlen (k_spot_payload))
            == 0);
    printf ("[spot/recv] publish: \"%s/%s\" → subscribe: \"%s/%.*s\"\n",
            k_spot_topic, k_spot_payload,
            topic, (int) zlink_msg_size (&parts[0]),
            (const char *) zlink_msg_data (&parts[0]));
    zlink_multipart_close (parts, count);

    zlink_spot_destroy (&sub_spot);
    zlink_spot_destroy (&pub_spot);
    zlink_spot_node_destroy (&sub_node);
    zlink_spot_node_destroy (&pub_node);
    zlink_ctx_term (ctx);
    return 0;
}
