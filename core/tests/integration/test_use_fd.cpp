/* SPDX-License-Identifier: MPL-2.0 */

#include "testutil.hpp"
#include "testutil_unity.hpp"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

SETUP_TEARDOWN_TESTCONTEXT

#if !defined(ZLINK_HAVE_WINDOWS)

void pre_allocate_sock_tcp (void *socket_, char *my_endpoint_)
{
    fd_t s = bind_socket_resolve_port ("127.0.0.1", "0", my_endpoint_);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_setsockopt (socket_, ZLINK_USE_FD, &s, sizeof (s)));
}

typedef void (*pre_allocate_sock_fun_t) (void *, char *);

void setup_socket_pair (pre_allocate_sock_fun_t pre_allocate_sock_fun_,
                        int bind_socket_type_,
                        int connect_socket_type_,
                        void **out_sb_,
                        void **out_sc_)
{
    *out_sb_ = test_context_socket (bind_socket_type_);

    char my_endpoint[MAX_SOCKET_STRING];
    pre_allocate_sock_fun_ (*out_sb_, my_endpoint);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_bind (*out_sb_, my_endpoint));

    *out_sc_ = test_context_socket (connect_socket_type_);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_connect (*out_sc_, my_endpoint));
}

void test_socket_pair (pre_allocate_sock_fun_t pre_allocate_sock_fun_,
                       int bind_socket_type_,
                       int connect_socket_type_)
{
    void *sb, *sc;
    setup_socket_pair (pre_allocate_sock_fun_, bind_socket_type_,
                       connect_socket_type_, &sb, &sc);

    bounce (sb, sc);

    test_context_socket_close (sc);
    test_context_socket_close (sb);
}

void test_pair (pre_allocate_sock_fun_t pre_allocate_sock_fun_)
{
    test_socket_pair (pre_allocate_sock_fun_, ZLINK_PAIR, ZLINK_PAIR);
}

void test_client_server (pre_allocate_sock_fun_t pre_allocate_sock_fun_)
{
#if defined(ZLINK_SERVER) && defined(ZLINK_CLIENT)
    void *sb, *sc;
    setup_socket_pair (pre_allocate_sock_fun_, ZLINK_SERVER, ZLINK_CLIENT, &sb,
                       &sc);

    zlink_msg_t msg;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_init_size (&msg, 1));

    char *data = static_cast<char *> (zlink_msg_data (&msg));
    data[0] = 1;

    int rc = zlink_msg_send (&msg, sc, ZLINK_SNDMORE);
    // TODO which error code is expected?
    TEST_ASSERT_EQUAL_INT (-1, rc);

    rc = zlink_msg_send (&msg, sc, 0);
    TEST_ASSERT_EQUAL_INT (1, rc);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_init (&msg));

    rc = zlink_msg_recv (&msg, sb, 0);
    TEST_ASSERT_EQUAL_INT (1, rc);

    uint32_t routing_id = zlink_msg_routing_id (&msg);
    TEST_ASSERT_NOT_EQUAL (0, routing_id);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_close (&msg));

    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_init_size (&msg, 1));

    data = static_cast<char *> (zlink_msg_data (&msg));
    data[0] = 2;

    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_set_routing_id (&msg, routing_id));

    rc = zlink_msg_send (&msg, sb, ZLINK_SNDMORE);
    // TODO which error code is expected?
    TEST_ASSERT_EQUAL_INT (-1, rc);

    rc = zlink_msg_send (&msg, sb, 0);
    TEST_ASSERT_EQUAL_INT (1, rc);

    rc = zlink_msg_recv (&msg, sc, 0);
    TEST_ASSERT_EQUAL_INT (1, rc);

    routing_id = zlink_msg_routing_id (&msg);
    TEST_ASSERT_EQUAL_INT (0, routing_id);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_close (&msg));

    test_context_socket_close (sc);
    test_context_socket_close (sb);
#endif
}

void test_pair_tcp ()
{
    test_pair (pre_allocate_sock_tcp);
}

void test_client_server_tcp ()
{
#if defined(ZLINK_SERVER) && defined(ZLINK_CLIENT)
    test_client_server (pre_allocate_sock_tcp);
#endif
}

char ipc_endpoint[MAX_SOCKET_STRING] = "";

void pre_allocate_sock_ipc (void *sb_, char *my_endpoint_)
{
    fd_t s = bind_socket_resolve_port ("", "", my_endpoint_, AF_UNIX, 0);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_setsockopt (sb_, ZLINK_USE_FD, &s, sizeof (s)));
    strcpy (ipc_endpoint, strchr (my_endpoint_, '/') + 2);
}

void test_pair_ipc ()
{
    test_pair (pre_allocate_sock_ipc);

    TEST_ASSERT_SUCCESS_ERRNO (unlink (ipc_endpoint));
}

void test_client_server_ipc ()
{
#if defined(ZLINK_SERVER) && defined(ZLINK_CLIENT)
    test_client_server (pre_allocate_sock_ipc);

    TEST_ASSERT_SUCCESS_ERRNO (unlink (ipc_endpoint));
#endif
}

int main ()
{
    setup_test_environment ();

    UNITY_BEGIN ();
    RUN_TEST (test_pair_tcp);
    RUN_TEST (test_client_server_tcp);

    RUN_TEST (test_pair_ipc);
    RUN_TEST (test_client_server_ipc);

    return UNITY_END ();
}
#else
int main ()
{
    return 0;
}
#endif
