/* SPDX-License-Identifier: MPL-2.0 */

#include "testutil.hpp"
#include "testutil_unity.hpp"

#include "utils/config.hpp"

#include <string.h>

SETUP_TEARDOWN_TESTCONTEXT

static const size_t stream_routing_id_size = 5;

static void recv_stream_event (void *socket_,
                               unsigned char expected_code_,
                               unsigned char routing_id_[stream_routing_id_size])
{
    int rc = zlink_recv (socket_, routing_id_, stream_routing_id_size, 0);
    TEST_ASSERT_EQUAL_INT (static_cast<int> (stream_routing_id_size), rc);

    int more = 0;
    size_t more_size = sizeof (more);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_getsockopt (socket_, ZLINK_RCVMORE, &more, &more_size));
    TEST_ASSERT_TRUE (more);

    unsigned char code = 0xFF;
    rc = zlink_recv (socket_, &code, 1, 0);
    TEST_ASSERT_EQUAL_INT (1, rc);
    TEST_ASSERT_EQUAL_UINT8 (expected_code_, code);
}

static void send_stream_msg (void *socket_,
                             const unsigned char routing_id_[stream_routing_id_size],
                             const void *data_,
                             size_t size_)
{
    TEST_ASSERT_EQUAL_INT (
      static_cast<int> (stream_routing_id_size),
      TEST_ASSERT_SUCCESS_ERRNO (zlink_send (socket_, routing_id_,
                                           stream_routing_id_size,
                                           ZLINK_SNDMORE)));
    TEST_ASSERT_EQUAL_INT ((int) size_,
                           TEST_ASSERT_SUCCESS_ERRNO (
                             zlink_send (socket_, data_, size_, 0)));
}

static int recv_stream_msg (void *socket_,
                            unsigned char routing_id_[stream_routing_id_size],
                            void *buf_,
                            size_t buf_size_)
{
    int rc = zlink_recv (socket_, routing_id_, stream_routing_id_size, 0);
    if (rc != static_cast<int> (stream_routing_id_size))
        return -1;

    int more = 0;
    size_t more_size = sizeof (more);
    zlink_getsockopt (socket_, ZLINK_RCVMORE, &more, &more_size);
    if (!more)
        return -1;

    return zlink_recv (socket_, buf_, buf_size_, 0);
}

void test_stream_tcp_basic ()
{
    void *server = test_context_socket (ZLINK_STREAM);
    void *client = test_context_socket (ZLINK_STREAM);
    TEST_ASSERT_NOT_NULL (server);
    TEST_ASSERT_NOT_NULL (client);

    const int zero = 0;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_setsockopt (server, ZLINK_LINGER, &zero, sizeof (zero)));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_setsockopt (client, ZLINK_LINGER, &zero, sizeof (zero)));

    char endpoint[MAX_SOCKET_STRING];
    bind_loopback_ipv4 (server, endpoint, sizeof (endpoint));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_connect (client, endpoint));

    unsigned char server_id[stream_routing_id_size];
    unsigned char client_id[stream_routing_id_size];

    recv_stream_event (server, 0x01, server_id);
    recv_stream_event (client, 0x01, client_id);

    const char payload[] = "hello";
    send_stream_msg (client, client_id, payload, sizeof (payload) - 1);

    unsigned char recv_id[stream_routing_id_size];
    char recv_buf[64];
    int rc = recv_stream_msg (server, recv_id, recv_buf, sizeof (recv_buf));
    TEST_ASSERT_EQUAL_INT ((int) sizeof (payload) - 1, rc);
    TEST_ASSERT_EQUAL_UINT8_ARRAY (server_id, recv_id,
                                   stream_routing_id_size);
    TEST_ASSERT_EQUAL_STRING_LEN (payload, recv_buf, sizeof (payload) - 1);

    const char reply[] = "world";
    send_stream_msg (server, server_id, reply, sizeof (reply) - 1);

    rc = recv_stream_msg (client, recv_id, recv_buf, sizeof (recv_buf));
    TEST_ASSERT_EQUAL_INT ((int) sizeof (reply) - 1, rc);
    TEST_ASSERT_EQUAL_UINT8_ARRAY (client_id, recv_id,
                                   stream_routing_id_size);
    TEST_ASSERT_EQUAL_STRING_LEN (reply, recv_buf, sizeof (reply) - 1);

    test_context_socket_close_zero_linger (client);

    zlink_pollitem_t items[] = {{server, 0, ZLINK_POLLIN, 0}};
    TEST_ASSERT_EQUAL_INT (1, zlink_poll (items, 1, 2000));

    recv_stream_event (server, 0x00, recv_id);
    TEST_ASSERT_EQUAL_UINT8_ARRAY (server_id, recv_id,
                                   stream_routing_id_size);

    test_context_socket_close_zero_linger (server);
}

