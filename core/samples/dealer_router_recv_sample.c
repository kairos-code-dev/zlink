/* SPDX-License-Identifier: MPL-2.0 */

#include "sample_common.h"

int main (void)
{
    void *ctx = zlink_ctx_new ();
    assert (ctx != NULL);
    void *router = zlink_socket (ctx, ZLINK_SOCKET_ROUTER);
    void *dealer = zlink_socket (ctx, ZLINK_SOCKET_DEALER);
    assert (router != NULL);
    assert (dealer != NULL);

    void *router_monitor = open_socket_monitor (
      router, ZLINK_SOCKET_MONITOR_EVENT_CONNECTION_READY);
    void *dealer_monitor = open_socket_monitor (
      dealer, ZLINK_SOCKET_MONITOR_EVENT_CONNECTION_READY);

    int rc = zlink_bind (router, "tcp://127.0.0.1:0");
    assert (rc == 0);
    char endpoint[256];
    get_last_endpoint (router, endpoint, sizeof (endpoint));
    rc = zlink_connect (dealer, endpoint);
    assert (rc == 0);
    assert (wait_connected (router_monitor, dealer_monitor, 2000));

    zlink_msg_t outbound;
    make_message (&outbound, k_dealer_router_request);
    rc = zlink_send (dealer, &outbound, 1, 0);
    assert (rc == 0);

    const zlink_routing_id_t *source_node_rid = NULL;
    const zlink_routing_id_t *source_spot_rid = NULL;
    uint64_t request_seq = 0;
    zlink_msg_t *parts = NULL;
    size_t count = 0;
    rc = zlink_router_recv (router, &source_node_rid, &source_spot_rid,
                            &request_seq, &parts, &count, 0);
    assert (rc == 0);
    assert (source_node_rid != NULL);
    assert (source_node_rid->size > 0);
    assert (source_spot_rid != NULL);
    assert (source_spot_rid->size == 0);
    assert (request_seq == 0);
    assert (count == 1);
    assert (zlink_msg_size (&parts[0]) == strlen (k_dealer_router_request));
    assert (memcmp (zlink_msg_data (&parts[0]), k_dealer_router_request,
                    strlen (k_dealer_router_request))
            == 0);
    zlink_multipart_close (parts, count);

    zlink_msg_t reply;
    make_message (&reply, k_dealer_router_reply);
    rc = zlink_send_rid (router, source_node_rid, &reply, 1, 0);
    assert (rc == 0);

    zlink_routing_id_t echo_rid;
    zlink_msg_t *echo_parts = NULL;
    size_t echo_count = 0;
    rc = zlink_recv (dealer, &echo_rid, &echo_parts, &echo_count, 0);
    assert (rc == 0);
    assert (echo_count == 1);
    assert (zlink_msg_size (&echo_parts[0])
            == strlen (k_dealer_router_reply));
    assert (memcmp (zlink_msg_data (&echo_parts[0]), k_dealer_router_reply,
                    strlen (k_dealer_router_reply))
            == 0);
    printf ("[dealer-router/recv] send: \"%s\" → recv: \"%.*s\"\n",
            k_dealer_router_request, (int) zlink_msg_size (&echo_parts[0]),
            (const char *) zlink_msg_data (&echo_parts[0]));
    zlink_multipart_close (echo_parts, echo_count);

    zlink_monitor_close (&dealer_monitor);
    zlink_monitor_close (&router_monitor);
    zlink_close (dealer);
    zlink_close (router);
    zlink_ctx_term (ctx);
    return 0;
}
