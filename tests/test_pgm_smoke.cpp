/* SPDX-License-Identifier: MPL-2.0 */

#include "testutil.hpp"
#include "testutil_unity.hpp"

#include <stdlib.h>
#include <string.h>

void setUp ()
{
}

void tearDown ()
{
}

static bool has_prefix (const char *str_, const char *prefix_)
{
    return str_ && prefix_ && strncmp (str_, prefix_, strlen (prefix_)) == 0;
}

static void test_pgm_smoke_pub_sub ()
{
#if defined(ZMQ_HAVE_OPENPGM)
    const char *endpoint = getenv ("ZMQ_PGM_SMOKE_ENDPOINT");
    if (!endpoint || endpoint[0] == '\0') {
        TEST_IGNORE_MESSAGE ("ZMQ_PGM_SMOKE_ENDPOINT not set");
    }

    if (!has_prefix (endpoint, "pgm://")
        && !has_prefix (endpoint, "epgm://")) {
        TEST_FAIL_MESSAGE (
          "ZMQ_PGM_SMOKE_ENDPOINT must start with pgm:// or epgm://");
    }

    void *ctx = zmq_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *pub = zmq_socket (ctx, ZMQ_PUB);
    TEST_ASSERT_NOT_NULL (pub);
    void *sub = zmq_socket (ctx, ZMQ_SUB);
    TEST_ASSERT_NOT_NULL (sub);

    const int hwm = 10;
    TEST_ASSERT_SUCCESS_ERRNO (
      zmq_setsockopt (pub, ZMQ_SNDHWM, &hwm, sizeof (hwm)));
    TEST_ASSERT_SUCCESS_ERRNO (
      zmq_setsockopt (sub, ZMQ_RCVHWM, &hwm, sizeof (hwm)));
    TEST_ASSERT_SUCCESS_ERRNO (
      zmq_setsockopt (sub, ZMQ_SUBSCRIBE, "", 0));

    int rc = zmq_bind (pub, endpoint);
    if (rc != 0) {
        const int err = zmq_errno ();
        close_zero_linger (pub);
        close_zero_linger (sub);
        TEST_ASSERT_SUCCESS_ERRNO (zmq_ctx_term (ctx));
        TEST_FAIL_MESSAGE (zmq_strerror (err));
    }

    rc = zmq_connect (sub, endpoint);
    if (rc != 0) {
        const int err = zmq_errno ();
        close_zero_linger (pub);
        close_zero_linger (sub);
        TEST_ASSERT_SUCCESS_ERRNO (zmq_ctx_term (ctx));
        TEST_FAIL_MESSAGE (zmq_strerror (err));
    }

    msleep (SETTLE_TIME * 2);

    const char *payload = "PGM_SMOKE";
    const size_t payload_size = strlen (payload);
    bool received = false;

    for (int attempt = 0; attempt < 20 && !received; ++attempt) {
        rc = zmq_send (pub, payload, payload_size, 0);
        if (rc < 0 && zmq_errno () != EAGAIN) {
            close_zero_linger (pub);
            close_zero_linger (sub);
            TEST_ASSERT_SUCCESS_ERRNO (zmq_ctx_term (ctx));
            TEST_FAIL_MESSAGE ("pgm send failed");
        }

        zmq_pollitem_t items[] = {{sub, 0, ZMQ_POLLIN, 0}};
        rc = zmq_poll (items, 1, 200);
        if (rc < 0) {
            close_zero_linger (pub);
            close_zero_linger (sub);
            TEST_ASSERT_SUCCESS_ERRNO (zmq_ctx_term (ctx));
            TEST_FAIL_MESSAGE ("zmq_poll failed");
        }

        if (items[0].revents & ZMQ_POLLIN) {
            char buffer[32];
            rc = zmq_recv (sub, buffer, sizeof (buffer), 0);
            if (rc >= 0) {
                TEST_ASSERT_EQUAL_INT ((int) payload_size, rc);
                TEST_ASSERT_EQUAL_INT (
                  0, memcmp (buffer, payload, payload_size));
                received = true;
            }
        }

        if (!received)
            msleep (50);
    }

    TEST_ASSERT_TRUE_MESSAGE (received, "pgm message not received");

    close_zero_linger (pub);
    close_zero_linger (sub);
    TEST_ASSERT_SUCCESS_ERRNO (zmq_ctx_term (ctx));
#else
    TEST_IGNORE_MESSAGE ("OpenPGM not enabled");
#endif
}

int main ()
{
    setup_test_environment ();

    UNITY_BEGIN ();
    RUN_TEST (test_pgm_smoke_pub_sub);
    return UNITY_END ();
}
