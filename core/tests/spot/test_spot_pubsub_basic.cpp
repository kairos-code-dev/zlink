/* SPDX-License-Identifier: MPL-2.0 */

#include "../testutil_unity.hpp"
#include "../testutil.hpp"

#include <string.h>

void setUp ()
{
}

void tearDown ()
{
}

static void test_spot_local_pubsub ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *node = zlink_spot_node_new (ctx);
    TEST_ASSERT_NOT_NULL (node);

    void *spot = zlink_spot_new (node);
    TEST_ASSERT_NOT_NULL (spot);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_subscribe (spot, "chat:room1:msg"));

    zlink_msg_t parts[1];
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_init_size (&parts[0], 5));
    memcpy (zlink_msg_data (&parts[0]), "hello", 5);

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_publish (spot, "chat:room1:msg", parts, 1, 0));

    zlink_msg_t *recv_parts = NULL;
    size_t recv_count = 0;
    char topic[256];
    size_t topic_len = 0;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_recv (spot, &recv_parts, &recv_count, 0, topic, &topic_len));
    TEST_ASSERT_EQUAL_INT (1, (int) recv_count);
    TEST_ASSERT_EQUAL_STRING ("chat:room1:msg", topic);
    TEST_ASSERT_EQUAL_INT (5, (int) zlink_msg_size (&recv_parts[0]));
    TEST_ASSERT_EQUAL_MEMORY ("hello", zlink_msg_data (&recv_parts[0]), 5);
    zlink_msgv_close (recv_parts, recv_count);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&spot));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

static void test_spot_pattern_subscribe ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *node = zlink_spot_node_new (ctx);
    TEST_ASSERT_NOT_NULL (node);

    void *spot = zlink_spot_new (node);
    TEST_ASSERT_NOT_NULL (spot);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_subscribe_pattern (spot, "zone:12:*"));

    zlink_msg_t parts[1];
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_init_size (&parts[0], 4));
    memcpy (zlink_msg_data (&parts[0]), "ping", 4);

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_publish (spot, "zone:12:state", parts, 1, 0));

    zlink_msg_t *recv_parts = NULL;
    size_t recv_count = 0;
    char topic[256];
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_recv (spot, &recv_parts, &recv_count, 0, topic, NULL));
    TEST_ASSERT_EQUAL_STRING ("zone:12:state", topic);
    zlink_msgv_close (recv_parts, recv_count);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&spot));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

static void test_spot_publish_no_subscribers ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *node = zlink_spot_node_new (ctx);
    TEST_ASSERT_NOT_NULL (node);

    void *spot = zlink_spot_new (node);
    TEST_ASSERT_NOT_NULL (spot);

    zlink_msg_t parts[1];
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_init_size (&parts[0], 3));
    memcpy (zlink_msg_data (&parts[0]), "nop", 3);

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_publish (spot, "metrics:cpu", parts, 1, 0));

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&spot));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

static void test_spot_multipart_publish ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *node = zlink_spot_node_new (ctx);
    TEST_ASSERT_NOT_NULL (node);

    void *spot = zlink_spot_new (node);
    TEST_ASSERT_NOT_NULL (spot);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_subscribe (spot, "mp:topic"));

    zlink_msg_t parts[2];
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_init_size (&parts[0], 3));
    memcpy (zlink_msg_data (&parts[0]), "one", 3);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_init_size (&parts[1], 3));
    memcpy (zlink_msg_data (&parts[1]), "two", 3);

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_publish (spot, "mp:topic", parts, 2, 0));

    zlink_msg_t *recv_parts = NULL;
    size_t recv_count = 0;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_recv (spot, &recv_parts, &recv_count, 0, NULL, NULL));
    TEST_ASSERT_EQUAL_INT (2, (int) recv_count);
    TEST_ASSERT_EQUAL_MEMORY ("one", zlink_msg_data (&recv_parts[0]), 3);
    TEST_ASSERT_EQUAL_MEMORY ("two", zlink_msg_data (&recv_parts[1]), 3);
    zlink_msgv_close (recv_parts, recv_count);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&spot));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

