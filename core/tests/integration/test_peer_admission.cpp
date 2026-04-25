/* SPDX-License-Identifier: MPL-2.0 */

#include "testutil.hpp"
#include "testutil_monitoring.hpp"
#include "testutil_unity.hpp"

#include <string.h>

SETUP_TEARDOWN_TESTCONTEXT

namespace
{
bool bind_registry_test_endpoints_local (void *registry_,
                                         char *pub_out_,
                                         size_t pub_size_,
                                         char *router_out_,
                                         size_t router_size_)
{
    if (!registry_ || !pub_out_ || !router_out_ || pub_size_ == 0
        || router_size_ == 0) {
        errno = EINVAL;
        return false;
    }

    const int base_port = 49900 + test_port_offset ();
    for (int i = 0; i < 128; ++i) {
        snprintf (pub_out_, pub_size_, "tcp://127.0.0.1:%d",
                  base_port + i * 2);
        snprintf (router_out_, router_size_, "tcp://127.0.0.1:%d",
                  base_port + i * 2 + 1);
        if (zlink_registry_bind (registry_, pub_out_, router_out_)
            == ZLINK_BIND_OK) {
            return true;
        }
    }

    return false;
}

bool bind_socket_test_endpoint_local (void *socket_,
                                      int base_port_,
                                      char *endpoint_out_,
                                      size_t endpoint_size_)
{
    if (!socket_ || !endpoint_out_ || endpoint_size_ == 0) {
        errno = EINVAL;
        return false;
    }

    const int base_port = base_port_ + test_port_offset ();
    for (int i = 0; i < 128; ++i) {
        snprintf (endpoint_out_, endpoint_size_, "tcp://127.0.0.1:%d",
                  base_port + i);
        if (zlink_bind (socket_, endpoint_out_) == ZLINK_BIND_OK)
            return true;
    }

    return false;
}

void noop_reply_handler (zlink_request_result_t result_,
                         zlink_msg_t *parts_,
                         size_t part_count_,
                         void *userdata_)
{
    LIBZLINK_UNUSED (result_);
    LIBZLINK_UNUSED (userdata_);
    zlink_multipart_close (parts_, part_count_);
}

int compare_routing_id_local (const zlink_routing_id_t *lhs_,
                              const zlink_routing_id_t *rhs_)
{
    const size_t lhs_size = lhs_ ? lhs_->size : 0;
    const size_t rhs_size = rhs_ ? rhs_->size : 0;
    const size_t shared = lhs_size < rhs_size ? lhs_size : rhs_size;
    const int cmp =
      shared > 0 ? memcmp (lhs_->data, rhs_->data, shared) : 0;
    if (cmp != 0)
        return cmp;
    if (lhs_size < rhs_size)
        return -1;
    if (lhs_size > rhs_size)
        return 1;
    return 0;
}

bool routing_id_equals_local (const zlink_routing_id_t *lhs_,
                              const zlink_routing_id_t *rhs_)
{
    return compare_routing_id_local (lhs_, rhs_) == 0;
}

void init_text_part_local (zlink_msg_t *part_, const char *text_)
{
    const size_t size = text_ ? strlen (text_) : 0;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_init_size (part_, size));
    if (size > 0)
        memcpy (zlink_msg_data (part_), text_, size);
}

zlink_submit_result_t send_text_local (void *socket_,
                                       const char *text_,
                                       zlink_send_flags_t flags_)
{
    zlink_msg_t part;
    memset (&part, 0, sizeof (part));
    init_text_part_local (&part, text_);
    return zlink_send (socket_, &part, 1, flags_);
}

zlink_config_result_t set_router_weight_local (void *router_, int weight_)
{
    return zlink_set_router_option (router_, ZLINK_ROUTER_OPT_WEIGHT, &weight_,
                                    sizeof (weight_));
}

zlink_submit_result_t send_text_rid_local (void *socket_,
                                           const zlink_routing_id_t *rid_,
                                           const char *text_,
                                           zlink_send_flags_t flags_)
{
    zlink_msg_t part;
    memset (&part, 0, sizeof (part));
    init_text_part_local (&part, text_);
    return zlink_send_rid (socket_, rid_, &part, 1, flags_);
}