void test_stream_maxmsgsize ()
{
    void *server = test_context_socket (ZLINK_STREAM);
    void *client = test_context_socket (ZLINK_STREAM);
    TEST_ASSERT_NOT_NULL (server);
    TEST_ASSERT_NOT_NULL (client);

    const int zero = 0;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_setsockopt (server, ZLINK_LINGER, &zero, sizeof (zero)));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_setsockopt (client, ZLINK_LINGER, &zero, sizeof (zero)));

    const int64_t maxmsgsize = 4;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_setsockopt (server, ZLINK_MAXMSGSIZE, &maxmsgsize, sizeof (maxmsgsize)));

    char endpoint[MAX_SOCKET_STRING];
    bind_loopback_ipv4 (server, endpoint, sizeof (endpoint));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_connect (client, endpoint));

    unsigned char server_id[stream_routing_id_size];
    unsigned char client_id[stream_routing_id_size];

    recv_stream_event (server, 0x01, server_id);
    recv_stream_event (client, 0x01, client_id);

    const char payload[] = "toolarge";
    send_stream_msg (client, client_id, payload, sizeof (payload) - 1);

    zlink_pollitem_t items[] = {{server, 0, ZLINK_POLLIN, 0}};
    TEST_ASSERT_EQUAL_INT (1, zlink_poll (items, 1, 2000));

    unsigned char recv_id[stream_routing_id_size];
    recv_stream_event (server, 0x00, recv_id);
    TEST_ASSERT_EQUAL_UINT8_ARRAY (server_id, recv_id, 4);

    test_context_socket_close_zero_linger (client);
    test_context_socket_close_zero_linger (server);
}

#if defined ZLINK_HAVE_WS
void test_stream_ws_basic ()
{
    void *server = test_context_socket (ZLINK_STREAM);
    void *client = test_context_socket (ZLINK_STREAM);
    TEST_ASSERT_NOT_NULL (server);
    TEST_ASSERT_NOT_NULL (client);

    const int zero = 0;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_setsockopt (server, ZLINK_LINGER, &zero, sizeof (zero)));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_setsockopt (client, ZLINK_LINGER, &zero, sizeof (zero)));

    TEST_ASSERT_SUCCESS_ERRNO (zlink_bind (server, "ws://127.0.0.1:*") );

    char endpoint[256];
    size_t endpoint_len = sizeof (endpoint);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_getsockopt (server, ZLINK_LAST_ENDPOINT, endpoint, &endpoint_len));

    TEST_ASSERT_SUCCESS_ERRNO (zlink_connect (client, endpoint));

    unsigned char server_id[stream_routing_id_size];
    unsigned char client_id[stream_routing_id_size];

    recv_stream_event (server, 0x01, server_id);
    recv_stream_event (client, 0x01, client_id);

    const char payload[] = "ws";
    send_stream_msg (client, client_id, payload, sizeof (payload) - 1);

    unsigned char recv_id[stream_routing_id_size];
    char recv_buf[64];
    int rc = recv_stream_msg (server, recv_id, recv_buf, sizeof (recv_buf));
    TEST_ASSERT_EQUAL_INT ((int) sizeof (payload) - 1, rc);
    TEST_ASSERT_EQUAL_UINT8_ARRAY (server_id, recv_id, 4);
    TEST_ASSERT_EQUAL_STRING_LEN (payload, recv_buf, sizeof (payload) - 1);

    test_context_socket_close_zero_linger (client);
    test_context_socket_close_zero_linger (server);
}