static void test_spot_peer_pubsub ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *node_a = zlink_spot_node_new (ctx);
    TEST_ASSERT_NOT_NULL (node_a);
    void *node_b = zlink_spot_node_new (ctx);
    TEST_ASSERT_NOT_NULL (node_b);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_bind (node_a, "inproc://spot-a"));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_bind (node_b, "inproc://spot-b"));

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_connect_peer_pub (node_b, "inproc://spot-a"));

    void *spot_b = zlink_spot_new (node_b);
    TEST_ASSERT_NOT_NULL (spot_b);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_subscribe (spot_b, "peer:topic"));

    msleep (50);

    void *spot_a = zlink_spot_new (node_a);
    TEST_ASSERT_NOT_NULL (spot_a);

    zlink_msg_t parts[1];
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_init_size (&parts[0], 4));
    memcpy (zlink_msg_data (&parts[0]), "pong", 4);

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_publish (spot_a, "peer:topic", parts, 1, 0));

    zlink_msg_t *recv_parts = NULL;
    size_t recv_count = 0;
    char topic[256];
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_recv (spot_b, &recv_parts, &recv_count, 0, topic, NULL));
    TEST_ASSERT_EQUAL_STRING ("peer:topic", topic);
    TEST_ASSERT_EQUAL_INT (1, (int) recv_count);
    TEST_ASSERT_EQUAL_MEMORY ("pong", zlink_msg_data (&recv_parts[0]), 4);
    zlink_msgv_close (recv_parts, recv_count);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&spot_a));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&spot_b));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node_a));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node_b));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

static void test_spot_peer_ipc ()
{
#if !defined(ZLINK_HAVE_IPC)
    TEST_IGNORE_MESSAGE ("IPC not compiled");
    return;
#else
    if (!zlink_has ("ipc")) {
        TEST_IGNORE_MESSAGE ("IPC not available");
        return;
    }

    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *node_a = zlink_spot_node_new (ctx);
    TEST_ASSERT_NOT_NULL (node_a);
    void *node_b = zlink_spot_node_new (ctx);
    TEST_ASSERT_NOT_NULL (node_b);

    char endpoint_a[MAX_SOCKET_STRING];
    char endpoint_b[MAX_SOCKET_STRING];
    make_random_ipc_endpoint (endpoint_a);
    make_random_ipc_endpoint (endpoint_b);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_bind (node_a, endpoint_a));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_bind (node_b, endpoint_b));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_connect_peer_pub (node_b, endpoint_a));

    void *spot_b = zlink_spot_new (node_b);
    TEST_ASSERT_NOT_NULL (spot_b);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_subscribe (spot_b, "ipc:test"));

    msleep (50);

    void *spot_a = zlink_spot_new (node_a);
    TEST_ASSERT_NOT_NULL (spot_a);

    zlink_msg_t parts[1];
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_init_size (&parts[0], 7));
    memcpy (zlink_msg_data (&parts[0]), "ipc-msg", 7);

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_publish (spot_a, "ipc:test", parts, 1, 0));

    zlink_msg_t *recv_parts = NULL;
    size_t recv_count = 0;
    char topic[256];
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_recv (spot_b, &recv_parts, &recv_count, 0, topic, NULL));
    TEST_ASSERT_EQUAL_STRING ("ipc:test", topic);
    TEST_ASSERT_EQUAL_INT (1, (int) recv_count);
    TEST_ASSERT_EQUAL_MEMORY ("ipc-msg", zlink_msg_data (&recv_parts[0]), 7);
    zlink_msgv_close (recv_parts, recv_count);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&spot_a));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&spot_b));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node_a));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node_b));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
#endif
}

static void test_spot_peer_tcp ()
{
    if (!zlink_has ("tcp")) {
        TEST_IGNORE_MESSAGE ("TCP not available");
        return;
    }

    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *node_a = zlink_spot_node_new (ctx);
    TEST_ASSERT_NOT_NULL (node_a);
    void *node_b = zlink_spot_node_new (ctx);
    TEST_ASSERT_NOT_NULL (node_b);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_bind (node_a, "tcp://127.0.0.1:*"));

    char endpoint[MAX_SOCKET_STRING];
    size_t endpoint_len = sizeof (endpoint);
    void *pub_socket_a = zlink_spot_node_pub_socket (node_a);
    TEST_ASSERT_NOT_NULL (pub_socket_a);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_getsockopt (pub_socket_a, ZLINK_LAST_ENDPOINT, endpoint, &endpoint_len));

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_bind (node_b, "tcp://127.0.0.1:*"));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_connect_peer_pub (node_b, endpoint));

    void *spot_b = zlink_spot_new (node_b);
    TEST_ASSERT_NOT_NULL (spot_b);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_subscribe (spot_b, "tcp:test"));

    msleep (50);

    void *spot_a = zlink_spot_new (node_a);
    TEST_ASSERT_NOT_NULL (spot_a);

    zlink_msg_t parts[1];
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_init_size (&parts[0], 7));
    memcpy (zlink_msg_data (&parts[0]), "tcp-msg", 7);

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_publish (spot_a, "tcp:test", parts, 1, 0));

    zlink_msg_t *recv_parts = NULL;
    size_t recv_count = 0;
    char topic[256];
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_recv (spot_b, &recv_parts, &recv_count, 0, topic, NULL));
    TEST_ASSERT_EQUAL_STRING ("tcp:test", topic);
    TEST_ASSERT_EQUAL_INT (1, (int) recv_count);
    TEST_ASSERT_EQUAL_MEMORY ("tcp-msg", zlink_msg_data (&recv_parts[0]), 7);
    zlink_msgv_close (recv_parts, recv_count);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&spot_a));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&spot_b));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node_a));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node_b));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