zlink_submit_result_t dealer_request_text_local (void *dealer_,
                                                 const char *text_)
{
    zlink_msg_t part;
    memset (&part, 0, sizeof (part));
    init_text_part_local (&part, text_);
    return zlink_dealer_request (dealer_, &part, 1, &noop_reply_handler, NULL,
                                 ZLINK_DONTWAIT, 1000);
}

zlink_submit_result_t router_request_text_local (void *router_,
                                                 const zlink_routing_id_t *rid_,
                                                 const char *text_)
{
    zlink_msg_t part;
    memset (&part, 0, sizeof (part));
    init_text_part_local (&part, text_);
    return zlink_router_request (router_, rid_, &part, 1, &noop_reply_handler,
                                 NULL, ZLINK_DONTWAIT, 1000);
}

bool recv_text_local (void *socket_,
                      const char *expected_,
                      zlink_routing_id_t *source_rid_out_,
                      int timeout_ms_)
{
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_option (socket_, ZLINK_OPT_RCVTIMEO, &timeout_ms_,
                        sizeof (timeout_ms_)));

    zlink_msg_t *parts = NULL;
    size_t part_count = 0;
    zlink_routing_id_t source_rid;
    memset (&source_rid, 0, sizeof (source_rid));
    const zlink_recv_result_t rc =
      zlink_recv (socket_, &source_rid, &parts, &part_count, 0);
    if (rc != ZLINK_RECV_OK)
        return false;

    TEST_ASSERT_EQUAL_UINT32 (1u, static_cast<uint32_t> (part_count));
    TEST_ASSERT_EQUAL_UINT32 (
      static_cast<uint32_t> (strlen (expected_)),
      static_cast<uint32_t> (zlink_msg_size (&parts[0])));
    TEST_ASSERT_EQUAL_MEMORY (expected_, zlink_msg_data (&parts[0]),
                              strlen (expected_));
    if (source_rid_out_)
        *source_rid_out_ = source_rid;
    zlink_multipart_close (parts, part_count);
    return true;
}

bool recv_router_text_local (void *router_,
                             const char *expected_,
                             zlink_routing_id_t *source_rid_out_,
                             int timeout_ms_)
{
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_option (router_, ZLINK_OPT_RCVTIMEO, &timeout_ms_,
                        sizeof (timeout_ms_)));

    const zlink_routing_id_t *source_rid = NULL;
    const zlink_routing_id_t *source_spot_rid = NULL;
    uint64_t request_seq = 0;
    zlink_msg_t *parts = NULL;
    size_t part_count = 0;
    const zlink_recv_result_t rc =
      zlink_router_recv (router_, &source_rid, &source_spot_rid, &request_seq,
                         &parts, &part_count, 0);
    if (rc != ZLINK_RECV_OK)
        return false;

    TEST_ASSERT_NOT_NULL (source_rid);
    TEST_ASSERT_TRUE (source_spot_rid == NULL || source_spot_rid->size == 0);
    TEST_ASSERT_EQUAL_UINT64 (0u, request_seq);
    TEST_ASSERT_EQUAL_UINT32 (1u, static_cast<uint32_t> (part_count));
    TEST_ASSERT_EQUAL_UINT32 (
      static_cast<uint32_t> (strlen (expected_)),
      static_cast<uint32_t> (zlink_msg_size (&parts[0])));
    TEST_ASSERT_EQUAL_MEMORY (expected_, zlink_msg_data (&parts[0]),
                              strlen (expected_));
    if (source_rid_out_)
        *source_rid_out_ = *source_rid;
    zlink_multipart_close (parts, part_count);
    return true;
}

bool recv_router_any_local (void *router_, int timeout_ms_)
{
    const zlink_routing_id_t *source_rid = NULL;
    const zlink_routing_id_t *source_spot_rid = NULL;
    uint64_t request_seq = 0;
    zlink_msg_t *parts = NULL;
    size_t part_count = 0;
    const zlink_recv_result_t rc =
      zlink_router_recv (router_, &source_rid, &source_spot_rid, &request_seq,
                         &parts, &part_count,
                         timeout_ms_ <= 0 ? ZLINK_DONTWAIT : 0);
    if (rc != ZLINK_RECV_OK)
        return false;
    zlink_multipart_close (parts, part_count);
    return true;
}

