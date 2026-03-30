/* SPDX-License-Identifier: MPL-2.0 */

#include "../testutil.hpp"
#include "../testutil_unity.hpp"

#include "../src/api/service_api_internal.hpp"

#include <unity.h>

void setUp ()
{
}

void tearDown ()
{
}

namespace
{
void test_spot_subject_access_resolves_composite_and_node_poller_sockets ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *spot = zlink_spot_new (ctx);
    TEST_ASSERT_NOT_NULL (spot);

    spot_handle_t *handle = as_spot_handle (spot);
    TEST_ASSERT_NOT_NULL (handle);
    TEST_ASSERT_NOT_NULL (handle->node);

    TEST_ASSERT_NULL (as_spot_pub_side_handle (spot));
    TEST_ASSERT_NULL (as_spot_sub_side_handle (spot));

    zlink::socket_base_t *spot_pub_socket =
      resolve_spot_pub_subject_poller_socket (spot);
    TEST_ASSERT_NOT_NULL (spot_pub_socket);
    TEST_ASSERT_NOT_NULL (handle->pub);
    TEST_ASSERT_EQUAL_PTR (handle->pub, as_spot_pub_side_handle (handle->pub));
    TEST_ASSERT_EQUAL_PTR (spot_pub_socket, spot_pub_poller_socket (handle->pub));

    zlink::socket_base_t *spot_sub_socket =
      resolve_spot_sub_subject_poller_socket (spot);
    TEST_ASSERT_NOT_NULL (spot_sub_socket);
    TEST_ASSERT_NOT_NULL (handle->sub);
    TEST_ASSERT_EQUAL_PTR (handle->sub, as_spot_sub_side_handle (handle->sub));
    TEST_ASSERT_EQUAL_PTR (spot_sub_socket, spot_sub_poller_socket (handle->sub));

    zlink::socket_base_t *node_pub_socket =
      resolve_spot_pub_subject_poller_socket (handle->node);
    TEST_ASSERT_NOT_NULL (node_pub_socket);

    zlink::socket_base_t *node_sub_socket =
      resolve_spot_sub_subject_poller_socket (handle->node);
    TEST_ASSERT_NOT_NULL (node_sub_socket);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&spot));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

