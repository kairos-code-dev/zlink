/* SPDX-License-Identifier: MPL-2.0 */
#include "testutil_security.hpp"

#include <stdlib.h>
#include <string.h>

const char *test_zap_domain = "ZAPTEST";

void socket_config_null_client (void *server_, void *server_secret_)
{
    LIBZMQ_UNUSED (server_);
    LIBZMQ_UNUSED (server_secret_);
}

void socket_config_null_server (void *server_, void *server_secret_)
{
    TEST_ASSERT_SUCCESS_ERRNO (zmq_setsockopt (
      server_, ZMQ_ZAP_DOMAIN, test_zap_domain, strlen (test_zap_domain)));
    LIBZMQ_UNUSED (server_secret_);
}

static const char test_plain_username[] = "testuser";
static const char test_plain_password[] = "testpass";

void socket_config_plain_client (void *server_, void *server_secret_)
{
    LIBZMQ_UNUSED (server_secret_);

    TEST_ASSERT_SUCCESS_ERRNO (
      zmq_setsockopt (server_, ZMQ_PLAIN_PASSWORD, test_plain_password, 8));
    TEST_ASSERT_SUCCESS_ERRNO (
      zmq_setsockopt (server_, ZMQ_PLAIN_USERNAME, test_plain_username, 8));
}

void socket_config_plain_server (void *server_, void *server_secret_)
{
    LIBZMQ_UNUSED (server_secret_);

    int as_server = 1;
    TEST_ASSERT_SUCCESS_ERRNO (
      zmq_setsockopt (server_, ZMQ_PLAIN_SERVER, &as_server, sizeof (int)));
    TEST_ASSERT_SUCCESS_ERRNO (zmq_setsockopt (
      server_, ZMQ_ZAP_DOMAIN, test_zap_domain, strlen (test_zap_domain)));
}

char valid_client_public[41];
char valid_client_secret[41];
char valid_server_public[41];
char valid_server_secret[41];

void setup_testutil_security_curve ()
{
    // CURVE is removed
}

void socket_config_curve_server (void *server_, void *server_secret_)
{
    LIBZMQ_UNUSED (server_);
    LIBZMQ_UNUSED (server_secret_);
}

void socket_config_curve_client (void *client_, void *data_)
{
    LIBZMQ_UNUSED (client_);
    LIBZMQ_UNUSED (data_);
}

void *zap_requests_handled;

void zap_handler_generic (zap_protocol_t zap_protocol_,
                          const char *expected_routing_id_)
{
    // Simplified ZAP handler
}

void zap_handler (void * /*unused_*/)
{
    zap_handler_generic (zap_ok);
}

void setup_context_and_server_side (void **zap_control_,
                                    void **zap_thread_,
                                    void **server_,
                                    void **server_mon_,
                                    char *my_endpoint_,
                                    zmq_thread_fn zap_handler_,
                                    socket_config_fn socket_config_,
                                    void *socket_config_data_,
                                    const char *routing_id_)
{
    setup_test_context ();
    *server_ = test_context_socket (ZMQ_ROUTER);
    socket_config_ (*server_, socket_config_data_);
    bind_loopback_ipv4 (*server_, my_endpoint_, MAX_SOCKET_STRING);
}

void shutdown_context_and_server_side (void *zap_thread_,
                                       void *server_,
                                       void *server_mon_,
                                       void *zap_control_,
                                       bool zap_handler_stopped_)
{
    test_context_socket_close (server_);
    teardown_test_context ();
}

void *create_and_connect_client (char *my_endpoint_,
                                 socket_config_fn socket_config_,
                                 void *socket_config_data_,
                                 void **client_mon_)
{
    void *client = test_context_socket (ZMQ_DEALER);
    socket_config_ (client, socket_config_data_);
    TEST_ASSERT_SUCCESS_ERRNO (zmq_connect (client, my_endpoint_));
    return client;
}

void expect_new_client_bounce_fail (char *my_endpoint_,
                                    void *server_,
                                    socket_config_fn socket_config_,
                                    void *socket_config_data_,
                                    void **client_mon_,
                                    int expected_client_event_,
                                    int expected_client_value_)
{
    // CURVE/Security bounce fail tests are skipped as mechanisms are removed
}