bool recv_buffer_text_local (void *socket_,
                             const char *expected_,
                             int timeout_ms_)
{
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_option (socket_, ZLINK_OPT_RCVTIMEO, &timeout_ms_,
                        sizeof (timeout_ms_)));

    char buffer[256];
    memset (buffer, 0, sizeof (buffer));
    const int rc = zlink_recv (socket_, buffer, sizeof (buffer), 0);
    if (rc < 0)
        return false;

    TEST_ASSERT_EQUAL_UINT32 (static_cast<uint32_t> (strlen (expected_)),
                              static_cast<uint32_t> (rc));
    TEST_ASSERT_EQUAL_STRING_LEN (expected_, buffer, strlen (expected_));
    return true;
}

bool wait_for_send_delivery_local (void *sender_,
                                   void *receiver_,
                                   const char *text_,
                                   int timeout_ms_)
{
    const int attempts = timeout_ms_ / 25;
    for (int i = 0; i < attempts; ++i) {
        const zlink_submit_result_t rc =
          send_text_local (sender_, text_, ZLINK_DONTWAIT);
        if (rc == ZLINK_SUBMIT_OK && recv_text_local (receiver_, text_, NULL, 1000))
            return true;
        msleep (25);
    }
    return false;
}

bool wait_for_routed_delivery_local (void *sender_,
                                     const zlink_routing_id_t *target_rid_,
                                     void *receiver_,
                                     const char *text_,
                                     int timeout_ms_)
{
    const int attempts = timeout_ms_ / 25;
    for (int i = 0; i < attempts; ++i) {
        const zlink_submit_result_t rc =
          send_text_rid_local (sender_, target_rid_, text_, ZLINK_DONTWAIT);
        if (rc == ZLINK_SUBMIT_OK
            && recv_router_text_local (receiver_, text_, NULL, 1000))
            return true;
        msleep (25);
    }
    return false;
}

bool wait_for_router_envelope_delivery_local (void *sender_,
                                              const zlink_routing_id_t *target_rid_,
                                              void *receiver_,
                                              const char *expected_source_rid_,
                                              const char *text_,
                                              int timeout_ms_)
{
    const int attempts = timeout_ms_ / 25;
    for (int i = 0; i < attempts; ++i) {
        const int route_rc =
          zlink_send (sender_, target_rid_->data, target_rid_->size,
                      ZLINK_SNDMORE | ZLINK_DONTWAIT);
        if (route_rc >= 0) {
            const int payload_rc =
              zlink_send (sender_, text_, strlen (text_), ZLINK_DONTWAIT);
            if (payload_rc >= 0
                && recv_buffer_text_local (receiver_, expected_source_rid_,
                                           1000)
                && recv_buffer_text_local (receiver_, text_, 1000)) {
                return true;
            }
        }
        msleep (25);
    }
    return false;
}

bool wait_for_socket_peer_weight_event_local (
  void *monitor_,
  const zlink_routing_id_t *expected_rid_,
  uint32_t expected_weight_,
  int timeout_ms_)
{
    const int attempts = timeout_ms_ / 25;
    for (int i = 0; i < attempts; ++i) {
        zlink_monitor_event_t event;
        memset (&event, 0, sizeof (event));
        if (recv_monitor_event_from_socket (monitor_, &event, ZLINK_DONTWAIT)
            == 0) {
            const bool routing_id_ok =
              !expected_rid_ || event.routing_id.size == 0
              || routing_id_equals_local (&event.routing_id, expected_rid_);
            if (event.event == ZLINK_EVENT_PEER_WEIGHT_CHANGED
                && event.value == static_cast<uint64_t> (expected_weight_)
                && routing_id_ok) {
                return true;
            }
            continue;
        }
        TEST_ASSERT_TRUE (errno == EAGAIN || errno == EINTR);
        msleep (25);
    }
    return false;
}

