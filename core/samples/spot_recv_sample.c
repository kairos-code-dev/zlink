/* SPDX-License-Identifier: MPL-2.0 */

#include "sample_common.h"

typedef struct spot_recv_state_t
{
    volatile int done;
    volatile int failed;
    char service_name[256];
    char topic[256];
    char payload[256];
} spot_recv_state_t;

static int wait_attach_pubsub (void *node_,
                               const char *service_name_,
                               void *publisher_,
                               void *subscriber_)
{
    struct timespec sleep_for;
    sleep_for.tv_sec = 0;
    sleep_for.tv_nsec = 1000000;
    struct timespec start;
    clock_gettime (CLOCK_MONOTONIC, &start);

    for (;;) {
        const int rc = zlink_spot_node_attach_pubsub (
          node_, service_name_, publisher_, subscriber_);
        if (rc == 0)
            return 1;

        struct timespec now;
        clock_gettime (CLOCK_MONOTONIC, &now);
        const long elapsed_ms =
          (long) (now.tv_sec - start.tv_sec) * 1000
          + (long) (now.tv_nsec - start.tv_nsec) / 1000000;
        if (elapsed_ms >= 5000)
            return 0;
        nanosleep (&sleep_for, NULL);
    }
}

static void on_spot_dispatch_event (void *spot_,
                                    zlink_spot_dispatch_event_t event_,
                                    void *userdata_)
{
    spot_recv_state_t *state = (spot_recv_state_t *) userdata_;
    if (event_ != ZLINK_SPOT_DISPATCH_EVENT_SUBSCRIBE_READABLE)
        return;

    zlink_routing_id_t source_rid;
    zlink_msg_t *parts = NULL;
    size_t part_count = 0;
    size_t service_name_len = sizeof (state->service_name);
    size_t topic_len = sizeof (state->topic);
    const int rc = zlink_spot_subscribe (
      spot_, &source_rid, &parts, &part_count, state->service_name,
      &service_name_len, state->topic, &topic_len, ZLINK_DONTWAIT);
    if (rc != 0) {
        state->failed = 1;
        state->done = 1;
        return;
    }

    if (part_count != 1 || zlink_msg_size (&parts[0]) >= sizeof (state->payload)) {
        state->failed = 1;
        state->done = 1;
        zlink_multipart_close (parts, part_count);
        return;
    }

    memcpy (state->payload, zlink_msg_data (&parts[0]), zlink_msg_size (&parts[0]));
    state->payload[zlink_msg_size (&parts[0])] = '\0';
    zlink_multipart_close (parts, part_count);
    state->done = 1;
}

int main (void)
{
    void *ctx = zlink_ctx_new ();
    assert (ctx != NULL);

    void *node = zlink_spot_node_new (ctx);
    void *publisher = zlink_socket (ctx, ZLINK_SOCKET_XPUB);
    void *subscriber = zlink_socket (ctx, ZLINK_SOCKET_SUB);
    void *spot = zlink_spot_new (node);
    assert (node != NULL);
    assert (publisher != NULL);
    assert (subscriber != NULL);
    assert (spot != NULL);

    char endpoint[256];
    reserve_tcp_endpoint (endpoint, sizeof (endpoint));
    int rc = zlink_bind (publisher, endpoint);
    assert (rc == 0);
    rc = zlink_connect (subscriber, endpoint);
    assert (rc == 0);
    assert (wait_attach_pubsub (node, k_service_name, publisher, subscriber));
    rc = zlink_set_subscription (spot, k_spot_topic);
    assert (rc == 0);

    spot_recv_state_t state;
    memset (&state, 0, sizeof (state));
    rc = zlink_spot_dispatch_event_handler (
      spot, &on_spot_dispatch_event, &state);
    assert (rc == 0);

    zlink_msg_t outbound;
    make_message (&outbound, k_spot_payload);
    rc = zlink_spot_publish (spot, k_service_name, k_spot_topic, &outbound, 1, 0);
    assert (rc == 0);

    struct timespec sleep_for;
    sleep_for.tv_sec = 0;
    sleep_for.tv_nsec = 1000000;
    struct timespec start;
    clock_gettime (CLOCK_MONOTONIC, &start);
    while (!state.done) {
        struct timespec now;
        clock_gettime (CLOCK_MONOTONIC, &now);
        const long elapsed_ms =
          (long) (now.tv_sec - start.tv_sec) * 1000
          + (long) (now.tv_nsec - start.tv_nsec) / 1000000;
        assert (elapsed_ms < 5000);
        nanosleep (&sleep_for, NULL);
    }

    assert (!state.failed);
    assert (strcmp (state.service_name, k_service_name) == 0);
    assert (strcmp (state.topic, k_spot_topic) == 0);
    assert (strcmp (state.payload, k_spot_payload) == 0);
    zlink_routing_id_t source_rid;
    memset (&source_rid, 0, sizeof (source_rid));
    assert (zlink_get_routing_id (spot, &source_rid) == 0);
    char routing_text[64];
    routing_id_to_display_text (&source_rid, routing_text, sizeof (routing_text));
    printf ("[spot/recv] spot: \"%s\" service: \"sample\" tick: 1 publish: \"%s/%s\" -> recv: \"%s/%s\"\n",
            routing_text, k_spot_topic, k_spot_payload, state.topic,
            state.payload);

    zlink_spot_destroy (&spot);
    zlink_close (subscriber);
    zlink_close (publisher);
    zlink_spot_node_destroy (&node);
    zlink_ctx_term (ctx);
    return 0;
}
