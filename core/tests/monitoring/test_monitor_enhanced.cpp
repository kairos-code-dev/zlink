/* SPDX-License-Identifier: MPL-2.0 */

#include "testutil.hpp"
#include "testutil_monitoring.hpp"
#include "testutil_unity.hpp"

#include <string.h>

SETUP_TEARDOWN_TESTCONTEXT

static void assert_auto_routing_id (void *socket_)
{
    uint8_t buf[255];
    size_t size = sizeof (buf);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_getsockopt (socket_, ZLINK_ROUTING_ID, buf, &size));
    TEST_ASSERT_EQUAL_UINT (16, size);
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
            while (recv_monitor_event_from_socket (monitor_, &ev,
                                                   ZLINK_DONTWAIT)
                   == 0) {
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

static void subscribe_all_if_needed (void *socket_, int type_)
{
    if (type_ != ZLINK_SUB)
        return;
    const char *all_topics = "";
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_setsockopt (socket_, ZLINK_SUBSCRIBE, all_topics, 0));
}

struct monitor_sequence_probe_t
{
    monitor_sequence_probe_t () :
        accepted_seen (false),
        ready_seen (false),
        disconnected_seen (false),
        ready_before_accepted (false),
        disconnected_before_ready (false)
    {
        memset (&accepted, 0, sizeof (accepted));
        memset (&ready, 0, sizeof (ready));
        memset (&disconnected, 0, sizeof (disconnected));
    }

    bool accepted_seen;
    bool ready_seen;
    bool disconnected_seen;
    bool ready_before_accepted;
    bool disconnected_before_ready;
    zlink_monitor_event_t accepted;
    zlink_monitor_event_t ready;
    zlink_monitor_event_t disconnected;
};

static bool routing_id_equal (const zlink_routing_id_t *lhs_,
                              const zlink_routing_id_t *rhs_)
{
    if (!lhs_ || !rhs_)
        return false;
    if (lhs_->size != rhs_->size)
        return false;
    if (lhs_->size == 0)
        return true;
    return memcmp (lhs_->data, rhs_->data, lhs_->size) == 0;
}

static void collect_sequence_events (void *monitor_,
                                     monitor_sequence_probe_t *probe_,
                                     int poll_timeout_ms_)
{
    if (!monitor_ || !probe_)
        return;

    zlink_pollitem_t items[] = {{monitor_, 0, ZLINK_POLLIN, 0}};
    const int rc = zlink_poll (items, 1, poll_timeout_ms_);
    if (rc <= 0 || (items[0].revents & ZLINK_POLLIN) == 0)
        return;

    for (;;) {
        zlink_monitor_event_t ev;
        if (recv_monitor_event_from_socket (monitor_, &ev, ZLINK_DONTWAIT) != 0)
            break;

        if (ev.event == ZLINK_EVENT_ACCEPTED) {
            if (!probe_->accepted_seen) {
                probe_->accepted = ev;
                probe_->accepted_seen = true;
            }
            continue;
        }

        if (ev.event == ZLINK_EVENT_CONNECTION_READY) {
            if (!probe_->accepted_seen)
                probe_->ready_before_accepted = true;
            if (!probe_->ready_seen) {
                probe_->ready = ev;
                probe_->ready_seen = true;
            }
            continue;
        }

        if (ev.event == ZLINK_EVENT_DISCONNECTED) {
            if (!probe_->ready_seen)
                probe_->disconnected_before_ready = true;
            if (!probe_->disconnected_seen) {
                probe_->disconnected = ev;
                probe_->disconnected_seen = true;
            }
        }
    }
}

static bool wait_for_sequence (void *monitor_,
                               monitor_sequence_probe_t *probe_,
                               bool require_disconnected_,
                               int timeout_ms_)
{
    const int slice_ms = 20;
    const int loops = timeout_ms_ > 0 ? timeout_ms_ / slice_ms + 1 : 1;

    for (int i = 0; i < loops; ++i) {
        collect_sequence_events (monitor_, probe_, slice_ms);
        if (probe_->accepted_seen && probe_->ready_seen
            && (!require_disconnected_ || probe_->disconnected_seen))
            return true;
    }

    collect_sequence_events (monitor_, probe_, 0);
    return probe_->accepted_seen && probe_->ready_seen
           && (!require_disconnected_ || probe_->disconnected_seen);
}