static void test_spot_peer_ws ()
{
    if (!zlink_has ("ws")) {
        TEST_IGNORE_MESSAGE ("WS not available");
        return;
    }

    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *node_a = zlink_spot_node_new (ctx);
    TEST_ASSERT_NOT_NULL (node_a);
    void *node_b = zlink_spot_node_new (ctx);
    TEST_ASSERT_NOT_NULL (node_b);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_bind (node_a, "ws://127.0.0.1:*"));

    char endpoint[MAX_SOCKET_STRING];
    size_t endpoint_len = sizeof (endpoint);
    void *pub_socket_a = zlink_spot_node_pub_socket (node_a);
    TEST_ASSERT_NOT_NULL (pub_socket_a);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_getsockopt (pub_socket_a, ZLINK_LAST_ENDPOINT, endpoint, &endpoint_len));

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_bind (node_b, "ws://127.0.0.1:*"));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_connect_peer_pub (node_b, endpoint));

    void *spot_b = zlink_spot_new (node_b);
    TEST_ASSERT_NOT_NULL (spot_b);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_subscribe (spot_b, "ws:test"));

    msleep (50);

    void *spot_a = zlink_spot_new (node_a);
    TEST_ASSERT_NOT_NULL (spot_a);

    zlink_msg_t parts[1];
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_init_size (&parts[0], 6));
    memcpy (zlink_msg_data (&parts[0]), "ws-msg", 6);

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_publish (spot_a, "ws:test", parts, 1, 0));

    zlink_msg_t *recv_parts = NULL;
    size_t recv_count = 0;
    char topic[256];
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_recv (spot_b, &recv_parts, &recv_count, 0, topic, NULL));
    TEST_ASSERT_EQUAL_STRING ("ws:test", topic);
    TEST_ASSERT_EQUAL_INT (1, (int) recv_count);
    TEST_ASSERT_EQUAL_MEMORY ("ws-msg", zlink_msg_data (&recv_parts[0]), 6);
    zlink_msgv_close (recv_parts, recv_count);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&spot_a));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&spot_b));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node_a));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node_b));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

static void test_spot_peer_tls ()
{
    // TODO: TLS peer connection hangs - needs investigation
    // TCP/WS/WSS peer tests pass, but TLS has different behavior
    if (!zlink_has ("tls")) {
        TEST_IGNORE_MESSAGE ("TLS not available");
        return;
    }

    const tls_test_files_t files = make_tls_test_files ();

    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *node_a = zlink_spot_node_new (ctx);
    TEST_ASSERT_NOT_NULL (node_a);
    void *node_b = zlink_spot_node_new (ctx);
    TEST_ASSERT_NOT_NULL (node_b);

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_set_tls_server (node_a, files.server_cert.c_str (),
                                       files.server_key.c_str ()));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_bind (node_a, "tls://localhost:*"));

    char endpoint[MAX_SOCKET_STRING];
    size_t endpoint_len = sizeof (endpoint);
    void *pub_socket_a = zlink_spot_node_pub_socket (node_a);
    TEST_ASSERT_NOT_NULL (pub_socket_a);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_getsockopt (pub_socket_a, ZLINK_LAST_ENDPOINT, endpoint, &endpoint_len));

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_set_tls_client (node_b, files.ca_cert.c_str (), "localhost", 0));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_set_tls_server (node_b, files.server_cert.c_str (),
                                       files.server_key.c_str ()));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_bind (node_b, "tls://localhost:*"));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_connect_peer_pub (node_b, endpoint));

    void *spot_b = zlink_spot_new (node_b);
    TEST_ASSERT_NOT_NULL (spot_b);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_subscribe (spot_b, "tls:test"));

    msleep (500);  // TLS handshake needs more time

    void *spot_a = zlink_spot_new (node_a);
    TEST_ASSERT_NOT_NULL (spot_a);

    zlink_msg_t parts[1];
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_init_size (&parts[0], 7));
    memcpy (zlink_msg_data (&parts[0]), "tls-msg", 7);

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_publish (spot_a, "tls:test", parts, 1, 0));

    zlink_msg_t *recv_parts = NULL;
    size_t recv_count = 0;
    char topic[256];
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_recv (spot_b, &recv_parts, &recv_count, 0, topic, NULL));
    TEST_ASSERT_EQUAL_STRING ("tls:test", topic);
    TEST_ASSERT_EQUAL_INT (1, (int) recv_count);
    TEST_ASSERT_EQUAL_MEMORY ("tls-msg", zlink_msg_data (&recv_parts[0]), 7);
    zlink_msgv_close (recv_parts, recv_count);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&spot_a));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&spot_b));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node_a));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node_b));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));

    cleanup_tls_test_files (files);
}

