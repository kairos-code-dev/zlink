/* SPDX-License-Identifier: MPL-2.0 */

#include "../testutil.hpp"
#include "../testutil_unity.hpp"

#include <atomic>
#include <errno.h>
#include <thread>
#include <vector>
#include <string.h>

static void test_is_threadsafe_flag ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *raw = zlink_socket (ctx, ZLINK_PAIR);
    TEST_ASSERT_NOT_NULL (raw);

    void *ts = zlink_socket_threadsafe (ctx, ZLINK_PAIR);
    TEST_ASSERT_NOT_NULL (ts);

    TEST_ASSERT_EQUAL_INT (0, zlink_is_threadsafe (raw));
    TEST_ASSERT_EQUAL_INT (1, zlink_is_threadsafe (ts));

    int value = 0;
    size_t value_size = sizeof (value);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_getsockopt (raw, ZLINK_THREAD_SAFE, &value, &value_size));
    TEST_ASSERT_EQUAL_INT (0, value);

    value = 0;
    value_size = sizeof (value);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_getsockopt (ts, ZLINK_THREAD_SAFE, &value, &value_size));
    TEST_ASSERT_EQUAL_INT (1, value);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_close (raw));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_close (ts));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

static void test_concurrent_send ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *receiver = zlink_socket (ctx, ZLINK_PAIR);
    TEST_ASSERT_NOT_NULL (receiver);
    void *sender = zlink_socket_threadsafe (ctx, ZLINK_PAIR);
    TEST_ASSERT_NOT_NULL (sender);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_bind (receiver, "inproc://ts-send"));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_connect (sender, "inproc://ts-send"));
    msleep (SETTLE_TIME);

    const int thread_count = 4;
    const int msgs_per_thread = 200;
    const int total_msgs = thread_count * msgs_per_thread;

    std::atomic<int> send_errors (0);
    std::vector<std::thread> workers;
    workers.reserve (thread_count);

    for (int i = 0; i < thread_count; ++i) {
        workers.push_back (std::thread ([sender, msgs_per_thread,
                                         &send_errors] () {
            for (int j = 0; j < msgs_per_thread; ++j) {
                if (zlink_send (sender, "A", 1, 0) != 1)
                    ++send_errors;
            }
        }));
    }

    int received = 0;
    char buf[4];
    for (int i = 0; i < total_msgs; ++i) {
        const int rc = zlink_recv (receiver, buf, sizeof (buf), 0);
        if (rc == 1)
            ++received;
    }

    for (size_t i = 0; i < workers.size (); ++i)
        workers[i].join ();

    TEST_ASSERT_EQUAL_INT (0, send_errors.load ());
    TEST_ASSERT_EQUAL_INT (total_msgs, received);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_close (sender));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_close (receiver));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

static void test_concurrent_recv ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *sender = zlink_socket (ctx, ZLINK_PAIR);
    TEST_ASSERT_NOT_NULL (sender);
    void *receiver = zlink_socket_threadsafe (ctx, ZLINK_PAIR);
    TEST_ASSERT_NOT_NULL (receiver);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_bind (sender, "inproc://ts-recv"));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_connect (receiver, "inproc://ts-recv"));
    msleep (SETTLE_TIME);

    const int thread_count = 4;
    const int total_msgs = 400;

    std::atomic<int> received (0);
    std::atomic<int> recv_errors (0);
    std::vector<std::thread> workers;
    workers.reserve (thread_count);

    for (int i = 0; i < thread_count; ++i) {
        workers.push_back (std::thread ([receiver, &received, &recv_errors] () {
            char buf[16];
            while (true) {
                const int rc = zlink_recv (receiver, buf, sizeof (buf), 0);
                if (rc < 0) {
                    ++recv_errors;
                    continue;
                }
                if (rc == 3 && memcmp (buf, "END", 3) == 0)
                    break;
                ++received;
            }
        }));
    }

    for (int i = 0; i < total_msgs; ++i) {
        TEST_ASSERT_EQUAL_INT (1, zlink_send (sender, "B", 1, 0));
    }
    for (int i = 0; i < thread_count; ++i) {
        TEST_ASSERT_EQUAL_INT (3, zlink_send (sender, "END", 3, 0));
    }

    for (size_t i = 0; i < workers.size (); ++i)
        workers[i].join ();

    TEST_ASSERT_EQUAL_INT (0, recv_errors.load ());
    TEST_ASSERT_EQUAL_INT (total_msgs, received.load ());

    TEST_ASSERT_SUCCESS_ERRNO (zlink_close (receiver));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_close (sender));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

static void test_concurrent_close ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *receiver = zlink_socket (ctx, ZLINK_PAIR);
    TEST_ASSERT_NOT_NULL (receiver);
    void *sender = zlink_socket_threadsafe (ctx, ZLINK_PAIR);
    TEST_ASSERT_NOT_NULL (sender);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_bind (receiver, "inproc://ts-close"));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_connect (sender, "inproc://ts-close"));
    msleep (SETTLE_TIME);

    const int thread_count = 4;
    std::atomic<bool> stop (false);
    std::vector<std::thread> workers;
    workers.reserve (thread_count);

    for (int i = 0; i < thread_count; ++i) {
        workers.push_back (std::thread ([sender, &stop] () {
            while (!stop.load ()) {
                const int rc = zlink_send (sender, "C", 1, ZLINK_DONTWAIT);
                if (rc == -1) {
                    const int err = errno;
                    if (err == ENOTSOCK || err == ETERM)
                        break;
                }
            }
        }));
    }

    msleep (50);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_close (sender));
    stop.store (true);

    for (size_t i = 0; i < workers.size (); ++i)
        workers[i].join ();

    errno = 0;
    TEST_ASSERT_EQUAL_INT (-1, zlink_send (sender, "C", 1, ZLINK_DONTWAIT));
    TEST_ASSERT_TRUE (errno == ENOTSOCK || errno == ETERM);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_close (receiver));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

int main (void)
{
    setup_test_environment ();

    UNITY_BEGIN ();
    RUN_TEST (test_is_threadsafe_flag);
    RUN_TEST (test_concurrent_send);
    RUN_TEST (test_concurrent_recv);
    RUN_TEST (test_concurrent_close);
    return UNITY_END ();
}
