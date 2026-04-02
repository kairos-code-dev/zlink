/* SPDX-License-Identifier: MPL-2.0 */

#include "sample_common.h"

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

    zlink_msg_t outbound;
    make_message (&outbound, k_pair_payload);
    rc = zlink_send (client, &outbound, 1, 0);
    assert (rc == 0);

    zlink_routing_id_t rid;
    zlink_msg_t *parts = NULL;
    size_t count = 0;
    rc = zlink_recv (server, &rid, &parts, &count, 0);
    assert (rc == 0);
    assert (count == 1);
    assert (zlink_msg_size (&parts[0]) == strlen (k_pair_payload));
    assert (memcmp (zlink_msg_data (&parts[0]), k_pair_payload,
                    strlen (k_pair_payload))
            == 0);
    printf ("[pair/recv] send: \"%s\" → recv: \"%.*s\"\n",
            k_pair_payload, (int) zlink_msg_size (&parts[0]),
            (const char *) zlink_msg_data (&parts[0]));
    zlink_multipart_close (parts, count);

    zlink_monitor_close (&client_monitor);
    zlink_monitor_close (&server_monitor);
    zlink_close (client);
    zlink_close (server);
    zlink_ctx_term (ctx);
    return 0;
}
