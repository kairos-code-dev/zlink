/* SPDX-License-Identifier: MPL-2.0 */

#include "testutil.hpp"
#include "testutil_unity.hpp"

#include <vector>

SETUP_TEARDOWN_TESTCONTEXT

static void assert_auto_routing_id (void *socket_)
{
    uint8_t buf[255];
    size_t size = sizeof (buf);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_getsockopt (socket_, ZLINK_ROUTING_ID, buf, &size));
    TEST_ASSERT_EQUAL_UINT (5, size);
    TEST_ASSERT_EQUAL_UINT8 (0, buf[0]);
}

static bool wait_for_event (void *monitor_,
                            uint64_t expected_event_,
                            zlink_monitor_event_t *out_)
{
    for (int attempt = 0; attempt < 50; ++attempt) {
        zlink_pollitem_t items[] = {{monitor_, 0, ZLINK_POLLIN, 0}};
        const int rc = zlink_poll (items, 1, 200);
        if (rc > 0 && (items[0].revents & ZLINK_POLLIN)) {
            zlink_monitor_event_t ev;
            while (zlink_monitor_recv (monitor_, &ev, ZLINK_DONTWAIT) == 0) {
                if (ev.event == expected_event_) {
                    if (out_)
                        *out_ = ev;
                    return true;
                }
            }
        }
    }
    return false;
}

void test_auto_routing_id_generation ()
{
    const int types[] = {ZLINK_PAIR,   ZLINK_PUB,   ZLINK_SUB, ZLINK_DEALER,
                         ZLINK_ROUTER, ZLINK_XPUB, ZLINK_XSUB, ZLINK_STREAM};

    for (size_t i = 0; i < sizeof (types) / sizeof (types[0]); ++i) {
        void *sock = test_context_socket (types[i]);
        TEST_ASSERT_NOT_NULL (sock);
        assert_auto_routing_id (sock);
        test_context_socket_close (sock);
    }
}

void test_monitor_open_and_connection_ready ()
{
    void *server = test_context_socket (ZLINK_ROUTER);
    void *client = test_context_socket (ZLINK_DEALER);
    TEST_ASSERT_NOT_NULL (server);
    TEST_ASSERT_NOT_NULL (client);

    char endpoint[MAX_SOCKET_STRING];
    bind_loopback_ipv4 (server, endpoint, sizeof endpoint);

    void *mon = zlink_socket_monitor_open (server,
                                         ZLINK_EVENT_CONNECTION_READY
                                           | ZLINK_EVENT_DISCONNECTED);
    TEST_ASSERT_NOT_NULL (mon);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_connect (client, endpoint));

    zlink_monitor_event_t ev;
    TEST_ASSERT_TRUE (
      wait_for_event (mon, ZLINK_EVENT_CONNECTION_READY, &ev));
    TEST_ASSERT_TRUE (ev.routing_id.size > 0);
    TEST_ASSERT_TRUE (ev.remote_addr[0] != '\0'
                      || ev.local_addr[0] != '\0');

    test_context_socket_close_zero_linger (client);

    TEST_ASSERT_TRUE (wait_for_event (mon, ZLINK_EVENT_DISCONNECTED, NULL));

    zlink_socket_monitor (server, NULL, 0);
    int linger = 0;
    zlink_setsockopt (mon, ZLINK_LINGER, &linger, sizeof (linger));
    zlink_close (mon);
    test_context_socket_close_zero_linger (server);
}

void test_peer_enumeration ()
{
    void *server = test_context_socket (ZLINK_ROUTER);
    void *client = test_context_socket (ZLINK_DEALER);
    TEST_ASSERT_NOT_NULL (server);
    TEST_ASSERT_NOT_NULL (client);

    char endpoint[MAX_SOCKET_STRING];
    bind_loopback_ipv4 (server, endpoint, sizeof endpoint);

    void *mon = zlink_socket_monitor_open (server, ZLINK_EVENT_CONNECTION_READY);
    TEST_ASSERT_NOT_NULL (mon);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_connect (client, endpoint));
    TEST_ASSERT_TRUE (wait_for_event (mon, ZLINK_EVENT_CONNECTION_READY, NULL));

    const int peer_count = zlink_socket_peer_count (server);
    TEST_ASSERT_TRUE (peer_count >= 1);

    zlink_routing_id_t peer_id;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_socket_peer_routing_id (server, 0, &peer_id));
    TEST_ASSERT_TRUE (peer_id.size > 0);

    const char payload[] = "ping";
    send_string_expect_success (client, payload, 0);

    unsigned char rid_buf[255];
    int rid_size = zlink_recv (server, rid_buf, sizeof (rid_buf), 0);
    TEST_ASSERT_TRUE (rid_size > 0);
    recv_string_expect_success (server, payload, 0);

    TEST_ASSERT_EQUAL_INT (peer_id.size, rid_size);

    zlink_peer_info_t info;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_socket_peer_info (server, &peer_id, &info));
    TEST_ASSERT_TRUE (info.routing_id.size > 0);

    TEST_ASSERT_EQUAL_INT (
      peer_id.size,
      TEST_ASSERT_SUCCESS_ERRNO (
        zlink_send (server, peer_id.data, peer_id.size, ZLINK_SNDMORE)));
    send_string_expect_success (server, payload, 0);
    recv_string_expect_success (client, payload, 0);

    zlink_socket_monitor (server, NULL, 0);
    int linger = 0;
    zlink_setsockopt (mon, ZLINK_LINGER, &linger, sizeof (linger));
    zlink_close (mon);
    test_context_socket_close_zero_linger (client);
    test_context_socket_close_zero_linger (server);

    LIBZLINK_UNUSED (rid_buf);
}

int main ()
{
    setup_test_environment ();

    UNITY_BEGIN ();
    RUN_TEST (test_auto_routing_id_generation);
    RUN_TEST (test_monitor_open_and_connection_ready);
    RUN_TEST (test_peer_enumeration);
    return UNITY_END ();
}
