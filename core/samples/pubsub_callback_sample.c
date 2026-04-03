/* SPDX-License-Identifier: MPL-2.0 */

#include "sample_common.h"

typedef struct
{
    callback_signal_t signal;
    char topic[256];
    size_t topic_len;
    char payload[256];
    size_t payload_len;
} pubsub_callback_ctx_t;

static void subscribe_callback (const zlink_routing_id_t *source_rid,
                                const char *topic, size_t topic_len,
                                zlink_msg_t *parts, size_t part_count,
                                void *userdata)
{
    (void) source_rid;
    pubsub_callback_ctx_t *cb = (pubsub_callback_ctx_t *) userdata;
    assert (cb != NULL);
    assert (topic != NULL);
    assert (part_count == 1);

    cb->topic_len = topic_len;
    assert (topic_len < sizeof (cb->topic));
    memcpy (cb->topic, topic, topic_len);
    cb->topic[topic_len] = '\0';

    cb->payload_len = zlink_msg_size (&parts[0]);
    assert (cb->payload_len < sizeof (cb->payload));
    memcpy (cb->payload, zlink_msg_data (&parts[0]), cb->payload_len);
    cb->payload[cb->payload_len] = '\0';

    zlink_multipart_close (parts, part_count);
    callback_signal_set (&cb->signal);
}

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

    pubsub_callback_ctx_t cb_ctx;
    callback_signal_init (&cb_ctx.signal);
    cb_ctx.topic_len = 0;
    cb_ctx.payload_len = 0;
    rc = zlink_subscribe_handler (subscriber, &subscribe_callback, &cb_ctx);
    assert (rc == 0);

    zlink_routing_id_t event_rid;
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

    assert (callback_signal_wait (&cb_ctx.signal, 2000));
    assert (strcmp (cb_ctx.topic, k_pubsub_topic) == 0);
    assert (strcmp (cb_ctx.payload, k_pubsub_payload) == 0);
    printf (
      "[pubsub/callback] publish: \"%s/%s\" → subscribe: \"%s/%s\"\n",
      k_pubsub_topic, k_pubsub_payload, cb_ctx.topic, cb_ctx.payload);

    callback_signal_destroy (&cb_ctx.signal);
    zlink_monitor_close (&sub_monitor);
    zlink_monitor_close (&pub_monitor);
    zlink_close (subscriber);
    zlink_close (publisher);
    zlink_ctx_term (ctx);
    return 0;
}