static void test_spot_peer_wss ()
{
    // TODO: WSS peer connection hangs - same issue as TLS peer test
    if (!zlink_has ("wss")) {
        TEST_IGNORE_MESSAGE ("WSS not available");
        return;
    }

    const tls_test_files_t files = make_tls_test_files ();

    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *node_a = zlink_spot_node_new (ctx);
    TEST_ASSERT_NOT_NULL (node_a);
    void *node_b = zlink_spot_node_new (ctx);
    TEST_ASSERT_NOT_NULL (node_b);

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_set_tls_server (node_a, files.server_cert.c_str (),
                                       files.server_key.c_str ()));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_bind (node_a, "wss://localhost:*"));

    char endpoint[MAX_SOCKET_STRING];
    size_t endpoint_len = sizeof (endpoint);
    void *pub_socket_a = zlink_spot_node_pub_socket (node_a);
    TEST_ASSERT_NOT_NULL (pub_socket_a);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_getsockopt (pub_socket_a, ZLINK_LAST_ENDPOINT, endpoint, &endpoint_len));

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_set_tls_client (node_b, files.ca_cert.c_str (), "localhost", 0));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_set_tls_server (node_b, files.server_cert.c_str (),
                                       files.server_key.c_str ()));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_bind (node_b, "wss://localhost:*"));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_connect_peer_pub (node_b, endpoint));

    void *spot_b = zlink_spot_new (node_b);
    TEST_ASSERT_NOT_NULL (spot_b);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_subscribe (spot_b, "wss:test"));

    msleep (500);  // TLS handshake needs more time

    void *spot_a = zlink_spot_new (node_a);
    TEST_ASSERT_NOT_NULL (spot_a);

    zlink_msg_t parts[1];
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_init_size (&parts[0], 7));
    memcpy (zlink_msg_data (&parts[0]), "wss-msg", 7);

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_publish (spot_a, "wss:test", parts, 1, 0));

    zlink_msg_t *recv_parts = NULL;
    size_t recv_count = 0;
    char topic[256];
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_recv (spot_b, &recv_parts, &recv_count, 0, topic, NULL));
    TEST_ASSERT_EQUAL_STRING ("wss:test", topic);
    TEST_ASSERT_EQUAL_INT (1, (int) recv_count);
    TEST_ASSERT_EQUAL_MEMORY ("wss-msg", zlink_msg_data (&recv_parts[0]), 7);
    zlink_msgv_close (recv_parts, recv_count);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&spot_a));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&spot_b));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node_a));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node_b));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));

    cleanup_tls_test_files (files);
}