bool wait_for_service_peer_weight_event_local (
  void *monitor_,
  const char *expected_endpoint_,
  const zlink_routing_id_t *expected_rid_,
  uint32_t expected_weight_,
  int timeout_ms_)
{
    const int attempts = timeout_ms_ / 25;
    for (int i = 0; i < attempts; ++i) {
        zlink_service_event_t event;
        memset (&event, 0, sizeof (event));
        if (recv_service_event_from_socket (monitor_, &event, ZLINK_DONTWAIT)
            == 0) {
            const bool routing_id_ok =
              !expected_rid_ || event.routing_id.size == 0
              || routing_id_equals_local (&event.routing_id, expected_rid_);
            if (event.event_type
                  == ZLINK_SERVICE_MONITOR_EVENT_PEER_WEIGHT_CHANGED
                && event.value == static_cast<uint32_t> (expected_weight_)
                && strcmp (event.endpoint, expected_endpoint_) == 0
                && routing_id_ok) {
                return true;
            }
            continue;
        }
        TEST_ASSERT_TRUE (errno == EAGAIN || errno == EINTR);
        msleep (25);
    }
    return false;
}

bool wait_for_discovery_peer_weight_local (void *discovery_,
                                          const char *endpoint_,
                                          uint32_t expected_,
                                          int timeout_ms_)
{
    const int attempts = timeout_ms_ / 25;
    for (int i = 0; i < attempts; ++i) {
        zlink_member_peer_entry_t entries[8];
        size_t count = 8;
        if (zlink_discovery_member_peers (discovery_, entries, &count)
            == ZLINK_CONFIG_OK) {
            for (size_t j = 0; j < count; ++j) {
                if (strcmp (entries[j].endpoint, endpoint_) == 0
                    && entries[j].weight == expected_) {
                    return true;
                }
            }
        }
        msleep (25);
    }
    return false;
}

bool wait_for_submit_result_local (void *socket_,
                                   const char *text_,
                                   zlink_submit_result_t expected_,
                                   int timeout_ms_)
{
    const int attempts = timeout_ms_ / 25;
    for (int i = 0; i < attempts; ++i) {
        if (send_text_local (socket_, text_, ZLINK_DONTWAIT) == expected_)
            return true;
        msleep (25);
    }
    return false;
}

bool wait_for_router_submit_result_local (void *socket_,
                                          const zlink_routing_id_t *rid_,
                                          const char *text_,
                                          zlink_submit_result_t expected_,
                                          int timeout_ms_)
{
    const int attempts = timeout_ms_ / 25;
    for (int i = 0; i < attempts; ++i) {
        if (send_text_rid_local (socket_, rid_, text_, ZLINK_DONTWAIT)
            == expected_) {
            return true;
        }
        msleep (25);
    }
    return false;
}

bool wait_for_dealer_request_result_local (void *dealer_,
                                           const char *text_,
                                           zlink_submit_result_t expected_,
                                           int timeout_ms_)
{
    const int attempts = timeout_ms_ / 25;
    for (int i = 0; i < attempts; ++i) {
        if (dealer_request_text_local (dealer_, text_) == expected_)
            return true;
        msleep (25);
    }
    return false;
}

bool wait_for_router_request_result_local (void *router_,
                                           const zlink_routing_id_t *rid_,
                                           const char *text_,
                                           zlink_submit_result_t expected_,
                                           int timeout_ms_)
{
    const int attempts = timeout_ms_ / 25;
    for (int i = 0; i < attempts; ++i) {
        if (router_request_text_local (router_, rid_, text_) == expected_)
            return true;
        msleep (25);
    }
    return false;
}

bool connect_discovery_registry_with_retry_local (void *discovery_,
                                                  const char *endpoint_,
                                                  int timeout_ms_)
{
    const int attempts = timeout_ms_ / 25;
    for (int i = 0; i < attempts; ++i) {
        if (zlink_discovery_connect_registry (discovery_, endpoint_)
            == ZLINK_CONNECT_OK) {
            return true;
        }
        msleep (25);
    }
    return false;
}

void send_weight_sample_local (void *sender_, int count_)
{
    for (int i = 0; i < count_; ++i) {
        bool sent = false;
        for (int attempt = 0; attempt < 100 && !sent; ++attempt) {
            sent = send_text_local (sender_, "m", ZLINK_DONTWAIT)
                   == ZLINK_SUBMIT_OK;
            if (!sent)
                msleep (1);
        }
        TEST_ASSERT_TRUE (sent);
    }
}

int drain_dealer_messages_local (void *dealer_, int max_count_)
{
    int count = 0;
    for (int i = 0; i < max_count_; ++i) {
        if (recv_text_local (dealer_, "m", NULL, 1))
            ++count;
        else
            break;
    }
    return count;
}

