/* SPDX-License-Identifier: MPL-2.0 */

#include "sample_common.h"

typedef struct
{
    callback_signal_t signal;
    char topic[256];
    size_t topic_len;
    char payload[256];
    size_t payload_len;
} spot_callback_ctx_t;

static void subscribe_callback (const zlink_routing_id_t *source_rid,
                                const char *topic, size_t topic_len,
                                zlink_msg_t *parts, size_t part_count,
                                void *userdata)
{
    (void) source_rid;
    spot_callback_ctx_t *cb = (spot_callback_ctx_t *) userdata;
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

    void *pub_node = zlink_spot_node_new (ctx);
    void *sub_node = zlink_spot_node_new (ctx);
    assert (pub_node != NULL);
    assert (sub_node != NULL);

    void *pub_spot = zlink_spot_new (pub_node);
    void *sub_spot = zlink_spot_new (sub_node);
    assert (pub_spot != NULL);
    assert (sub_spot != NULL);

    void *sub_monitor = open_service_monitor (
      sub_spot,
      ZLINK_SERVICE_MONITOR_EVENT_PEER_ADMISSION_CHANGED
        | ZLINK_SERVICE_MONITOR_EVENT_ERROR);
    void *pub_monitor = open_service_monitor (
      pub_spot,
      ZLINK_SERVICE_MONITOR_EVENT_PEER_ADMISSION_CHANGED
        | ZLINK_SERVICE_MONITOR_EVENT_ERROR);

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
    assert (wait_spot_ready (sub_monitor, pub_monitor,
                             status.local_endpoint, 10000));

    zlink_msg_t outbound;
    make_message (&outbound, k_spot_payload);
    rc = zlink_publish (pub_spot, k_spot_topic, &outbound, 1, 0);
    assert (rc == 0);

    zlink_routing_id_t source_rid;
    zlink_msg_t *parts = NULL;
    size_t part_count = 0;
    char service_name[256];
    size_t service_name_len = sizeof (service_name);
    char topic[256];
    size_t topic_len = sizeof (topic);
    rc = zlink_spot_subscribe (sub_spot, &source_rid, &parts, &part_count,
                               service_name, &service_name_len, topic,
                               &topic_len, 0);
    assert (rc == 0);
    assert (part_count == 1);
    assert (topic_len == strlen (k_spot_topic));
    assert (memcmp (topic, k_spot_topic, topic_len) == 0);
    assert (zlink_msg_size (&parts[0]) == strlen (k_spot_payload));
    assert (memcmp (zlink_msg_data (&parts[0]), k_spot_payload,
                    strlen (k_spot_payload)) == 0);
    zlink_multipart_close (parts, part_count);
    printf (
      "[spot/callback] publish: \"%s/%s\" -> subscribe via recv\n",
      k_spot_topic, k_spot_payload);
    zlink_monitor_close (&pub_monitor);
    zlink_monitor_close (&sub_monitor);
    zlink_spot_destroy (&sub_spot);
    zlink_spot_destroy (&pub_spot);
    zlink_spot_node_destroy (&sub_node);
    zlink_spot_node_destroy (&pub_node);
    zlink_ctx_term (ctx);
    return 0;
}