void test_spot_subject_access_routes_subscription_and_routing_state ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *spot = zlink_spot_new (ctx);
    TEST_ASSERT_NOT_NULL (spot);

    spot_handle_t *handle = as_spot_handle (spot);
    TEST_ASSERT_NOT_NULL (handle);

    TEST_ASSERT_NOT_NULL (resolve_spot_pub_subject_poller_socket (spot));
    TEST_ASSERT_NOT_NULL (resolve_spot_sub_subject_poller_socket (spot));
    TEST_ASSERT_NOT_NULL (handle->pub);
    TEST_ASSERT_NOT_NULL (handle->sub);

    const unsigned char rid_bytes[] = {0x11, 0x22, 0x33};
    TEST_ASSERT_SUCCESS_ERRNO (
      spot_subject_set_routing_id (spot, rid_bytes, sizeof (rid_bytes)));

    zlink_routing_id_t rid;
    TEST_ASSERT_SUCCESS_ERRNO (spot_subject_get_routing_id (handle->pub, &rid));
    TEST_ASSERT_EQUAL_UINT8 (sizeof (rid_bytes), rid.size);
    TEST_ASSERT_EQUAL_MEMORY (rid_bytes, rid.data, sizeof (rid_bytes));

    TEST_ASSERT_SUCCESS_ERRNO (spot_subject_set_subscription (spot, "alpha"));
    TEST_ASSERT_SUCCESS_ERRNO (
      spot_subject_set_subscription (handle->sub, "beta*"));

    int topics_count = 0;
    size_t topics_size = sizeof (topics_count);
    TEST_ASSERT_SUCCESS_ERRNO (spot_subject_get_sub_option (
      spot, ZLINK_SUB_OPT_TOPICS_COUNT, &topics_count, &topics_size));
    TEST_ASSERT_EQUAL_INT (2, topics_count);

    char filter[16];
    memset (filter, 0, sizeof (filter));
    size_t filter_len = sizeof (filter);
    int is_pattern = 0;
    TEST_ASSERT_SUCCESS_ERRNO (spot_subject_subscription_at (
      handle->sub, 0, filter, &filter_len, &is_pattern));
    TEST_ASSERT_EQUAL_UINT (5, filter_len);
    TEST_ASSERT_EQUAL_MEMORY ("alpha", filter, filter_len);
    TEST_ASSERT_EQUAL_INT (0, is_pattern);

    memset (filter, 0, sizeof (filter));
    filter_len = sizeof (filter);
    is_pattern = 0;
    TEST_ASSERT_SUCCESS_ERRNO (spot_subject_subscription_at (
      spot, 1, filter, &filter_len, &is_pattern));
    TEST_ASSERT_EQUAL_UINT (5, filter_len);
    TEST_ASSERT_EQUAL_MEMORY ("beta*", filter, filter_len);
    TEST_ASSERT_EQUAL_INT (1, is_pattern);

    TEST_ASSERT_SUCCESS_ERRNO (spot_subject_unset_subscription (spot, "alpha"));
    TEST_ASSERT_SUCCESS_ERRNO (
      spot_subject_unset_subscription (handle->sub, "beta*"));

    topics_size = sizeof (topics_count);
    TEST_ASSERT_SUCCESS_ERRNO (spot_subject_get_sub_option (
      handle->sub, ZLINK_SUB_OPT_TOPICS_COUNT, &topics_count, &topics_size));
    TEST_ASSERT_EQUAL_INT (0, topics_count);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&spot));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

void test_spot_subject_access_routes_composite_and_node_options ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *spot = zlink_spot_new (ctx);
    TEST_ASSERT_NOT_NULL (spot);

    spot_handle_t *handle = as_spot_handle (spot);
    TEST_ASSERT_NOT_NULL (handle);
    TEST_ASSERT_NOT_NULL (handle->node);

    const int linger = 27;
    TEST_ASSERT_SUCCESS_ERRNO (
      spot_subject_set_common_option (spot, ZLINK_OPT_LINGER, &linger,
                                      sizeof (linger)));

    int actual = 0;
    size_t actual_size = sizeof (actual);
    TEST_ASSERT_SUCCESS_ERRNO (spot_subject_get_common_option (
      handle->pub, ZLINK_OPT_LINGER, &actual, &actual_size));
    TEST_ASSERT_EQUAL_INT (linger, actual);

    actual = 0;
    actual_size = sizeof (actual);
    TEST_ASSERT_SUCCESS_ERRNO (spot_subject_get_common_option (
      handle->sub, ZLINK_OPT_LINGER, &actual, &actual_size));
    TEST_ASSERT_EQUAL_INT (linger, actual);

    const int sndtimeo = 44;
    TEST_ASSERT_SUCCESS_ERRNO (
      spot_subject_set_common_option (handle->pub, ZLINK_OPT_SNDTIMEO,
                                      &sndtimeo, sizeof (sndtimeo)));

    actual = 0;
    actual_size = sizeof (actual);
    TEST_ASSERT_SUCCESS_ERRNO (spot_subject_get_common_option (
      spot, ZLINK_OPT_SNDTIMEO, &actual, &actual_size));
    TEST_ASSERT_EQUAL_INT (sndtimeo, actual);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&spot));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}
} // namespace

int main ()
{
    setup_test_environment ();

    UNITY_BEGIN ();
    RUN_TEST (test_spot_subject_access_resolves_composite_and_node_poller_sockets);
    RUN_TEST (test_spot_subject_access_routes_subscription_and_routing_state);
    RUN_TEST (test_spot_subject_access_routes_composite_and_node_options);
    return UNITY_END ();
}