int drain_router_messages_local (void *router_, int max_count_)
{
    int count = 0;
    for (int i = 0; i < max_count_; ++i) {
        if (recv_router_any_local (router_, 0))
            ++count;
        else
            break;
    }
    return count;
}
} // namespace

void test_socket_default_options_are_enabled ()
{
    void *router = test_context_socket (ZLINK_SOCKET_ROUTER);

    int value = 0;
    size_t size = sizeof (value);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_get_router_option (
      router, ZLINK_ROUTER_OPT_MANDATORY, &value, &size));
    TEST_ASSERT_EQUAL_INT (1, value);

    value = -1;
    size = sizeof (value);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_get_option (
      router, ZLINK_OPT_RID_DUPLICATE_POLICY, &value, &size));
    TEST_ASSERT_EQUAL_INT (ZLINK_RID_DUPLICATE_REJECT, value);

    value = -1;
    size = sizeof (value);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_get_router_option (
      router, ZLINK_ROUTER_OPT_WEIGHT, &value, &size));
    TEST_ASSERT_EQUAL_INT (100, value);

    test_context_socket_close (router);
}

void test_invalid_weight_option_is_rejected ()
{
    void *router = test_context_socket (ZLINK_SOCKET_ROUTER);
    int value = 101;
    TEST_ASSERT_EQUAL_INT (
      ZLINK_CONFIG_INVALID_ARGUMENT,
      zlink_set_router_option (router, ZLINK_ROUTER_OPT_WEIGHT, &value,
                               sizeof (value)));
    value = -1;
    TEST_ASSERT_EQUAL_INT (
      ZLINK_CONFIG_INVALID_ARGUMENT,
      zlink_set_router_option (router, ZLINK_ROUTER_OPT_WEIGHT, &value,
                               sizeof (value)));
    test_context_socket_close (router);
}

void test_dealer_router_weighted_ratio_100_50 ()
{
    char endpoint_a[MAX_SOCKET_STRING];
    char endpoint_b[MAX_SOCKET_STRING];
    void *dealer = test_context_socket (ZLINK_SOCKET_DEALER);
    void *router_a = test_context_socket (ZLINK_SOCKET_ROUTER);
    void *router_b = test_context_socket (ZLINK_SOCKET_ROUTER);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_routing_id (router_a, "router-a", 8));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_routing_id (router_b, "router-b", 8));

    zlink_socket_monitor_open_options_t monitor_opts;
    memset (&monitor_opts, 0, sizeof (monitor_opts));
    monitor_opts.events = ZLINK_EVENT_PEER_WEIGHT_CHANGED;
    void *dealer_monitor = zlink_socket_monitor_open (dealer, &monitor_opts);
    TEST_ASSERT_NOT_NULL (dealer_monitor);

    bind_loopback_ipv4 (router_a, endpoint_a, sizeof (endpoint_a));
    bind_loopback_ipv4 (router_b, endpoint_b, sizeof (endpoint_b));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_connect (dealer, endpoint_a));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_connect (dealer, endpoint_b));
    zlink_routing_id_t rid_b;
    memset (&rid_b, 0, sizeof (rid_b));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_get_routing_id (router_b, &rid_b));
    bool saw_a = false;
    bool saw_b = false;
    for (int i = 0; i < 10 && (!saw_a || !saw_b); ++i) {
        TEST_ASSERT_EQUAL_INT (
          ZLINK_SUBMIT_OK,
          send_text_local (dealer, "warmup", ZLINK_DONTWAIT));
        msleep (5);
        saw_a = drain_router_messages_local (router_a, 2) > 0 || saw_a;
        saw_b = drain_router_messages_local (router_b, 2) > 0 || saw_b;
    }
    TEST_ASSERT_TRUE (saw_a);
    TEST_ASSERT_TRUE (saw_b);
    TEST_ASSERT_SUCCESS_ERRNO (set_router_weight_local (router_b, 50));
    TEST_ASSERT_TRUE (
      wait_for_socket_peer_weight_event_local (dealer_monitor, &rid_b, 50,
                                               5000));

    send_weight_sample_local (dealer, 15);
    msleep (25);
    const int count_a = drain_router_messages_local (router_a, 15);
    const int count_b = drain_router_messages_local (router_b, 15);
    TEST_ASSERT_EQUAL_INT (10, count_a);
    TEST_ASSERT_EQUAL_INT (5, count_b);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_monitor_close (&dealer_monitor));
    test_context_socket_close (router_b);
    test_context_socket_close (router_a);
    test_context_socket_close (dealer);
}

