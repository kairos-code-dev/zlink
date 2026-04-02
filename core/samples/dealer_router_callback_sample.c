/* SPDX-License-Identifier: MPL-2.0 */

#include "sample_common.h"

typedef struct
{
    callback_signal_t signal;
    zlink_routing_id_t routing_id;
    char payload[256];
    size_t payload_len;
} router_callback_ctx_t;

typedef struct
{
    callback_signal_t signal;
    char payload[256];
    size_t payload_len;
} dealer_callback_ctx_t;

static void router_callback (const zlink_routing_id_t *source_rid,
                             zlink_msg_t *parts, size_t part_count,
                             void *userdata)
{
    router_callback_ctx_t *cb = (router_callback_ctx_t *) userdata;
    assert (cb != NULL);
    assert (source_rid != NULL);
    assert (part_count == 1);

    cb->routing_id = *source_rid;
    cb->payload_len = zlink_msg_size (&parts[0]);
    assert (cb->payload_len < sizeof (cb->payload));
    memcpy (cb->payload, zlink_msg_data (&parts[0]), cb->payload_len);
    cb->payload[cb->payload_len] = '\0';

    zlink_multipart_close (parts, part_count);
    callback_signal_set (&cb->signal);
}

static void dealer_callback (const zlink_routing_id_t *source_rid,
                             zlink_msg_t *parts, size_t part_count,
                             void *userdata)
{
    (void) source_rid;
    dealer_callback_ctx_t *cb = (dealer_callback_ctx_t *) userdata;
    assert (cb != NULL);
    assert (part_count == 1);

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
    void *router = zlink_socket (ctx, ZLINK_SOCKET_ROUTER);
    void *dealer = zlink_socket (ctx, ZLINK_SOCKET_DEALER);
    assert (router != NULL);
    assert (dealer != NULL);

    void *router_monitor = open_socket_monitor (
      router, ZLINK_SOCKET_MONITOR_EVENT_CONNECTION_READY_CHANGED);
    void *dealer_monitor = open_socket_monitor (
      dealer, ZLINK_SOCKET_MONITOR_EVENT_CONNECTION_READY_CHANGED);

    int rc = zlink_bind (router, "tcp://127.0.0.1:0");
    assert (rc == 0);
    char endpoint[256];
    get_last_endpoint (router, endpoint, sizeof (endpoint));
    rc = zlink_connect (dealer, endpoint);
    assert (rc == 0);
    assert (wait_connected (router_monitor, dealer_monitor, 2000));

    router_callback_ctx_t router_cb;
    callback_signal_init (&router_cb.signal);
    router_cb.payload_len = 0;
    dealer_callback_ctx_t dealer_cb;
    callback_signal_init (&dealer_cb.signal);
    dealer_cb.payload_len = 0;
    rc = zlink_recv_handler (router, &router_callback, &router_cb);
    assert (rc == 0);
    rc = zlink_recv_handler (dealer, &dealer_callback, &dealer_cb);
    assert (rc == 0);

    zlink_msg_t outbound;
    make_message (&outbound, k_dealer_router_request);
    rc = zlink_send (dealer, &outbound, 1, 0);
    assert (rc == 0);

    assert (callback_signal_wait (&router_cb.signal, 2000));
    assert (router_cb.routing_id.size > 0);
    assert (strcmp (router_cb.payload, k_dealer_router_request) == 0);

    zlink_msg_t reply;
    make_message (&reply, k_dealer_router_reply);
    rc = zlink_send_rid (router, &router_cb.routing_id, &reply, 1, 0);
    assert (rc == 0);

    assert (callback_signal_wait (&dealer_cb.signal, 2000));
    assert (strcmp (dealer_cb.payload, k_dealer_router_reply) == 0);
    printf ("[dealer-router/callback] send: \"%s\" → recv: \"%s\"\n",
            k_dealer_router_request, dealer_cb.payload);

    callback_signal_destroy (&dealer_cb.signal);
    callback_signal_destroy (&router_cb.signal);
    zlink_monitor_close (&dealer_monitor);
    zlink_monitor_close (&router_monitor);
    zlink_close (dealer);
    zlink_close (router);
    zlink_ctx_term (ctx);
    return 0;
}
