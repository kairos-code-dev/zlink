/* SPDX-License-Identifier: MPL-2.0 */

#include "../testutil.hpp"
#include "../testutil_unity.hpp"

#include <stdlib.h>
#include <string.h>
#include <chrono>

void setUp ()
{
}

void tearDown ()
{
}

static void recv_raw_spot_message (void *sub_socket_,
                                   const char *topic_,
                                   const char *payload_)
{
    zlink_pollitem_t item = {sub_socket_, 0, ZLINK_POLLIN, 0};
    TEST_ASSERT_EQUAL_INT (1, zlink_poll (&item, 1, 2000));
    TEST_ASSERT_TRUE ((item.revents & ZLINK_POLLIN) != 0);

    zlink_msg_t topic;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_init (&topic));
    const int topic_rc = zlink_msg_recv (&topic, sub_socket_, ZLINK_DONTWAIT);
    TEST_ASSERT_EQUAL_INT (static_cast<int> (strlen (topic_)), topic_rc);
    TEST_ASSERT_EQUAL_MEMORY (topic_, zlink_msg_data (&topic), strlen (topic_));
    TEST_ASSERT_TRUE (zlink_msg_more (&topic) != 0);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_close (&topic));

    zlink_msg_t payload;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_init (&payload));
    const int payload_rc =
      zlink_msg_recv (&payload, sub_socket_, ZLINK_DONTWAIT);
    TEST_ASSERT_EQUAL_INT (static_cast<int> (strlen (payload_)), payload_rc);
    TEST_ASSERT_EQUAL_MEMORY (payload_, zlink_msg_data (&payload),
                              strlen (payload_));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_close (&payload));
}

static void test_spot_sub_can_be_polled_via_service_instance ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *pub_node = zlink_spot_node_new (ctx);
    void *sub_node = zlink_spot_node_new (ctx);
    TEST_ASSERT_NOT_NULL (pub_node);
    TEST_ASSERT_NOT_NULL (sub_node);

    char endpoint[MAX_SOCKET_STRING];
    snprintf (endpoint, sizeof (endpoint), "tcp://127.0.0.1:%d",
              test_port (22100));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_bind (pub_node, endpoint));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_connect_peer_pub (sub_node, endpoint));

    void *pub = zlink_spot_pub_new (pub_node);
    void *sub = zlink_spot_sub_new (sub_node);
    TEST_ASSERT_NOT_NULL (pub);
    TEST_ASSERT_NOT_NULL (sub);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_sub_subscribe (sub, "mode:sub"));
    msleep (100);

    void *poller = zlink_poller_new ();
    TEST_ASSERT_NOT_NULL (poller);

    int tag_value = 17;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_poller_add_spot_sub (poller, sub, &tag_value, ZLINK_POLLIN));

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_pub_publish_bytes (pub, "mode:sub", "pong", 4, 0));

    zlink_poller_event_t event;
    memset (&event, 0, sizeof (event));
    TEST_ASSERT_EQUAL_INT (1, zlink_poller_wait (poller, &event, 2000));
    TEST_ASSERT_NOT_NULL (event.socket);
    TEST_ASSERT_TRUE ((event.events & ZLINK_POLLIN) != 0);
    TEST_ASSERT_EQUAL_PTR (&tag_value, event.user_data);

    zlink_msg_t *parts = NULL;
    size_t part_count = 0;
    char topic[256];
    size_t topic_len = sizeof (topic);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_sub_recv (sub, &parts, &part_count, ZLINK_DONTWAIT, topic,
                           &topic_len));
    TEST_ASSERT_EQUAL_UINT (8, topic_len);
    TEST_ASSERT_EQUAL_MEMORY ("mode:sub", topic, topic_len);
    TEST_ASSERT_EQUAL_UINT64 (1, part_count);
    TEST_ASSERT_EQUAL_UINT (4, zlink_msg_size (&parts[0]));
    TEST_ASSERT_EQUAL_MEMORY ("pong", zlink_msg_data (&parts[0]), 4);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_close (&parts[0]));
    free (parts);

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_poller_modify_spot_sub (poller, sub, ZLINK_POLLIN | ZLINK_POLLOUT));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_poller_remove_spot_sub (poller, sub));

    TEST_ASSERT_SUCCESS_ERRNO (zlink_poller_destroy (&poller));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_sub_destroy (&sub));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_pub_destroy (&pub));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&sub_node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&pub_node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