void test_dealer_dealer_weighted_ratio_100_50 ()
{
    char endpoint_a[MAX_SOCKET_STRING];
    char endpoint_b[MAX_SOCKET_STRING];
    void *sender = test_context_socket (ZLINK_SOCKET_DEALER);
    void *dealer_a = test_context_socket (ZLINK_SOCKET_DEALER);
    void *dealer_b = test_context_socket (ZLINK_SOCKET_DEALER);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_routing_id (dealer_a, "dealer-a", 8));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_routing_id (dealer_b, "dealer-b", 8));

    zlink_socket_monitor_open_options_t monitor_opts;
    memset (&monitor_opts, 0, sizeof (monitor_opts));
    monitor_opts.events = ZLINK_EVENT_PEER_WEIGHT_CHANGED;
    void *sender_monitor = zlink_socket_monitor_open (sender, &monitor_opts);
    TEST_ASSERT_NOT_NULL (sender_monitor);

    bind_loopback_ipv4 (dealer_a, endpoint_a, sizeof (endpoint_a));
    bind_loopback_ipv4 (dealer_b, endpoint_b, sizeof (endpoint_b));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_connect (sender, endpoint_a));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_connect (sender, endpoint_b));
    zlink_routing_id_t rid_b;
    memset (&rid_b, 0, sizeof (rid_b));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_get_routing_id (dealer_b, &rid_b));
    bool saw_a = false;
    bool saw_b = false;
    for (int i = 0; i < 10 && (!saw_a || !saw_b); ++i) {
        send_weight_sample_local (sender, 1);
        msleep (5);
        saw_a = drain_dealer_messages_local (dealer_a, 2) > 0 || saw_a;
        saw_b = drain_dealer_messages_local (dealer_b, 2) > 0 || saw_b;
    }
    TEST_ASSERT_TRUE (saw_a);
    TEST_ASSERT_TRUE (saw_b);
    int weight_b = 50;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_dealer_option (
      dealer_b, ZLINK_DEALER_OPT_WEIGHT, &weight_b, sizeof (weight_b)));
    TEST_ASSERT_TRUE (
      wait_for_socket_peer_weight_event_local (sender_monitor, &rid_b, 50,
                                               5000));

    send_weight_sample_local (sender, 15);
    msleep (25);
    const int count_a = drain_dealer_messages_local (dealer_a, 15);
    const int count_b = drain_dealer_messages_local (dealer_b, 15);
    TEST_ASSERT_EQUAL_INT (10, count_a);
    TEST_ASSERT_EQUAL_INT (5, count_b);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_monitor_close (&sender_monitor));
    test_context_socket_close (dealer_b);
    test_context_socket_close (dealer_a);
    test_context_socket_close (sender);
}

void test_router_weight_blocks_dealer_outbound_and_emits_monitor_event ()
{
    char endpoint[MAX_SOCKET_STRING];
    void *router = test_context_socket (ZLINK_SOCKET_ROUTER);
    void *dealer = test_context_socket (ZLINK_SOCKET_DEALER);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_routing_id (router, "router-a", 8));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_routing_id (dealer, "dealer-a", 8));

    zlink_socket_monitor_open_options_t monitor_opts;
    memset (&monitor_opts, 0, sizeof (monitor_opts));
    monitor_opts.events = ZLINK_EVENT_PEER_WEIGHT_CHANGED;
    void *dealer_monitor = zlink_socket_monitor_open (dealer, &monitor_opts);
    TEST_ASSERT_NOT_NULL (dealer_monitor);

    bind_loopback_ipv4 (router, endpoint, sizeof (endpoint));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_connect (dealer, endpoint));
    zlink_routing_id_t router_rid;
    memset (&router_rid, 0, sizeof (router_rid));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_get_routing_id (router, &router_rid));
    send_string_expect_success (dealer, "warmup", 0);
    recv_string_expect_success (router, "dealer-a", 0);
    recv_string_expect_success (router, "warmup", 0);

    TEST_ASSERT_SUCCESS_ERRNO (
      set_router_weight_local (router, 0));
    TEST_ASSERT_TRUE (wait_for_socket_peer_weight_event_local (
      dealer_monitor, &router_rid, 0, 5000));

    TEST_ASSERT_TRUE (wait_for_submit_result_local (
      dealer, "blocked", ZLINK_SUBMIT_NOT_ADMITTED, 4000));
    TEST_ASSERT_TRUE (wait_for_dealer_request_result_local (
      dealer, "request-blocked", ZLINK_SUBMIT_NOT_ADMITTED, 4000));

    TEST_ASSERT_SUCCESS_ERRNO (
      set_router_weight_local (router, 100));
    TEST_ASSERT_TRUE (wait_for_socket_peer_weight_event_local (
      dealer_monitor, &router_rid, 100, 5000));
    msleep (SETTLE_TIME);
    send_string_expect_success (dealer, "after-serving", 0);
    recv_string_expect_success (router, "dealer-a", 0);
    recv_string_expect_success (router, "after-serving", 0);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_monitor_close (&dealer_monitor));
    test_context_socket_close (dealer);
    test_context_socket_close (router);
}

