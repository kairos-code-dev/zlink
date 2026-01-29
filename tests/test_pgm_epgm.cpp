/* SPDX-License-Identifier: MPL-2.0 */

#include "testutil.hpp"
#include "testutil_unity.hpp"

void setUp ()
{
}

void tearDown ()
{
}

static void test_pgm_requires_pubsub ()
{
#if defined(ZMQ_HAVE_OPENPGM)
    void *ctx = zmq_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *router = zmq_socket (ctx, ZMQ_ROUTER);
    TEST_ASSERT_NOT_NULL (router);

    int rc = zmq_connect (router, "pgm://invalid");
    TEST_ASSERT_EQUAL_INT (-1, rc);
#ifdef ENOCOMPATPROTO
    TEST_ASSERT_EQUAL_INT (ENOCOMPATPROTO, zmq_errno ());
#else
    TEST_ASSERT_TRUE (zmq_errno () != 0);
#endif

    rc = zmq_connect (router, "epgm://invalid");
    TEST_ASSERT_EQUAL_INT (-1, rc);
#ifdef ENOCOMPATPROTO
    TEST_ASSERT_EQUAL_INT (ENOCOMPATPROTO, zmq_errno ());
#else
    TEST_ASSERT_TRUE (zmq_errno () != 0);
#endif

    TEST_ASSERT_SUCCESS_ERRNO (zmq_close (router));
    TEST_ASSERT_SUCCESS_ERRNO (zmq_ctx_term (ctx));
#else
    TEST_IGNORE_MESSAGE ("OpenPGM not enabled");
#endif
}

static void test_pgm_invalid_address ()
{
#if defined(ZMQ_HAVE_OPENPGM)
    void *ctx = zmq_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *pub = zmq_socket (ctx, ZMQ_PUB);
    TEST_ASSERT_NOT_NULL (pub);

    int rc = zmq_connect (pub, "pgm://invalid");
    TEST_ASSERT_EQUAL_INT (-1, rc);
    TEST_ASSERT_EQUAL_INT (EINVAL, zmq_errno ());

    rc = zmq_connect (pub, "epgm://invalid");
    TEST_ASSERT_EQUAL_INT (-1, rc);
    TEST_ASSERT_EQUAL_INT (EINVAL, zmq_errno ());

    TEST_ASSERT_SUCCESS_ERRNO (zmq_close (pub));
    TEST_ASSERT_SUCCESS_ERRNO (zmq_ctx_term (ctx));
#else
    TEST_IGNORE_MESSAGE ("OpenPGM not enabled");
#endif
}

int main ()
{
    setup_test_environment ();

    UNITY_BEGIN ();
    RUN_TEST (test_pgm_requires_pubsub);
    RUN_TEST (test_pgm_invalid_address);
    return UNITY_END ();
}
