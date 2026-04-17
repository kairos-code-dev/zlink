/* SPDX-License-Identifier: MPL-2.0 */

#include "sample_common.h"

int main (void)
{
    void *ctx = zlink_ctx_new ();
    assert (ctx != NULL);
    void *publisher = zlink_socket (ctx, ZLINK_SOCKET_XPUB);
    void *subscriber = zlink_socket (ctx, ZLINK_SOCKET_SUB);
    assert (publisher != NULL);
    assert (subscriber != NULL);

    void *pub_monitor = open_socket_monitor (
      publisher, ZLINK_SOCKET_MONITOR_EVENT_CONNECTION_READY);
    void *sub_monitor = open_socket_monitor (
      subscriber, ZLINK_SOCKET_MONITOR_EVENT_CONNECTION_READY);

    int rc = zlink_bind (publisher, "tcp://127.0.0.1:0");
    assert (rc == 0);
    char endpoint[256];
    get_last_endpoint (publisher, endpoint, sizeof (endpoint));
    rc = zlink_connect (subscriber, endpoint);
    assert (rc == 0);
    assert (wait_connected (pub_monitor, sub_monitor, 2000));

    rc = zlink_set_subscription (subscriber, k_pubsub_topic);
    assert (rc == 0);

    const zlink_routing_id_t *event_rid = NULL;
    int subscribed = 0;
    char event_topic[256];
    size_t event_topic_len = sizeof (event_topic);
    rc = zlink_subscription_event (publisher, &event_rid, &subscribed,
                                   event_topic, &event_topic_len, 0);
    assert (rc == 0);
    assert (subscribed == 1);
    assert (strcmp (event_topic, k_pubsub_topic) == 0);

    zlink_msg_t outbound;
    make_message (&outbound, k_pubsub_payload);
    rc = zlink_publish (publisher, k_pubsub_topic, &outbound, 1, 0);
    assert (rc == 0);

    const zlink_routing_id_t *rid = NULL;
    zlink_msg_t *parts = NULL;
    size_t count = 0;
    char topic[256];
    size_t topic_len = sizeof (topic);
    rc = zlink_subscribe (subscriber, &rid, &parts, &count,
                          topic, &topic_len, 0);
    assert (rc == 0);
    assert (strcmp (topic, k_pubsub_topic) == 0);
    assert (count == 1);
    assert (zlink_msg_size (&parts[0]) == strlen (k_pubsub_payload));
    assert (memcmp (zlink_msg_data (&parts[0]), k_pubsub_payload,
                    strlen (k_pubsub_payload))
            == 0);
    printf ("[pubsub/recv] publish: \"%s/%s\" → subscribe: \"%s/%.*s\"\n",
            k_pubsub_topic, k_pubsub_payload,
            topic, (int) zlink_msg_size (&parts[0]),
            (const char *) zlink_msg_data (&parts[0]));
    zlink_multipart_close (parts, count);

    zlink_monitor_close (&sub_monitor);
    zlink_monitor_close (&pub_monitor);
    zlink_close (subscriber);
    zlink_close (publisher);
    zlink_ctx_term (ctx);
    return 0;
}