static void test_spot_pub_can_be_polled_via_service_instance ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *pub_node = zlink_spot_node_new (ctx);
    TEST_ASSERT_NOT_NULL (pub_node);

    char endpoint[MAX_SOCKET_STRING];
    snprintf (endpoint, sizeof (endpoint), "tcp://127.0.0.1:%d",
              test_port (22101));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_bind (pub_node, endpoint));

    void *pub = zlink_spot_pub_new (pub_node);
    TEST_ASSERT_NOT_NULL (pub);

    void *raw_sub = zlink_socket (ctx, ZLINK_SUB);
    TEST_ASSERT_NOT_NULL (raw_sub);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_setsockopt (raw_sub, ZLINK_SUBSCRIBE, "mode:pub", 8));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_connect (raw_sub, endpoint));
    msleep (100);

    void *poller = zlink_poller_new ();
    TEST_ASSERT_NOT_NULL (poller);

    int tag_value = 23;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_poller_add_spot_pub (poller, pub, &tag_value, ZLINK_POLLOUT));

    zlink_poller_event_t event;
    memset (&event, 0, sizeof (event));
    TEST_ASSERT_EQUAL_INT (1, zlink_poller_wait (poller, &event, 2000));
    TEST_ASSERT_NOT_NULL (event.socket);
    TEST_ASSERT_TRUE ((event.events & ZLINK_POLLOUT) != 0);
    TEST_ASSERT_EQUAL_PTR (&tag_value, event.user_data);

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_pub_publish_bytes (pub, "mode:pub", "pong", 4, 0));
    recv_raw_spot_message (raw_sub, "mode:pub", "pong");

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_poller_modify_spot_pub (poller, pub, ZLINK_POLLOUT));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_poller_remove_spot_pub (poller, pub));

    TEST_ASSERT_SUCCESS_ERRNO (zlink_poller_destroy (&poller));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_close (raw_sub));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_pub_destroy (&pub));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&pub_node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

static void test_spot_peer_queries_do_not_enter_pollable_mode ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *pub_node = zlink_spot_node_new (ctx);
    void *sub_node = zlink_spot_node_new (ctx);
    TEST_ASSERT_NOT_NULL (pub_node);
    TEST_ASSERT_NOT_NULL (sub_node);

    char endpoint[MAX_SOCKET_STRING];
    snprintf (endpoint, sizeof (endpoint), "tcp://127.0.0.1:%d",
              test_port (22102));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_bind (pub_node, endpoint));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_connect_peer_pub (sub_node, endpoint));

    void *pub = zlink_spot_pub_new (pub_node);
    void *sub = zlink_spot_sub_new (sub_node);
    TEST_ASSERT_NOT_NULL (pub);
    TEST_ASSERT_NOT_NULL (sub);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_sub_subscribe (sub, "mode:peer"));
    msleep (100);

    size_t pub_peer_count = 0;
    size_t sub_peer_count = 0;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_pub_peers (pub, NULL, &pub_peer_count));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_sub_peers (sub, NULL, &sub_peer_count));
    TEST_ASSERT_TRUE (pub_peer_count > 0);
    TEST_ASSERT_TRUE (sub_peer_count > 0);

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_pub_publish_bytes (pub, "mode:peer", "pong", 4, 0));

    zlink_msg_t *parts = NULL;
    size_t part_count = 0;
    char topic[256];
    size_t topic_len = sizeof (topic);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_sub_recv (sub, &parts, &part_count, 0, topic, &topic_len));
    TEST_ASSERT_EQUAL_UINT (9, topic_len);
    TEST_ASSERT_EQUAL_MEMORY ("mode:peer", topic, topic_len);
    TEST_ASSERT_EQUAL_UINT64 (1, part_count);
    TEST_ASSERT_EQUAL_UINT (4, zlink_msg_size (&parts[0]));
    TEST_ASSERT_EQUAL_MEMORY ("pong", zlink_msg_data (&parts[0]), 4);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_close (&parts[0]));
    free (parts);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_sub_destroy (&sub));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_pub_destroy (&pub));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&sub_node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&pub_node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

