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
#if defined(ZLINK_HAVE_OPENPGM)
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *router = zlink_socket (ctx, ZLINK_ROUTER);
    TEST_ASSERT_NOT_NULL (router);

    int rc = zlink_connect (router, "pgm://invalid");
    TEST_ASSERT_EQUAL_INT (-1, rc);
#ifdef ENOCOMPATPROTO
    TEST_ASSERT_EQUAL_INT (ENOCOMPATPROTO, zlink_errno ());
#else
    TEST_ASSERT_TRUE (zlink_errno () != 0);
#endif

    rc = zlink_connect (router, "epgm://invalid");
    TEST_ASSERT_EQUAL_INT (-1, rc);
#ifdef ENOCOMPATPROTO
    TEST_ASSERT_EQUAL_INT (ENOCOMPATPROTO, zlink_errno ());
#else
    TEST_ASSERT_TRUE (zlink_errno () != 0);
#endif

    TEST_ASSERT_SUCCESS_ERRNO (zlink_close (router));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
#else
    TEST_IGNORE_MESSAGE ("OpenPGM not enabled");
#endif
}

static void test_pgm_invalid_address ()
{
#if defined(ZLINK_HAVE_OPENPGM)
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *pub = zlink_socket (ctx, ZLINK_PUB);
    TEST_ASSERT_NOT_NULL (pub);

    int rc = zlink_connect (pub, "pgm://invalid");
    TEST_ASSERT_EQUAL_INT (-1, rc);
    TEST_ASSERT_EQUAL_INT (EINVAL, zlink_errno ());

    rc = zlink_connect (pub, "epgm://invalid");
    TEST_ASSERT_EQUAL_INT (-1, rc);
    TEST_ASSERT_EQUAL_INT (EINVAL, zlink_errno ());

    TEST_ASSERT_SUCCESS_ERRNO (zlink_close (pub));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
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
