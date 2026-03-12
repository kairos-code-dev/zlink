/* SPDX-License-Identifier: MPL-2.0 */

#include "testutil.hpp"
#include "testutil_unity.hpp"

#include <cstring>

SETUP_TEARDOWN_TESTCONTEXT

static bool is_transport_available (const char *transport_)
{
    //  TCP and inproc are always available (core transports)
    if (strcmp (transport_, "tcp") == 0 || strcmp (transport_, "inproc") == 0)
        return true;

    //  IPC is available on Unix-like systems
#ifdef ZLINK_HAVE_IPC
    if (strcmp (transport_, "ipc") == 0)
        return true;
#endif

    //  WebSocket and TLS transports are optional and reported by zlink_has()
    return zlink_has (transport_) != 0;
}

static bool is_tls_transport (const char *transport_)
{
    return strcmp (transport_, "tls") == 0 || strcmp (transport_, "wss") == 0;
}

static void configure_tls (void *server_,
                           void *client_,
                           const tls_test_files_t &files_)
{
    const int trust_system = 0;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_setsockopt (
      client_, ZLINK_TLS_TRUST_SYSTEM, &trust_system, sizeof (trust_system)));

    TEST_ASSERT_SUCCESS_ERRNO (zlink_setsockopt (
      server_, ZLINK_TLS_CERT, files_.server_cert.c_str (),
      files_.server_cert.size ()));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_setsockopt (
      server_, ZLINK_TLS_KEY, files_.server_key.c_str (),
      files_.server_key.size ()));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_setsockopt (
      client_, ZLINK_TLS_CA, files_.ca_cert.c_str (), files_.ca_cert.size ()));

    const char hostname[] = "localhost";
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_setsockopt (client_, ZLINK_TLS_HOSTNAME, hostname, strlen (hostname)));
}

static void bind_endpoint (void *socket_,
                           const char *transport_,
                           const char *inproc_name_,
                           char *endpoint_,
                           size_t endpoint_len_)
{
    if (strcmp (transport_, "inproc") == 0) {
        snprintf (endpoint_, endpoint_len_, "inproc://%s", inproc_name_);
        TEST_ASSERT_SUCCESS_ERRNO (zlink_bind (socket_, endpoint_));
        return;
    }

    if (strcmp (transport_, "tcp") == 0) {
        test_bind (socket_, "tcp://127.0.0.1:*", endpoint_, endpoint_len_);
        return;
    }

    if (strcmp (transport_, "ipc") == 0) {
        test_bind (socket_, "ipc://*", endpoint_, endpoint_len_);
        return;
    }

    if (strcmp (transport_, "ws") == 0) {
        test_bind (socket_, "ws://127.0.0.1:*", endpoint_, endpoint_len_);
        return;
    }

    if (strcmp (transport_, "wss") == 0) {
        test_bind (socket_, "wss://127.0.0.1:*", endpoint_, endpoint_len_);
        return;
    }

    if (strcmp (transport_, "tls") == 0) {
        test_bind (socket_, "tls://127.0.0.1:*", endpoint_, endpoint_len_);
        return;
    }

    TEST_FAIL_MESSAGE ("unknown transport");
}

static void run_pair (const char *transport_)
{
    if (!is_transport_available (transport_))
        TEST_IGNORE_MESSAGE ("transport not available");

    void *server = test_context_socket (ZLINK_PAIR);
    void *client = test_context_socket (ZLINK_PAIR);

    tls_test_files_t tls_files;
    if (is_tls_transport (transport_)) {
        tls_files = make_tls_test_files ();
        configure_tls (server, client, tls_files);
    }

    char endpoint[MAX_SOCKET_STRING];
    bind_endpoint (server, transport_, "matrix_pair", endpoint, sizeof (endpoint));

    TEST_ASSERT_SUCCESS_ERRNO (zlink_connect (client, endpoint));
    msleep (SETTLE_TIME);

    send_string_expect_success (client, "pair-hello", 0);
    recv_string_expect_success (server, "pair-hello", 0);
    send_string_expect_success (server, "pair-ack", 0);
    recv_string_expect_success (client, "pair-ack", 0);

    test_context_socket_close (client);
    test_context_socket_close (server);
    if (is_tls_transport (transport_))
        cleanup_tls_test_files (tls_files);
}

static void run_pubsub (const char *transport_)
{
    if (!is_transport_available (transport_))
        TEST_IGNORE_MESSAGE ("transport not available");

    void *pub = test_context_socket (ZLINK_PUB);
    void *sub = test_context_socket (ZLINK_SUB);

    tls_test_files_t tls_files;
    if (is_tls_transport (transport_)) {
        tls_files = make_tls_test_files ();
        configure_tls (pub, sub, tls_files);
    }

    char endpoint[MAX_SOCKET_STRING];
    bind_endpoint (pub, transport_, "matrix_pubsub", endpoint, sizeof (endpoint));

    TEST_ASSERT_SUCCESS_ERRNO (zlink_connect (sub, endpoint));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_setsockopt (sub, ZLINK_SUBSCRIBE, "", 0));
    msleep (SETTLE_TIME);

    send_string_expect_success (pub, "pubsub-hello", 0);
    recv_string_expect_success (sub, "pubsub-hello", 0);

    test_context_socket_close (sub);
    test_context_socket_close (pub);
    if (is_tls_transport (transport_))
        cleanup_tls_test_files (tls_files);
}

