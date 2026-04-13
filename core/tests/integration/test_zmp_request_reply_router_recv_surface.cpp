/* SPDX-License-Identifier: MPL-2.0 */

#include "../testutil.hpp"
#include "../testutil_unity.hpp"

#include <string>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <future>
#include <mutex>

#include <unity.h>

SETUP_TEARDOWN_TESTCONTEXT

namespace
{
struct reply_probe_t
{
    std::mutex mutex;
    std::condition_variable cv;
    bool done;
    zlink_request_result_t result;
    size_t part_count;
    std::string payload;

    reply_probe_t () : done (false),
                       result (ZLINK_REQUEST_PROTOCOL_ERROR),
                       part_count (0)
    {
    }
};

std::string msg_to_string (const zlink_msg_t *msg_)
{
    return std::string (
      static_cast<const char *> (zlink_msg_data (
        const_cast<zlink_msg_t *> (msg_))),
      zlink_msg_size (const_cast<zlink_msg_t *> (msg_)));
}

void init_string_part (zlink_msg_t *part_, const char *text_)
{
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_init_size (part_, strlen (text_)));
    memcpy (zlink_msg_data (part_), text_, strlen (text_));
}

bool wait_for_reply (reply_probe_t *probe_)
{
    std::unique_lock<std::mutex> lock (probe_->mutex);
    return probe_->cv.wait_for (
      lock, std::chrono::milliseconds (SETTLE_TIME * 20),
      [probe_]() { return probe_->done; });
}

void capture_reply (zlink_request_result_t result_,
                    zlink_msg_t *parts_,
                    size_t part_count_,
                    void *userdata_)
{
    reply_probe_t *probe = static_cast<reply_probe_t *> (userdata_);
    if (!probe)
        return;

    {
        std::lock_guard<std::mutex> lock (probe->mutex);
        probe->done = true;
        probe->result = result_;
        probe->part_count = part_count_;
        probe->payload =
          part_count_ > 0 ? msg_to_string (&parts_[0]) : std::string ();
    }
    probe->cv.notify_all ();
}

void test_dealer_request_is_visible_through_router_recv ()
{
    void *router = test_context_socket (ZLINK_SOCKET_ROUTER);
    void *dealer = test_context_socket (ZLINK_SOCKET_DEALER);
    TEST_ASSERT_NOT_NULL (router);
    TEST_ASSERT_NOT_NULL (dealer);

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_routing_id (dealer, "dealer-recv", 11));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_bind (router, "inproc://zmp-router-recv-surface"));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_connect (dealer, "inproc://zmp-router-recv-surface"));
    msleep (SETTLE_TIME);

    {
        const zlink_routing_id_t *source_rid = NULL;
        const zlink_routing_id_t *source_spot_rid = NULL;
        uint64_t request_seq = 0;
        zlink_msg_t *parts = NULL;
        size_t part_count = 0;
        TEST_ASSERT_EQUAL_INT (
          ZLINK_RECV_NO_DATA,
          zlink_router_recv (router, &source_rid, &source_spot_rid,
                             &request_seq, &parts, &part_count,
                             ZLINK_DONTWAIT));
        TEST_ASSERT_EQUAL_INT (EAGAIN, zlink_errno ());
    }

    zlink_msg_t request_part;
    zlink_msg_init (&request_part);
    init_string_part (&request_part, "dealer-request-recv");

    std::future<void> router_done = std::async (
      std::launch::async, [router] () {
          const zlink_routing_id_t *source_rid = NULL;
          const zlink_routing_id_t *source_spot_rid = NULL;
          uint64_t request_seq = 0;
          zlink_msg_t *parts = NULL;
          size_t part_count = 0;
          TEST_ASSERT_SUCCESS_ERRNO (
            zlink_router_recv (
              router, &source_rid, &source_spot_rid, &request_seq, &parts,
              &part_count, 0));

          TEST_ASSERT_NOT_NULL (source_rid);
          TEST_ASSERT_NOT_NULL (source_spot_rid);
          TEST_ASSERT_EQUAL_UINT64 (0, source_spot_rid->size);
          TEST_ASSERT_TRUE (request_seq != 0);
          TEST_ASSERT_EQUAL_UINT64 (1, part_count);
          TEST_ASSERT_EQUAL_STRING_LEN (
            "dealer-recv", reinterpret_cast<const char *> (source_rid->data),
            source_rid->size);
          const std::string request_payload = msg_to_string (&parts[0]);
          TEST_ASSERT_EQUAL_STRING_LEN (
            "dealer-request-recv", request_payload.c_str (),
            request_payload.size ());
          zlink_multipart_close (parts, part_count);

          zlink_msg_t reply_part;
          zlink_msg_init (&reply_part);
          init_string_part (&reply_part, "router-reply-recv");
          TEST_ASSERT_SUCCESS_ERRNO (
            zlink_router_reply (router, source_rid, request_seq, &reply_part,
                                1));
      });

    reply_probe_t reply_probe;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_dealer_request (
      dealer, &request_part, 1, &capture_reply, &reply_probe, 0, 3000));
    TEST_ASSERT_TRUE (wait_for_reply (&reply_probe));
    router_done.get ();

    {
        std::lock_guard<std::mutex> lock (reply_probe.mutex);
        TEST_ASSERT_TRUE (reply_probe.done);
        TEST_ASSERT_EQUAL_INT (ZLINK_REQUEST_OK, reply_probe.result);
        TEST_ASSERT_EQUAL_UINT64 (1, reply_probe.part_count);
        TEST_ASSERT_EQUAL_STRING_LEN (
          "router-reply-recv", reply_probe.payload.c_str (),
          reply_probe.payload.size ());
    }

    test_context_socket_close_zero_linger (dealer);
    test_context_socket_close_zero_linger (router);
}
}

int main ()
{
    UNITY_BEGIN ();

    setup_test_environment ();

    RUN_TEST (test_dealer_request_is_visible_through_router_recv);

    const int rc = UNITY_END ();
    fflush (NULL);
    std::_Exit (rc);
}