static void test_spot_unsubscribe ()
{
    // TODO: zlink_spot_sub_socket API ??? ???????
    TEST_IGNORE_MESSAGE ("Unsubscribe test pending - requires spot_sub_socket API");
    return;

    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *node = zlink_spot_node_new (ctx);
    TEST_ASSERT_NOT_NULL (node);

    void *spot = zlink_spot_new (node);
    TEST_ASSERT_NOT_NULL (spot);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_subscribe (spot, "unsub:topic"));

    zlink_msg_t parts[1];
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_init_size (&parts[0], 4));
    memcpy (zlink_msg_data (&parts[0]), "msg1", 4);

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_publish (spot, "unsub:topic", parts, 1, 0));

    zlink_msg_t *recv_parts = NULL;
    size_t recv_count = 0;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_recv (spot, &recv_parts, &recv_count, 0, NULL, NULL));
    TEST_ASSERT_EQUAL_INT (1, (int) recv_count);
    TEST_ASSERT_EQUAL_MEMORY ("msg1", zlink_msg_data (&recv_parts[0]), 4);
    zlink_msgv_close (recv_parts, recv_count);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_unsubscribe (spot, "unsub:topic"));

    msleep (50);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_init_size (&parts[0], 4));
    memcpy (zlink_msg_data (&parts[0]), "msg2", 4);

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_publish (spot, "unsub:topic", parts, 1, 0));

    int rcvtimeo = 100;
    void *sub_socket = zlink_spot_sub_socket (spot);
    TEST_ASSERT_NOT_NULL (sub_socket);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_setsockopt (sub_socket, ZLINK_RCVTIMEO, &rcvtimeo, sizeof (rcvtimeo)));

    int rc = zlink_spot_recv (spot, &recv_parts, &recv_count, 0, NULL, NULL);
    TEST_ASSERT_EQUAL_INT (-1, rc);
    TEST_ASSERT_EQUAL_INT (EAGAIN, errno);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&spot));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

static void test_spot_multi_publisher ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *node_a = zlink_spot_node_new (ctx);
    TEST_ASSERT_NOT_NULL (node_a);
    void *node_b = zlink_spot_node_new (ctx);
    TEST_ASSERT_NOT_NULL (node_b);
    void *node_c = zlink_spot_node_new (ctx);
    TEST_ASSERT_NOT_NULL (node_c);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_bind (node_a, "inproc://pub-a"));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_bind (node_b, "inproc://pub-b"));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_bind (node_c, "inproc://sub-c"));

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_connect_peer_pub (node_c, "inproc://pub-a"));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_connect_peer_pub (node_c, "inproc://pub-b"));

    void *spot_c = zlink_spot_new (node_c);
    TEST_ASSERT_NOT_NULL (spot_c);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_subscribe (spot_c, "multi:topic"));

    msleep (50);

    void *spot_a = zlink_spot_new (node_a);
    TEST_ASSERT_NOT_NULL (spot_a);
    void *spot_b = zlink_spot_new (node_b);
    TEST_ASSERT_NOT_NULL (spot_b);

    zlink_msg_t parts_a[1];
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_init_size (&parts_a[0], 5));
    memcpy (zlink_msg_data (&parts_a[0]), "from-a", 5);

    zlink_msg_t parts_b[1];
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_init_size (&parts_b[0], 6));
    memcpy (zlink_msg_data (&parts_b[0]), "from-b", 6);

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_publish (spot_a, "multi:topic", parts_a, 1, 0));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_publish (spot_b, "multi:topic", parts_b, 1, 0));

    zlink_msg_t *recv_parts_1 = NULL;
    size_t recv_count_1 = 0;
    char topic_1[256];
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_recv (spot_c, &recv_parts_1, &recv_count_1, 0, topic_1, NULL));
    TEST_ASSERT_EQUAL_STRING ("multi:topic", topic_1);
    TEST_ASSERT_EQUAL_INT (1, (int) recv_count_1);

    zlink_msg_t *recv_parts_2 = NULL;
    size_t recv_count_2 = 0;
    char topic_2[256];
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_recv (spot_c, &recv_parts_2, &recv_count_2, 0, topic_2, NULL));
    TEST_ASSERT_EQUAL_STRING ("multi:topic", topic_2);
    TEST_ASSERT_EQUAL_INT (1, (int) recv_count_2);

    bool got_from_a = false;
    bool got_from_b = false;

    if (zlink_msg_size (&recv_parts_1[0]) == 5
        && memcmp (zlink_msg_data (&recv_parts_1[0]), "from-a", 5) == 0) {
        got_from_a = true;
    } else if (zlink_msg_size (&recv_parts_1[0]) == 6
               && memcmp (zlink_msg_data (&recv_parts_1[0]), "from-b", 6) == 0) {
        got_from_b = true;
    }

    if (zlink_msg_size (&recv_parts_2[0]) == 5
        && memcmp (zlink_msg_data (&recv_parts_2[0]), "from-a", 5) == 0) {
        got_from_a = true;
    } else if (zlink_msg_size (&recv_parts_2[0]) == 6
               && memcmp (zlink_msg_data (&recv_parts_2[0]), "from-b", 6) == 0) {
        got_from_b = true;
    }

    TEST_ASSERT_TRUE (got_from_a);
    TEST_ASSERT_TRUE (got_from_b);

    zlink_msgv_close (recv_parts_1, recv_count_1);
    zlink_msgv_close (recv_parts_2, recv_count_2);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&spot_a));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&spot_b));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&spot_c));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node_a));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node_b));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node_c));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