static void test_spot_node_default_handles_can_be_polled_for_direct_io ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *pub_node = zlink_spot_node_new (ctx);
    void *sub_node = zlink_spot_node_new (ctx);
    TEST_ASSERT_NOT_NULL (pub_node);
    TEST_ASSERT_NOT_NULL (sub_node);

    char endpoint[MAX_SOCKET_STRING];
    snprintf (endpoint, sizeof (endpoint), "tcp://127.0.0.1:%d",
              test_port (22104));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_bind (pub_node, endpoint));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_connect_peer_pub (sub_node, endpoint));

    void *default_pub = zlink_spot_node_default_pub (pub_node);
    void *default_sub = zlink_spot_node_default_sub (sub_node);
    TEST_ASSERT_NOT_NULL (default_pub);
    TEST_ASSERT_NOT_NULL (default_sub);
    TEST_ASSERT_EQUAL_PTR (default_pub, zlink_spot_node_default_pub (pub_node));
    TEST_ASSERT_EQUAL_PTR (default_sub, zlink_spot_node_default_sub (sub_node));

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_subscribe (sub_node, "mode:node"));
    msleep (100);

    void *poller = zlink_poller_new ();
    TEST_ASSERT_NOT_NULL (poller);

    int pub_tag = 41;
    int sub_tag = 42;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_poller_add_spot_pub (poller, default_pub, &pub_tag, ZLINK_POLLOUT));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_poller_add_spot_sub (poller, default_sub, &sub_tag, ZLINK_POLLIN));
    msleep (50);

    zlink_poller_event_t event;
    memset (&event, 0, sizeof (event));
    TEST_ASSERT_EQUAL_INT (1, zlink_poller_wait (poller, &event, 2000));
    TEST_ASSERT_TRUE ((event.events & ZLINK_POLLOUT) != 0);

    for (int i = 0; i < 3; ++i) {
        TEST_ASSERT_SUCCESS_ERRNO (
          zlink_spot_node_publish_bytes (pub_node, "mode:node", "pong", 4, 0));
        msleep (20);
    }

    zlink_msg_t *parts = NULL;
    size_t part_count = 0;
    char topic[256] = {0};
    size_t topic_len = sizeof (topic);
    const std::chrono::steady_clock::time_point deadline =
      std::chrono::steady_clock::now () + std::chrono::milliseconds (2000);
    while (std::chrono::steady_clock::now () < deadline) {
        if (zlink_spot_node_recv (sub_node, &parts, &part_count, ZLINK_DONTWAIT,
                                  topic, &topic_len)
            == 0)
            break;
        TEST_ASSERT_EQUAL_INT (EAGAIN, zlink_errno ());
        msleep (10);
        topic_len = sizeof (topic);
    }
    TEST_ASSERT_NOT_NULL (parts);
    TEST_ASSERT_EQUAL_UINT (9, topic_len);
    TEST_ASSERT_EQUAL_MEMORY ("mode:node", topic, topic_len);
    TEST_ASSERT_EQUAL_UINT64 (1, part_count);
    TEST_ASSERT_EQUAL_UINT (4, zlink_msg_size (&parts[0]));
    TEST_ASSERT_EQUAL_MEMORY ("pong", zlink_msg_data (&parts[0]), 4);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_close (&parts[0]));
    free (parts);

    errno = 0;
    TEST_ASSERT_EQUAL_INT (-1, zlink_spot_pub_destroy (&default_pub));
    TEST_ASSERT_EQUAL_INT (EINVAL, zlink_errno ());
    errno = 0;
    TEST_ASSERT_EQUAL_INT (-1, zlink_spot_sub_destroy (&default_sub));
    TEST_ASSERT_EQUAL_INT (EINVAL, zlink_errno ());

    TEST_ASSERT_SUCCESS_ERRNO (zlink_poller_remove_spot_sub (poller, default_sub));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_poller_remove_spot_pub (poller, default_pub));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_poller_destroy (&poller));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&sub_node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&pub_node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

int main (int, char **)
{
    setup_test_environment (300);

    UNITY_BEGIN ();
    RUN_TEST (test_spot_sub_can_be_polled_via_service_instance);
    RUN_TEST (test_spot_pub_can_be_polled_via_service_instance);
    RUN_TEST (test_spot_peer_queries_do_not_enter_pollable_mode);
    RUN_TEST (test_spot_node_default_handles_can_be_polled_for_direct_io);
    return UNITY_END ();
}
