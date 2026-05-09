/* SPDX-License-Identifier: MPL-2.0 */

#ifndef ZLINK_TEST_SPOT_MULTI_SERVICE_FIXTURE_HPP_INCLUDED
#define ZLINK_TEST_SPOT_MULTI_SERVICE_FIXTURE_HPP_INCLUDED

#include "../testutil.hpp"
#include "../testutil_unity.hpp"

#include <stdio.h>
#include <string.h>

#include <string>
#include <vector>

namespace spot_multi_service_fixture
{
inline void teardown_diagf_local (const char *stage_)
{
    if (!getenv ("ZLINK_DEBUG_MULTI_SERVICE_TEARDOWN"))
        return;
    fprintf (stderr, "[multi-service-teardown] %s\n", stage_);
    fflush (stderr);
}

inline bool connect_discovery_registry_with_retry_local (
  void *discovery_, const char *endpoint_, int timeout_ms_)
{
    return zlink_test_wait_until (timeout_ms_, [=] {
        if (zlink_discovery_connect_registry (discovery_, endpoint_)
            == ZLINK_CONNECT_OK) {
            return true;
        }
        return false;
    });
}

inline bool wait_for_service_summary_count_local (
  void *discovery_, uint16_t service_role_, size_t expected_count_, int timeout_ms_)
{
    return zlink_test_wait_until (timeout_ms_, [=] {
        zlink_member_peer_entry_t entries[8];
        size_t count = 8;
        if (zlink_discovery_member_peers (discovery_, entries, &count)
              == ZLINK_CONFIG_OK) {
            size_t matched = 0;
            for (size_t j = 0; j < count; ++j) {
                if (entries[j].service_role == service_role_)
                    ++matched;
            }
            if (matched >= expected_count_)
                return true;
        }
        return false;
    });
}

inline bool wait_for_service_attachment_shape_local (
  void *node_,
  const char *channel_name_,
  uint32_t auto_router_count_,
  uint32_t auto_pub_count_,
  uint32_t auto_sub_count_,
  int timeout_ms_)
{
    return zlink_test_wait_until (timeout_ms_, [=] {
        size_t count = 0;
        if (zlink_spot_node_service_attachment_count (node_, &count)
            == ZLINK_CONFIG_OK) {
            for (size_t j = 0; j < count; ++j) {
                zlink_spot_service_attachment_stats_t row;
                memset (&row, 0, sizeof (row));
                if (zlink_spot_node_service_attachment_at (node_, j, &row)
                      != ZLINK_CONFIG_OK) {
                    continue;
                }
                if (strcmp (row.channel_name, channel_name_) != 0)
                    continue;
                if (row.auto_router_count == auto_router_count_
                    && row.auto_pub_count == auto_pub_count_
                    && row.auto_sub_count == auto_sub_count_) {
                    return true;
                }
            }
        }
        return false;
    });
}

inline bool wait_for_subscription_event_local (
  void *xpub_, const char *expected_topic_, int *subscribed_out_, int timeout_ms_)
{
    return zlink_test_wait_until (timeout_ms_, [=] {
        zlink_routing_id_t source_rid;
        char topic[64];
        size_t topic_len = sizeof (topic);
        int subscribed = 0;
        memset (&source_rid, 0, sizeof (source_rid));
        if (zlink_subscription_event (
              xpub_, &source_rid, &subscribed, topic, &topic_len, ZLINK_DONTWAIT)
            == ZLINK_RECV_OK) {
            if (topic_len == strlen (expected_topic_)
                && memcmp (topic, expected_topic_, topic_len) == 0) {
                if (subscribed_out_)
                    *subscribed_out_ = subscribed;
                return true;
            }
        }
        return false;
    });
}

inline bool wait_for_spot_service_message_local (
  void *spot_,
  const char *expected_service_,
  const char *expected_topic_,
  const char *expected_payload_,
  int timeout_ms_)
{
    return zlink_test_wait_until (timeout_ms_, [=] {
        zlink_msg_t *parts = NULL;
        size_t part_count = 0;
        zlink_routing_id_t source_rid;
        char topic[64];
        size_t topic_len = sizeof (topic);
        memset (&source_rid, 0, sizeof (source_rid));
        const zlink_recv_result_t rc = zlink_spot_subscribe (
          spot_, &source_rid, &parts, &part_count, topic, &topic_len,
          static_cast<zlink_recv_flags_t> (ZLINK_DONTWAIT));
        if (rc == ZLINK_RECV_OK) {
            const bool matched =
              part_count == 1
              && topic_len == strlen (expected_topic_)
              && zlink_msg_size (&parts[0]) == strlen (expected_payload_)
              && memcmp (topic, expected_topic_, topic_len) == 0
              && memcmp (zlink_msg_data (&parts[0]), expected_payload_,
                         strlen (expected_payload_))
                   == 0;
            zlink_multipart_close (parts, part_count);
            if (matched)
                return true;
        }
        return false;
    });
}

inline bool wait_for_publish_and_spot_service_message_local (
  void *xpub_,
  void *spot_,
  const char *expected_service_,
  const char *expected_topic_,
  const char *expected_payload_,
  int timeout_ms_)
{
    return zlink_test_wait_until (timeout_ms_, [=] {
        zlink_msg_t part;
        const size_t payload_size = strlen (expected_payload_);
        TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_init_size (&part, payload_size));
        memcpy (zlink_msg_data (&part), expected_payload_, payload_size);
        TEST_ASSERT_EQUAL_INT (ZLINK_SUBMIT_OK,
                               zlink_publish (xpub_, expected_topic_, &part, 1, 0));
        if (wait_for_spot_service_message_local (
              spot_, expected_service_, expected_topic_, expected_payload_,
              zlink_test_poll_step_ms)) {
            return true;
        }
        return false;
    });
}