static void test_spot_node_setsockopt ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *node = zlink_spot_node_new (ctx);
    TEST_ASSERT_NOT_NULL (node);

    /* Queue PUB SNDHWM before bind (socket does not exist yet) */
    int sndhwm = 500;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_setsockopt (node, ZLINK_SPOT_NODE_SOCKET_PUB,
                                  ZLINK_SNDHWM, &sndhwm, sizeof (sndhwm)));

    /* Queue SUB RCVHWM before the worker creates the socket */
    int rcvhwm = 600;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_setsockopt (node, ZLINK_SPOT_NODE_SOCKET_SUB,
                                  ZLINK_RCVHWM, &rcvhwm, sizeof (rcvhwm)));

    /* Invalid socket role returns -1 */
    int dummy = 1;
    int rc = zlink_spot_node_setsockopt (node, 99, ZLINK_SNDHWM, &dummy,
                                         sizeof (dummy));
    TEST_ASSERT_EQUAL_INT (-1, rc);

    /* bind triggers PUB creation -> queued opts applied */
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_bind (node, "inproc://spot-sockopt"));

    void *pub_sock = zlink_spot_node_pub_socket (node);
    TEST_ASSERT_NOT_NULL (pub_sock);

    int actual_sndhwm = 0;
    size_t optlen = sizeof (actual_sndhwm);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_getsockopt (pub_sock, ZLINK_SNDHWM, &actual_sndhwm, &optlen));
    TEST_ASSERT_EQUAL_INT (500, actual_sndhwm);

    /* Set PUB option after socket exists -> applied immediately */
    int sndhwm2 = 700;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_setsockopt (node, ZLINK_SPOT_NODE_SOCKET_PUB,
                                  ZLINK_SNDHWM, &sndhwm2, sizeof (sndhwm2)));
    actual_sndhwm = 0;
    optlen = sizeof (actual_sndhwm);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_getsockopt (pub_sock, ZLINK_SNDHWM, &actual_sndhwm, &optlen));
    TEST_ASSERT_EQUAL_INT (700, actual_sndhwm);

    /* spot_t setsockopt delegates to node */
    void *spot = zlink_spot_new (node);
    TEST_ASSERT_NOT_NULL (spot);

    int spot_sndhwm = 800;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_setsockopt (spot, ZLINK_SPOT_SOCKET_PUB,
                             ZLINK_SNDHWM, &spot_sndhwm,
                             sizeof (spot_sndhwm)));
    actual_sndhwm = 0;
    optlen = sizeof (actual_sndhwm);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_getsockopt (pub_sock, ZLINK_SNDHWM, &actual_sndhwm, &optlen));
    TEST_ASSERT_EQUAL_INT (800, actual_sndhwm);

    /* DEALER role on spot is rejected */
    rc = zlink_spot_setsockopt (spot, 99, ZLINK_SNDHWM, &dummy, sizeof (dummy));
    TEST_ASSERT_EQUAL_INT (-1, rc);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&spot));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

int main (int, char **)
{
    setup_test_environment ();

    UNITY_BEGIN ();
    RUN_TEST (test_spot_local_pubsub);
    RUN_TEST (test_spot_pattern_subscribe);
    RUN_TEST (test_spot_publish_no_subscribers);
    RUN_TEST (test_spot_multipart_publish);
    RUN_TEST (test_spot_peer_pubsub);
    RUN_TEST (test_spot_peer_ipc);
    RUN_TEST (test_spot_peer_tcp);
    RUN_TEST (test_spot_peer_ws);
    RUN_TEST (test_spot_peer_tls);
    RUN_TEST (test_spot_peer_wss);
    RUN_TEST (test_spot_unsubscribe);
    RUN_TEST (test_spot_multi_publisher);
    RUN_TEST (test_spot_node_setsockopt);
    return UNITY_END ();
}

