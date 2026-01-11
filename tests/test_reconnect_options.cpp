/* SPDX-License-Identifier: MPL-2.0 */
#include <assert.h>

#include "testutil.hpp"
#include "testutil_unity.hpp"
#include "testutil_monitoring.hpp"

#include <unity.h>

// test behavior with (mostly) default values
void reconnect_default ()
{
    // setup pub socket
    void *pub = test_context_socket (ZMQ_PUB);
    //  Bind pub socket
    TEST_ASSERT_SUCCESS_ERRNO (zmq_bind (pub, ENDPOINT_0));

    // setup sub socket
    void *sub = test_context_socket (ZMQ_SUB);
    //  Monitor all events on sub
    TEST_ASSERT_SUCCESS_ERRNO (
      zmq_socket_monitor (sub, "inproc://monitor-sub", ZMQ_EVENT_ALL));
    //  Create socket for collecting monitor events
    void *sub_mon = test_context_socket (ZMQ_PAIR);
    //  Connect so they'll get events
    TEST_ASSERT_SUCCESS_ERRNO (zmq_connect (sub_mon, "inproc://monitor-sub"));
    // set reconnect interval so only a single reconnect is tried
    int interval = 60 * 1000;
    TEST_ASSERT_SUCCESS_ERRNO (
      zmq_setsockopt (sub, ZMQ_RECONNECT_IVL, &interval, sizeof (interval)));
    // connect to pub
    TEST_ASSERT_SUCCESS_ERRNO (zmq_connect (sub, ENDPOINT_0));

    //  confirm that we get following events
    expect_monitor_event (sub_mon, ZMQ_EVENT_CONNECT_DELAYED);
    expect_monitor_event (sub_mon, ZMQ_EVENT_CONNECTED);
    expect_monitor_event (sub_mon, ZMQ_EVENT_HANDSHAKE_SUCCEEDED);

    // close the pub socket
    test_context_socket_close_zero_linger (pub);

    //  confirm that we get following events
    expect_monitor_event (sub_mon, ZMQ_EVENT_DISCONNECTED);
    expect_monitor_event (sub_mon, ZMQ_EVENT_CONNECT_RETRIED);

    // ZMQ_EVENT_CONNECT_RETRIED should be last event, because of timeout set above
    int event;
    char *event_address;
    int rc = get_monitor_event_with_timeout (sub_mon, &event, &event_address,
                                             2 * 1000);
    assert (rc == -1);
    LIBZMQ_UNUSED (rc);

    //  Close sub
    //  TODO why does this use zero_linger?
    test_context_socket_close_zero_linger (sub);

    //  Close monitor
    //  TODO why does this use zero_linger?
    test_context_socket_close_zero_linger (sub_mon);
}


// test successful reconnect
void reconnect_success ()
{
    // setup pub socket
    void *pub = test_context_socket (ZMQ_PUB);
    //  Bind pub socket
    TEST_ASSERT_SUCCESS_ERRNO (zmq_bind (pub, ENDPOINT_0));

    // setup sub socket
    void *sub = test_context_socket (ZMQ_SUB);
    //  Monitor all events on sub
    TEST_ASSERT_SUCCESS_ERRNO (
      zmq_socket_monitor (sub, "inproc://monitor-sub", ZMQ_EVENT_ALL));
    //  Create socket for collecting monitor events
    void *sub_mon = test_context_socket (ZMQ_PAIR);
    //  Connect so they'll get events
    TEST_ASSERT_SUCCESS_ERRNO (zmq_connect (sub_mon, "inproc://monitor-sub"));
    // set reconnect interval so only a single reconnect is tried
    int interval = 1 * 1000;
    TEST_ASSERT_SUCCESS_ERRNO (
      zmq_setsockopt (sub, ZMQ_RECONNECT_IVL, &interval, sizeof (interval)));
    // connect to pub
    TEST_ASSERT_SUCCESS_ERRNO (zmq_connect (sub, ENDPOINT_0));

    //  confirm that we get following events
    expect_monitor_event (sub_mon, ZMQ_EVENT_CONNECT_DELAYED);
    expect_monitor_event (sub_mon, ZMQ_EVENT_CONNECTED);
    expect_monitor_event (sub_mon, ZMQ_EVENT_HANDSHAKE_SUCCEEDED);

    // close the pub socket
    test_context_socket_close_zero_linger (pub);

    //  confirm that we get following events
    expect_monitor_event (sub_mon, ZMQ_EVENT_DISCONNECTED);
    expect_monitor_event (sub_mon, ZMQ_EVENT_CONNECT_RETRIED);

    // ZMQ_EVENT_CONNECT_RETRIED should be last event, because of timeout set above
    int event;
    char *event_address;
    int rc = get_monitor_event_with_timeout (sub_mon, &event, &event_address,
                                             SETTLE_TIME);
    assert (rc == -1);
    LIBZMQ_UNUSED (rc);

    //  Now re-bind pub socket and wait for re-connect
    pub = test_context_socket (ZMQ_PUB);
    TEST_ASSERT_SUCCESS_ERRNO (zmq_bind (pub, ENDPOINT_0));
    msleep (SETTLE_TIME);

    //  confirm that we get following events
    expect_monitor_event (sub_mon, ZMQ_EVENT_CONNECT_DELAYED);
    expect_monitor_event (sub_mon, ZMQ_EVENT_CONNECTED);
    expect_monitor_event (sub_mon, ZMQ_EVENT_HANDSHAKE_SUCCEEDED);

    // ZMQ_EVENT_HANDSHAKE_SUCCEEDED should be last event
    rc = get_monitor_event_with_timeout (sub_mon, &event, &event_address,
                                         SETTLE_TIME);
    assert (rc == -1);

    //  Close sub
    //  TODO why does this use zero_linger?
    test_context_socket_close_zero_linger (sub);
    test_context_socket_close_zero_linger (pub);

    //  Close monitor
    //  TODO why does this use zero_linger?
    test_context_socket_close_zero_linger (sub_mon);
}

void setUp ()
{
    setup_test_context ();
}

void tearDown ()
{
    teardown_test_context ();
}

int main (void)
{
    setup_test_environment ();

    UNITY_BEGIN ();

    RUN_TEST (reconnect_default);
    RUN_TEST (reconnect_success);
    return UNITY_END ();
}
