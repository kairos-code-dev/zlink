/* SPDX-License-Identifier: MPL-2.0 */

#include "testutil.hpp"
#include "testutil_unity.hpp"

void setUp ()
{
}

void tearDown ()
{
}

void test_capabilities ()
{
#if defined(ZMQ_HAVE_IPC)
    TEST_ASSERT_TRUE (zmq_has ("ipc"));
#else
    TEST_ASSERT_TRUE (!zmq_has ("ipc"));
#endif

#if defined(ZMQ_HAVE_TLS)
    TEST_ASSERT_TRUE (zmq_has ("tls"));
#else
    TEST_ASSERT_TRUE (!zmq_has ("tls"));
#endif

#if defined(ZMQ_HAVE_WS)
    TEST_ASSERT_TRUE (zmq_has ("ws"));
#else
    TEST_ASSERT_TRUE (!zmq_has ("ws"));
#endif

#if defined(ZMQ_HAVE_WSS)
    TEST_ASSERT_TRUE (zmq_has ("wss"));
#else
    TEST_ASSERT_TRUE (!zmq_has ("wss"));
#endif

    // PGM is removed
    TEST_ASSERT_TRUE (!zmq_has ("pgm"));

    // TIPC is removed
    TEST_ASSERT_TRUE (!zmq_has ("tipc"));

    // NORM is removed
    TEST_ASSERT_TRUE (!zmq_has ("norm"));

    // CURVE is removed
    TEST_ASSERT_TRUE (!zmq_has ("curve"));

    // GSSAPI is removed
    TEST_ASSERT_TRUE (!zmq_has ("gssapi"));

    // VMCI is removed
    TEST_ASSERT_TRUE (!zmq_has ("vmci"));

    // Draft API is removed
    TEST_ASSERT_TRUE (!zmq_has ("draft"));
}

int main ()
{
    setup_test_environment ();

    UNITY_BEGIN ();
    RUN_TEST (test_capabilities);
    return UNITY_END ();
}