void test_router_weight_blocks_router_outbound_and_emits_monitor_event ()
{
    char endpoint[MAX_SOCKET_STRING];
    void *router_a = test_context_socket (ZLINK_SOCKET_ROUTER);
    void *router_b = test_context_socket (ZLINK_SOCKET_ROUTER);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_routing_id (router_a, "router-a", 8));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_routing_id (router_b, "router-b", 8));

    zlink_socket_monitor_open_options_t monitor_opts;
    memset (&monitor_opts, 0, sizeof (monitor_opts));
    monitor_opts.events = ZLINK_EVENT_PEER_WEIGHT_CHANGED;
    void *router_a_monitor = zlink_socket_monitor_open (router_a, &monitor_opts);
    TEST_ASSERT_NOT_NULL (router_a_monitor);

    bind_loopback_ipv4 (router_b, endpoint, sizeof (endpoint));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_connect (router_a, endpoint));

    zlink_routing_id_t rid_b;
    memset (&rid_b, 0, sizeof (rid_b));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_get_routing_id (router_b, &rid_b));
    TEST_ASSERT_TRUE (wait_for_router_envelope_delivery_local (
      router_a, &rid_b, router_b, "router-a", "warmup", 5000));

    TEST_ASSERT_SUCCESS_ERRNO (
      set_router_weight_local (router_b, 0));
    TEST_ASSERT_TRUE (wait_for_socket_peer_weight_event_local (
      router_a_monitor, &rid_b, 0, 5000));

    TEST_ASSERT_TRUE (wait_for_router_submit_result_local (
      router_a, &rid_b, "blocked", ZLINK_SUBMIT_NOT_ADMITTED, 4000));
    TEST_ASSERT_TRUE (wait_for_router_request_result_local (
      router_a, &rid_b, "request-blocked", ZLINK_SUBMIT_NOT_ADMITTED, 4000));

    TEST_ASSERT_SUCCESS_ERRNO (
      set_router_weight_local (router_b, 100));
    TEST_ASSERT_TRUE (wait_for_socket_peer_weight_event_local (
      router_a_monitor, &rid_b, 100, 5000));
    msleep (SETTLE_TIME);
    TEST_ASSERT_TRUE (wait_for_router_envelope_delivery_local (
      router_a, &rid_b, router_b, "router-a", "after-serving", 5000));

    TEST_ASSERT_SUCCESS_ERRNO (zlink_monitor_close (&router_a_monitor));
    test_context_socket_close (router_b);
    test_context_socket_close (router_a);
}

