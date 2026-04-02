/* SPDX-License-Identifier: MPL-2.0 */

#include "sample_common.h"

typedef struct
{
    callback_signal_t signal;
    char payload[256];
    size_t payload_len;
} pair_callback_ctx_t;

static void pair_callback (const zlink_routing_id_t *source_rid,
                           zlink_msg_t *parts, size_t part_count,
                           void *userdata)
{
    (void) source_rid;
    pair_callback_ctx_t *cb = (pair_callback_ctx_t *) userdata;
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
    void *server = zlink_socket (ctx, ZLINK_SOCKET_PAIR);
    void *client = zlink_socket (ctx, ZLINK_SOCKET_PAIR);
    assert (server != NULL);
    assert (client != NULL);

    void *server_monitor = open_socket_monitor (
      server, ZLINK_SOCKET_MONITOR_EVENT_CONNECTION_READY_CHANGED);
    void *client_monitor = open_socket_monitor (
      client, ZLINK_SOCKET_MONITOR_EVENT_CONNECTION_READY_CHANGED);

    int rc = zlink_bind (server, "tcp://127.0.0.1:0");
    assert (rc == 0);
    char endpoint[256];
    get_last_endpoint (server, endpoint, sizeof (endpoint));
    rc = zlink_connect (client, endpoint);
    assert (rc == 0);
    assert (wait_connected (server_monitor, client_monitor, 2000));

    pair_callback_ctx_t cb_ctx;
    callback_signal_init (&cb_ctx.signal);
    cb_ctx.payload_len = 0;
    rc = zlink_recv_handler (server, &pair_callback, &cb_ctx);
    assert (rc == 0);

    zlink_msg_t outbound;
    make_message (&outbound, k_pair_payload);
    rc = zlink_send (client, &outbound, 1, 0);
    assert (rc == 0);

    assert (callback_signal_wait (&cb_ctx.signal, 2000));
    assert (strcmp (cb_ctx.payload, k_pair_payload) == 0);
    printf ("[pair/callback] send: \"%s\" → recv: \"%s\"\n",
            k_pair_payload, cb_ctx.payload);

    callback_signal_destroy (&cb_ctx.signal);
    zlink_monitor_close (&client_monitor);
    zlink_monitor_close (&server_monitor);
    zlink_close (client);
    zlink_close (server);
    zlink_ctx_term (ctx);
    return 0;
}