inline bool try_recv_router_payload_local (void *router_,
                                           std::string *payload_out_)
{
    const zlink_routing_id_t *source_node_rid = NULL;
    const zlink_routing_id_t *source_spot_rid = NULL;
    uint64_t request_seq = 0;
    zlink_msg_t *parts = NULL;
    size_t part_count = 0;
    if (zlink_router_recv (router_, &source_node_rid, &source_spot_rid,
                           &request_seq, &parts, &part_count, ZLINK_DONTWAIT)
        != ZLINK_RECV_OK) {
        return false;
    }

    if (payload_out_) {
        payload_out_->assign (
          static_cast<const char *> (zlink_msg_data (&parts[0])),
          zlink_msg_size (&parts[0]));
    }
    zlink_multipart_close (parts, part_count);
    return true;
}

inline bool wait_for_router_distribution_local (void *spot_,
                                                const char *channel_name_,
                                                void *router_a_,
                                                void *router_b_,
                                                int timeout_ms_)
{
    bool saw_a = false;
    bool saw_b = false;
    int sent = 0;
    return zlink_test_wait_until (timeout_ms_, [=, &saw_a, &saw_b, &sent] {
        char payload[32];
        snprintf (payload, sizeof (payload), "msg-%d", sent++);

        zlink_msg_t part;
        const size_t payload_size = strlen (payload);
        TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_init_size (&part, payload_size));
        memcpy (zlink_msg_data (&part), payload, payload_size);
        const zlink_submit_result_t submit_rc =
          zlink_spot_send_channel (spot_, channel_name_, &part, 1,
                                   ZLINK_DONTWAIT);
        if (submit_rc != ZLINK_SUBMIT_OK)
            zlink_msg_close (&part);

        std::string recv_payload;
        if (!saw_a && try_recv_router_payload_local (router_a_, &recv_payload))
            saw_a = true;
        if (!saw_b && try_recv_router_payload_local (router_b_, &recv_payload))
            saw_b = true;
        return saw_a && saw_b;
    });
}

inline bool wait_for_router_set_distribution_local (
  void *spot_,
  const char *channel_name_,
  const std::vector<void *> &routers_,
  int timeout_ms_)
{
    std::vector<bool> seen (routers_.size (), false);
    size_t seen_count = 0;
    int sent = 0;

    return zlink_test_wait_until (
      timeout_ms_, [=, &seen, &seen_count, &sent, &routers_] {
        char payload[32];
        snprintf (payload, sizeof (payload), "fanout-%d", sent++);

        zlink_msg_t part;
        const size_t payload_size = strlen (payload);
        TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_init_size (&part, payload_size));
        memcpy (zlink_msg_data (&part), payload, payload_size);
        const zlink_submit_result_t submit_rc =
          zlink_spot_send_channel (spot_, channel_name_, &part, 1,
                                   ZLINK_DONTWAIT);
        if (submit_rc != ZLINK_SUBMIT_OK)
            zlink_msg_close (&part);

        for (size_t router_index = 0; router_index < routers_.size ();
             ++router_index) {
            std::string recv_payload;
            if (!seen[router_index]
                && try_recv_router_payload_local (routers_[router_index],
                                                  &recv_payload)) {
                seen[router_index] = true;
                ++seen_count;
            }
        }
        return seen_count == routers_.size ();
    });
}

inline void destroy_multi_service_fixture_local (void **spot_,
                                                 void **node_,
                                                 void **consumer_discovery_,
                                                 void **provider_discovery_,
                                                 void **registry_,
                                                 void *ctx_)
{
    if (spot_ && *spot_) {
        teardown_diagf_local ("spot_destroy");
        TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (spot_));
    }
    if (consumer_discovery_ && *consumer_discovery_) {
        teardown_diagf_local ("consumer_discovery_destroy");
        TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_destroy (consumer_discovery_));
    }
    if (provider_discovery_ && *provider_discovery_) {
        teardown_diagf_local ("provider_discovery_destroy");
        TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_destroy (provider_discovery_));
    }
    if (node_ && *node_) {
        teardown_diagf_local ("spot_node_destroy");
        TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (node_));
    }
    if (registry_ && *registry_) {
        teardown_diagf_local ("registry_destroy");
        TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_destroy (registry_));
    }
    teardown_diagf_local ("ctx_shutdown");
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_shutdown (ctx_));
    teardown_diagf_local ("ctx_term");
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx_));
}
}

#endif