void test_discovery_member_peers_reports_weight_and_service_monitor_event ()
{
    if (!zlink_has ("tcp")) {
        TEST_IGNORE_MESSAGE ("TCP not available");
        return;
    }

    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *registry = zlink_registry_new (ctx);
    TEST_ASSERT_NOT_NULL (registry);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_registry_set_broadcast_interval (registry, 50));

    char registry_pub[MAX_SOCKET_STRING];
    char registry_router[MAX_SOCKET_STRING];
    char router_endpoint[MAX_SOCKET_STRING];
    char dealer_endpoint[MAX_SOCKET_STRING];

    TEST_ASSERT_TRUE (bind_registry_test_endpoints_local (
      registry, registry_pub, sizeof (registry_pub), registry_router,
      sizeof (registry_router)));

    void *router_discovery = zlink_discovery_new (
      ctx, ZLINK_SERVICE_TYPE_SOCKET, "peer-weight-socket");
    void *dealer_discovery = zlink_discovery_new (
      ctx, ZLINK_SERVICE_TYPE_SOCKET, "peer-weight-socket");
    TEST_ASSERT_NOT_NULL (router_discovery);
    TEST_ASSERT_NOT_NULL (dealer_discovery);
    TEST_ASSERT_TRUE (connect_discovery_registry_with_retry_local (
      router_discovery, registry_router, 3000));
    TEST_ASSERT_TRUE (connect_discovery_registry_with_retry_local (
      dealer_discovery, registry_router, 3000));

    zlink_service_monitor_open_options_t service_monitor_opts;
    memset (&service_monitor_opts, 0, sizeof (service_monitor_opts));
    service_monitor_opts.events =
      ZLINK_SERVICE_MONITOR_EVENT_PEER_WEIGHT_CHANGED;
    void *service_monitor =
      zlink_service_monitor_open (dealer_discovery, &service_monitor_opts);
    TEST_ASSERT_NOT_NULL (service_monitor);
    msleep (SETTLE_TIME);

    void *router = zlink_socket (ctx, ZLINK_SOCKET_ROUTER);
    void *dealer = zlink_socket (ctx, ZLINK_SOCKET_DEALER);
    TEST_ASSERT_NOT_NULL (router);
    TEST_ASSERT_NOT_NULL (dealer);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_routing_id (router, "router-da", 9));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_routing_id (dealer, "dealer-da", 9));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_socket_attach_discovery (router, router_discovery));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_socket_attach_discovery (dealer, dealer_discovery));
    TEST_ASSERT_TRUE (bind_socket_test_endpoint_local (
      router, 50160, router_endpoint, sizeof (router_endpoint)));
    TEST_ASSERT_TRUE (bind_socket_test_endpoint_local (
      dealer, 50320, dealer_endpoint, sizeof (dealer_endpoint)));

    zlink_routing_id_t router_rid;
    memset (&router_rid, 0, sizeof (router_rid));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_get_routing_id (router, &router_rid));

    TEST_ASSERT_TRUE (wait_for_discovery_peer_weight_local (
      dealer_discovery, router_endpoint, 100, 5000));

    TEST_ASSERT_SUCCESS_ERRNO (
      set_router_weight_local (router, 0));
    TEST_ASSERT_TRUE (wait_for_discovery_peer_weight_local (
      dealer_discovery, router_endpoint, 0, 5000));
    TEST_ASSERT_TRUE (wait_for_service_peer_weight_event_local (
      service_monitor, router_endpoint, &router_rid, 0,
      5000));

    TEST_ASSERT_SUCCESS_ERRNO (
      set_router_weight_local (router, 100));
    TEST_ASSERT_TRUE (wait_for_discovery_peer_weight_local (
      dealer_discovery, router_endpoint, 100, 5000));
    TEST_ASSERT_TRUE (wait_for_service_peer_weight_event_local (
      service_monitor, router_endpoint, &router_rid, 100,
      5000));

    TEST_ASSERT_SUCCESS_ERRNO (zlink_monitor_close (&service_monitor));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_destroy (&dealer_discovery));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_destroy (&router_discovery));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_destroy (&registry));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

int main ()
{
    setup_test_environment ();

    UNITY_BEGIN ();
    RUN_TEST (test_socket_default_options_are_enabled);
    RUN_TEST (test_invalid_weight_option_is_rejected);
    RUN_TEST (test_dealer_router_weighted_ratio_100_50);
    RUN_TEST (test_dealer_dealer_weighted_ratio_100_50);
    RUN_TEST (test_router_weight_blocks_dealer_outbound_and_emits_monitor_event);
    RUN_TEST (test_router_weight_blocks_router_outbound_and_emits_monitor_event);
    RUN_TEST (
      test_discovery_member_peers_reports_weight_and_service_monitor_event);
    return UNITY_END ();
}