#if defined ZLINK_HAVE_WSS
void test_stream_wss_basic ()
{
    const tls_test_files_t files = make_tls_test_files ();

    void *server = test_context_socket (ZLINK_STREAM);
    void *client = test_context_socket (ZLINK_STREAM);
    TEST_ASSERT_NOT_NULL (server);
    TEST_ASSERT_NOT_NULL (client);

    const int zero = 0;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_setsockopt (server, ZLINK_LINGER, &zero, sizeof (zero)));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_setsockopt (client, ZLINK_LINGER, &zero, sizeof (zero)));

    const int trust_system = 0;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_setsockopt (
      client, ZLINK_TLS_TRUST_SYSTEM, &trust_system, sizeof (trust_system)));

    TEST_ASSERT_SUCCESS_ERRNO (zlink_setsockopt (
      server, ZLINK_TLS_CERT, files.server_cert.c_str (),
      files.server_cert.size ()));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_setsockopt (
      server, ZLINK_TLS_KEY, files.server_key.c_str (),
      files.server_key.size ()));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_setsockopt (
      client, ZLINK_TLS_CA, files.ca_cert.c_str (), files.ca_cert.size ()));

    const char hostname[] = "localhost";
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_setsockopt (client, ZLINK_TLS_HOSTNAME, hostname, strlen (hostname)));

    TEST_ASSERT_SUCCESS_ERRNO (zlink_bind (server, "wss://127.0.0.1:*") );

    char endpoint[256];
    size_t endpoint_len = sizeof (endpoint);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_getsockopt (server, ZLINK_LAST_ENDPOINT, endpoint, &endpoint_len));

    TEST_ASSERT_SUCCESS_ERRNO (zlink_connect (client, endpoint));

    unsigned char server_id[stream_routing_id_size];
    unsigned char client_id[stream_routing_id_size];

    recv_stream_event (server, 0x01, server_id);
    recv_stream_event (client, 0x01, client_id);

    const char payload[] = "wss";
    send_stream_msg (client, client_id, payload, sizeof (payload) - 1);

    unsigned char recv_id[stream_routing_id_size];
    char recv_buf[64];
    int rc = recv_stream_msg (server, recv_id, recv_buf, sizeof (recv_buf));
    TEST_ASSERT_EQUAL_INT ((int) sizeof (payload) - 1, rc);
    TEST_ASSERT_EQUAL_UINT8_ARRAY (server_id, recv_id, 4);
    TEST_ASSERT_EQUAL_STRING_LEN (payload, recv_buf, sizeof (payload) - 1);

    test_context_socket_close_zero_linger (client);
    test_context_socket_close_zero_linger (server);
    cleanup_tls_test_files (files);
}
#endif  // ZLINK_HAVE_WSS
#endif  // ZLINK_HAVE_WS

int main (void)
{
    UNITY_BEGIN ();

    setup_test_environment ();

    RUN_TEST (test_stream_tcp_basic);
    RUN_TEST (test_stream_maxmsgsize);

#if defined ZLINK_HAVE_WS
    RUN_TEST (test_stream_ws_basic);
#if defined ZLINK_HAVE_WSS
    RUN_TEST (test_stream_wss_basic);
#endif
#else
    TEST_IGNORE_MESSAGE ("WebSocket support not enabled");
#endif

    return UNITY_END ();
}