static void run_client_monitor_ready_disconnected_test (int client_type_,
                                                        int server_type_)
{
    void *server = test_context_socket (server_type_);
    void *client = test_context_socket (client_type_);
    TEST_ASSERT_NOT_NULL (server);
    TEST_ASSERT_NOT_NULL (client);

    subscribe_all_if_needed (server, server_type_);
    subscribe_all_if_needed (client, client_type_);

    const int zero = 0;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_setsockopt (server, ZLINK_LINGER, &zero, sizeof (zero)));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_setsockopt (client, ZLINK_LINGER, &zero, sizeof (zero)));

    char endpoint[MAX_SOCKET_STRING];
    bind_loopback_ipv4 (server, endpoint, sizeof endpoint);

    void *mon = zlink_socket_monitor_open (
      client, ZLINK_EVENT_CONNECTION_READY | ZLINK_EVENT_DISCONNECTED);
    TEST_ASSERT_NOT_NULL (mon);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_setsockopt (mon, ZLINK_LINGER, &zero, sizeof (zero)));

    TEST_ASSERT_SUCCESS_ERRNO (zlink_connect (client, endpoint));

    zlink_monitor_event_t ready;
    TEST_ASSERT_TRUE (wait_for_event (mon, ZLINK_EVENT_CONNECTION_READY, &ready));
    TEST_ASSERT_TRUE (ready.remote_addr[0] != '\0'
                      || ready.local_addr[0] != '\0');

    test_context_socket_close_zero_linger (server);

    zlink_monitor_event_t disconnected;
    TEST_ASSERT_TRUE (
      wait_for_event (mon, ZLINK_EVENT_DISCONNECTED, &disconnected));
    TEST_ASSERT_TRUE (disconnected.remote_addr[0] != '\0'
                      || disconnected.local_addr[0] != '\0');
    if (ready.routing_id.size > 0 && disconnected.routing_id.size > 0) {
        TEST_ASSERT_TRUE (
          routing_id_equal (&ready.routing_id, &disconnected.routing_id));
    }

    zlink_socket_monitor (client, NULL, 0);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_close (mon));
    test_context_socket_close_zero_linger (client);
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

void test_router_monitor_event_sequence_timing ()
{
    void *server = test_context_socket (ZLINK_ROUTER);
    void *client = test_context_socket (ZLINK_DEALER);
    TEST_ASSERT_NOT_NULL (server);
    TEST_ASSERT_NOT_NULL (client);

    const int zero = 0;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_setsockopt (server, ZLINK_LINGER, &zero, sizeof (zero)));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_setsockopt (client, ZLINK_LINGER, &zero, sizeof (zero)));

    char endpoint[MAX_SOCKET_STRING];
    bind_loopback_ipv4 (server, endpoint, sizeof endpoint);

    void *mon = zlink_socket_monitor_open (
      server, ZLINK_EVENT_ACCEPTED | ZLINK_EVENT_CONNECTION_READY
                | ZLINK_EVENT_DISCONNECTED);
    TEST_ASSERT_NOT_NULL (mon);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_setsockopt (mon, ZLINK_LINGER, &zero, sizeof (zero)));

    TEST_ASSERT_SUCCESS_ERRNO (zlink_connect (client, endpoint));

    monitor_sequence_probe_t probe;
    TEST_ASSERT_TRUE (wait_for_sequence (mon, &probe, false, 5000));
    TEST_ASSERT_FALSE (probe.ready_before_accepted);
    TEST_ASSERT_TRUE (probe.accepted_seen);
    TEST_ASSERT_EQUAL_UINT (0, probe.accepted.routing_id.size);
    TEST_ASSERT_TRUE (probe.ready_seen);
    TEST_ASSERT_TRUE (
      probe.ready.routing_id.size > 0);

    test_context_socket_close_zero_linger (client);

    TEST_ASSERT_TRUE (wait_for_sequence (mon, &probe, true, 5000));
    TEST_ASSERT_FALSE (probe.disconnected_before_ready);
    if (probe.disconnected.routing_id.size > 0) {
        TEST_ASSERT_TRUE (
          routing_id_equal (&probe.ready.routing_id,
                            &probe.disconnected.routing_id));
    }

    zlink_socket_monitor (server, NULL, 0);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_close (mon));
    test_context_socket_close_zero_linger (server);
}

void test_dealer_monitor_ready_and_disconnected ()
{
    run_client_monitor_ready_disconnected_test (ZLINK_DEALER, ZLINK_ROUTER);
}

void test_pub_monitor_ready_and_disconnected ()
{
    run_client_monitor_ready_disconnected_test (ZLINK_PUB, ZLINK_SUB);
}

void test_sub_monitor_ready_and_disconnected ()
{
    run_client_monitor_ready_disconnected_test (ZLINK_SUB, ZLINK_PUB);
}

int main ()
{
    setup_test_environment ();

    UNITY_BEGIN ();
    RUN_TEST (test_auto_routing_id_generation);
    RUN_TEST (test_monitor_open_and_connection_ready);
    RUN_TEST (test_peer_enumeration);
    RUN_TEST (test_router_monitor_event_sequence_timing);
    RUN_TEST (test_dealer_monitor_ready_and_disconnected);
    RUN_TEST (test_pub_monitor_ready_and_disconnected);
    RUN_TEST (test_sub_monitor_ready_and_disconnected);
    return UNITY_END ();
}
