/* SPDX-License-Identifier: MPL-2.0 */

#include "testutil.hpp"
#include "testutil_unity.hpp"

#include <string.h>

#include <thread>
#include <chrono>
#include <string>

extern "C" int zlink_socket_request_progress_internal (void *socket_);

namespace
{
void init_string_part (zlink_msg_t *part_, const char *text_)
{
    const size_t size = strlen (text_);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_init_size (part_, size));
    memcpy (zlink_msg_data (part_), text_, size);
}

void set_routing_id_text (void *handle_, const char *text_)
{
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_routing_id (handle_, text_, strlen (text_)));
}

std::string msg_to_string (const zlink_msg_t *part_)
{
    zlink_msg_t *mutable_part = const_cast<zlink_msg_t *> (part_);
    return std::string (static_cast<const char *> (zlink_msg_data (mutable_part)),
                        zlink_msg_size (part_));
}

zlink_routing_id_t get_routing_id_value (void *handle_)
{
    zlink_routing_id_t rid;
    memset (&rid, 0, sizeof (rid));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_get_routing_id (handle_, &rid));
    return rid;
}

void ignore_reply (zlink_request_result_t,
                   zlink_msg_t *parts_,
                   size_t part_count_,
                   void *)
{
    if (parts_ && part_count_ > 0)
        zlink_multipart_close (parts_, part_count_);
}

void test_spot_poller_wait_reports_original_spot_for_subscribe ()
{
    void *ctx = zlink_ctx_new ();
    void *node = zlink_spot_node_new (ctx);
    void *sub_spot = zlink_spot_new (node);
    void *pub_spot = zlink_spot_new (node);
    void *poller = zlink_poller_new ();
    int user_tag = 11;
    TEST_ASSERT_NOT_NULL (ctx);
    TEST_ASSERT_NOT_NULL (node);
    TEST_ASSERT_NOT_NULL (sub_spot);
    TEST_ASSERT_NOT_NULL (pub_spot);
    TEST_ASSERT_NOT_NULL (poller);

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_subscription (sub_spot, "poll.spot.topic"));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_poller_add (poller, sub_spot, &user_tag, ZLINK_POLLIN));

    zlink_msg_t part;
    init_string_part (&part, "poll-spot-subscribe");
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_publish (pub_spot, "poll.spot.topic", &part, 1, 0));

    zlink_poller_event_t ev;
    TEST_ASSERT_EQUAL_INT (1, zlink_poller_wait (poller, &ev, 1000, NULL));
    TEST_ASSERT_EQUAL_INT (ZLINK_POLLER_SOURCE_SOCKET, ev.source_kind);
    TEST_ASSERT_EQUAL_PTR (sub_spot, ev.socket);
    TEST_ASSERT_EQUAL_PTR (&user_tag, ev.user_data);
    TEST_ASSERT_EQUAL_INT (ZLINK_POLLIN, ev.events);

    zlink_routing_id_t source_rid;
    zlink_msg_t *parts = NULL;
    size_t part_count = 0;
    char topic[64];
    size_t topic_len = sizeof (topic);
    memset (&source_rid, 0, sizeof (source_rid));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_subscribe (
      sub_spot, &source_rid, &parts, &part_count, topic, &topic_len,
      ZLINK_DONTWAIT));
    TEST_ASSERT_EQUAL_STRING ("poll.spot.topic",
                              std::string (topic, topic_len).c_str ());
    TEST_ASSERT_EQUAL_STRING ("poll-spot-subscribe",
                              part_count > 0 ? msg_to_string (&parts[0]).c_str ()
                                             : "");
    zlink_multipart_close (parts, part_count);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_poller_remove (poller, sub_spot));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_poller_destroy (&poller));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&pub_spot));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&sub_spot));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

void test_spot_poller_wait_reports_original_spot_for_routed_recv ()
{
    void *ctx = zlink_ctx_new ();
    void *node = zlink_spot_node_new (ctx);
    void *recv_spot = zlink_spot_new (node);
    void *router = zlink_socket (ctx, ZLINK_SOCKET_ROUTER);
    void *poller = zlink_poller_new ();
    int user_tag = 29;
    TEST_ASSERT_NOT_NULL (ctx);
    TEST_ASSERT_NOT_NULL (node);
    TEST_ASSERT_NOT_NULL (recv_spot);
    TEST_ASSERT_NOT_NULL (router);
    TEST_ASSERT_NOT_NULL (poller);

    const zlink_routing_id_t node_rid = get_routing_id_value (node);
    const zlink_routing_id_t recv_spot_rid = get_routing_id_value (recv_spot);
    set_routing_id_text (router, "poller-router");

    TEST_ASSERT_SUCCESS_ERRNO (zlink_poller_add (poller, recv_spot, &user_tag,
                                                 ZLINK_POLLIN));

    zlink_msg_t part;
    init_string_part (&part, "poll-spot-routed");
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_router_request_spot (router, &node_rid, &recv_spot_rid, &part, 1,
                                 &ignore_reply, NULL, 0, 1000));

    zlink_poller_event_t ev;
    TEST_ASSERT_EQUAL_INT (1, zlink_poller_wait (poller, &ev, 1000, NULL));
    TEST_ASSERT_EQUAL_INT (ZLINK_POLLER_SOURCE_SOCKET, ev.source_kind);
    TEST_ASSERT_EQUAL_PTR (recv_spot, ev.socket);
    TEST_ASSERT_EQUAL_PTR (&user_tag, ev.user_data);
    TEST_ASSERT_EQUAL_INT (ZLINK_POLLIN, ev.events);

    const zlink_routing_id_t *source_node_rid = NULL;
    const zlink_routing_id_t *source_spot_rid = NULL;
    uint64_t request_seq = 0;
    zlink_msg_t *parts = NULL;
    size_t part_count = 0;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_recv (recv_spot, &source_node_rid, &source_spot_rid, &request_seq,
                       &parts, &part_count, ZLINK_DONTWAIT));
    TEST_ASSERT_NOT_NULL (source_node_rid);
    TEST_ASSERT_TRUE (source_spot_rid == NULL || source_spot_rid->size == 0);
    TEST_ASSERT_TRUE (request_seq != 0);
    TEST_ASSERT_EQUAL_STRING (
      "poll-spot-routed",
      part_count > 0 ? msg_to_string (&parts[0]).c_str () : "");
    zlink_multipart_close (parts, part_count);

    zlink_msg_t reply_part;
    init_string_part (&reply_part, "poll-spot-reply");
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_reply_router (recv_spot, source_node_rid, request_seq,
                               &reply_part, 1));
    std::this_thread::sleep_for (std::chrono::milliseconds (10));
    TEST_ASSERT_TRUE (zlink_socket_request_progress_internal (router) >= 0);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_poller_remove (poller, recv_spot));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_poller_destroy (&poller));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_close (router));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&recv_spot));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}
}

SETUP_TEARDOWN_TESTCONTEXT

int main (void)
{
    setup_test_environment (60);

    UNITY_BEGIN ();
    RUN_TEST (test_spot_poller_wait_reports_original_spot_for_subscribe);
    RUN_TEST (test_spot_poller_wait_reports_original_spot_for_routed_recv);
    return UNITY_END ();
}