static void run_router_dealer (const char *transport_)
{
    if (!is_transport_available (transport_))
        TEST_IGNORE_MESSAGE ("transport not available");

    void *router = test_context_socket (ZLINK_ROUTER);
    void *dealer = test_context_socket (ZLINK_DEALER);

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_setsockopt (dealer, ZLINK_ROUTING_ID, "DEALER1", 7));

    tls_test_files_t tls_files;
    if (is_tls_transport (transport_)) {
        tls_files = make_tls_test_files ();
        configure_tls (router, dealer, tls_files);
    }

    char endpoint[MAX_SOCKET_STRING];
    bind_endpoint (router, transport_, "matrix_router_dealer", endpoint,
                   sizeof (endpoint));

    TEST_ASSERT_SUCCESS_ERRNO (zlink_connect (dealer, endpoint));
    msleep (SETTLE_TIME);

    send_string_expect_success (dealer, "dealer-msg", 0);

    char identity[32];
    int id_size =
      TEST_ASSERT_SUCCESS_ERRNO (zlink_recv (router, identity, sizeof (identity), 0));
    TEST_ASSERT_EQUAL_INT (7, id_size);
    TEST_ASSERT_EQUAL_STRING_LEN ("DEALER1", identity, 7);
    recv_string_expect_success (router, "dealer-msg", 0);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_send (router, "DEALER1", 7, ZLINK_SNDMORE));
    send_string_expect_success (router, "router-reply", 0);
    recv_string_expect_success (dealer, "router-reply", 0);

    test_context_socket_close (dealer);
    test_context_socket_close (router);
    if (is_tls_transport (transport_))
        cleanup_tls_test_files (tls_files);
}

static void run_router_router (const char *transport_)
{
    if (!is_transport_available (transport_))
        TEST_IGNORE_MESSAGE ("transport not available");

    void *server = test_context_socket (ZLINK_ROUTER);
    void *client = test_context_socket (ZLINK_ROUTER);

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_setsockopt (server, ZLINK_ROUTING_ID, "SERVER", 6));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_setsockopt (client, ZLINK_ROUTING_ID, "CLIENT", 6));

    tls_test_files_t tls_files;
    if (is_tls_transport (transport_)) {
        tls_files = make_tls_test_files ();
        configure_tls (server, client, tls_files);
    }

    char endpoint[MAX_SOCKET_STRING];
    bind_endpoint (server, transport_, "matrix_router_router", endpoint,
                   sizeof (endpoint));

    TEST_ASSERT_SUCCESS_ERRNO (zlink_connect (client, endpoint));
    msleep (SETTLE_TIME);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_send (client, "SERVER", 6, ZLINK_SNDMORE));
    send_string_expect_success (client, "router-msg", 0);

    char identity[32];
    int id_size =
      TEST_ASSERT_SUCCESS_ERRNO (zlink_recv (server, identity, sizeof (identity), 0));
    TEST_ASSERT_EQUAL_INT (6, id_size);
    TEST_ASSERT_EQUAL_STRING_LEN ("CLIENT", identity, 6);
    recv_string_expect_success (server, "router-msg", 0);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_send (server, "CLIENT", 6, ZLINK_SNDMORE));
    send_string_expect_success (server, "router-reply", 0);

    id_size =
      TEST_ASSERT_SUCCESS_ERRNO (zlink_recv (client, identity, sizeof (identity), 0));
    TEST_ASSERT_EQUAL_INT (6, id_size);
    TEST_ASSERT_EQUAL_STRING_LEN ("SERVER", identity, 6);
    recv_string_expect_success (client, "router-reply", 0);

    test_context_socket_close (client);
    test_context_socket_close (server);
    if (is_tls_transport (transport_))
        cleanup_tls_test_files (tls_files);
}

static void test_transport_matrix (const char *transport_)
{
    fprintf (stderr, "Testing transport: %s\n", transport_);
    fflush (stderr);

    run_pair (transport_);
    fprintf (stderr, "  PAIR complete\n");
    fflush (stderr);

    run_pubsub (transport_);
    fprintf (stderr, "  PUB/SUB complete\n");
    fflush (stderr);

    run_router_dealer (transport_);
    fprintf (stderr, "  ROUTER/DEALER complete\n");
    fflush (stderr);

    run_router_router (transport_);
    fprintf (stderr, "  ROUTER/ROUTER complete\n");
    fflush (stderr);
}

void test_matrix_tcp ()
{
    test_transport_matrix ("tcp");
}

void test_matrix_inproc ()
{
    test_transport_matrix ("inproc");
}

void test_matrix_ipc ()
{
    test_transport_matrix ("ipc");
}

void test_matrix_ws ()
{
    test_transport_matrix ("ws");
}

void test_matrix_wss ()
{
    test_transport_matrix ("wss");
}

void test_matrix_tls ()
{
    test_transport_matrix ("tls");
}

int main ()
{
    setup_test_environment (); //  Use default 60 second timeout

    UNITY_BEGIN ();
    RUN_TEST (test_matrix_tcp);
    RUN_TEST (test_matrix_inproc);
    RUN_TEST (test_matrix_ipc);
    RUN_TEST (test_matrix_ws);
    RUN_TEST (test_matrix_wss);
    RUN_TEST (test_matrix_tls);
    return UNITY_END ();
}
