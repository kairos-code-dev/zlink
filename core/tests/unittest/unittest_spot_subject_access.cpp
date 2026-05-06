/* SPDX-License-Identifier: MPL-2.0 */

#include "../testutil.hpp"
#include "../testutil_unity.hpp"

#include "../src/api/service_api_internal.hpp"

#include <unity.h>

#include <cerrno>
#include <cstring>
#include <vector>

void setUp ()
{
}

void tearDown ()
{
}

namespace
{
void noop_spot_handler (const zlink_routing_id_t *,
                        const zlink_routing_id_t *,
                        uint64_t,
                        zlink_msg_t *,
                        size_t,
                        void *)
{
}

void noop_reply_handler (zlink_request_result_t,
                         zlink_msg_t *,
                         size_t,
                         void *)
{
}

std::vector<zlink_spot_node_socket_snapshot_entry_t>
read_socket_snapshot (void *node_)
{
    size_t count = 0;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_internal_sockets_snapshot (node_, NULL, NULL, &count));

    std::vector<zlink_spot_node_socket_snapshot_entry_t> rows (count);
    if (count != 0) {
        TEST_ASSERT_SUCCESS_ERRNO (
          zlink_spot_node_internal_sockets_snapshot (node_, NULL, &rows[0],
                                                     &count));
        rows.resize (count);
    }
    return rows;
}

bool snapshot_has_socket (void *node_, const char *socket_name_)
{
    std::vector<zlink_spot_node_socket_snapshot_entry_t> rows =
      read_socket_snapshot (node_);
    for (size_t i = 0; i < rows.size (); ++i) {
        if (strcmp (rows[i].socket_name, socket_name_) == 0)
            return true;
    }
    return false;
}

bool snapshot_has_owner_socket (void *node_,
                                zlink_spot_node_socket_owner_t owner_,
                                const char *socket_name_)
{
    std::vector<zlink_spot_node_socket_snapshot_entry_t> rows =
      read_socket_snapshot (node_);
    for (size_t i = 0; i < rows.size (); ++i) {
        if (rows[i].owner == owner_
            && strcmp (rows[i].socket_name, socket_name_) == 0) {
            return true;
        }
    }
    return false;
}

size_t count_owner_rows (
  const std::vector<zlink_spot_node_socket_snapshot_entry_t> &rows_,
  zlink_spot_node_socket_owner_t owner_)
{
    size_t count = 0;
    for (size_t i = 0; i < rows_.size (); ++i) {
        if (rows_[i].owner == owner_)
            ++count;
    }
    return count;
}

size_t count_socket_rows (
  const std::vector<zlink_spot_node_socket_snapshot_entry_t> &rows_,
  zlink_socket_type_t socket_type_)
{
    size_t count = 0;
    for (size_t i = 0; i < rows_.size (); ++i) {
        if (rows_[i].socket_type == socket_type_)
            ++count;
    }
    return count;
}

size_t count_socket_name_rows (
  const std::vector<zlink_spot_node_socket_snapshot_entry_t> &rows_,
  const char *socket_name_)
{
    size_t count = 0;
    for (size_t i = 0; i < rows_.size (); ++i) {
        if (strcmp (rows_[i].socket_name, socket_name_) == 0)
            ++count;
    }
    return count;
}

const zlink_spot_node_socket_snapshot_entry_t *find_socket_row (
  const std::vector<zlink_spot_node_socket_snapshot_entry_t> &rows_,
  zlink_spot_node_socket_owner_t owner_,
  const char *socket_name_)
{
    for (size_t i = 0; i < rows_.size (); ++i) {
        if (rows_[i].owner == owner_
            && strcmp (rows_[i].socket_name, socket_name_) == 0) {
            return &rows_[i];
        }
    }
    return NULL;
}

void test_spot_node_options_default_zero_and_invalid_mode ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *default_node = zlink_spot_node_new (ctx, NULL);
    TEST_ASSERT_NOT_NULL (default_node);
    TEST_ASSERT_TRUE (snapshot_has_socket (default_node, "mesh-pub"));
    TEST_ASSERT_TRUE (snapshot_has_socket (default_node, "internal-router"));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&default_node));

    zlink_spot_node_options_t zero_options;
    memset (&zero_options, 0, sizeof (zero_options));
    void *zero_node = zlink_spot_node_new (ctx, &zero_options);
    TEST_ASSERT_NOT_NULL (zero_node);
    TEST_ASSERT_TRUE (snapshot_has_socket (zero_node, "mesh-pub"));
    TEST_ASSERT_TRUE (snapshot_has_socket (zero_node, "internal-router"));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&zero_node));

    zlink_spot_node_options_t invalid_options;
    memset (&invalid_options, 0, sizeof (invalid_options));
    invalid_options.mode = static_cast<zlink_spot_node_mode_t> (99);
    errno = 0;
    void *invalid_node = zlink_spot_node_new (ctx, &invalid_options);
    TEST_ASSERT_NULL (invalid_node);
    TEST_ASSERT_EQUAL_INT (EINVAL, errno);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

void test_spot_node_pubsub_mode_disables_routed_sockets_without_lazy_create ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    zlink_spot_node_options_t options;
    memset (&options, 0, sizeof (options));
    options.mode = ZLINK_SPOT_NODE_MODE_PUBSUB;
    void *node = zlink_spot_node_new (ctx, &options);
    TEST_ASSERT_NOT_NULL (node);
    TEST_ASSERT_TRUE (snapshot_has_socket (node, "mesh-pub"));
    TEST_ASSERT_FALSE (snapshot_has_socket (node, "internal-router"));
    TEST_ASSERT_FALSE (snapshot_has_socket (node, "external-router"));

    void *spot = zlink_spot_new (node);
    TEST_ASSERT_NOT_NULL (spot);
    const size_t count_before = read_socket_snapshot (node).size ();
    TEST_ASSERT_TRUE (snapshot_has_owner_socket (
      node, ZLINK_SPOT_NODE_SOCKET_OWNER_SPOT, "pub"));
    TEST_ASSERT_TRUE (snapshot_has_owner_socket (
      node, ZLINK_SPOT_NODE_SOCKET_OWNER_SPOT, "sub"));
    errno = 0;
    TEST_ASSERT_EQUAL_INT (
      ZLINK_HANDLER_NOT_SUPPORTED,
      zlink_spot_handler (spot, &noop_spot_handler, NULL));
    TEST_ASSERT_EQUAL_INT (ENOTSUP, errno);
    TEST_ASSERT_EQUAL_UINT (count_before, read_socket_snapshot (node).size ());

    zlink_routing_id_t rid;
    memset (&rid, 0, sizeof (rid));
    rid.size = 1;
    rid.data[0] = 1;
    zlink_msg_t msg;
    zlink_msg_init (&msg);
    errno = 0;
    TEST_ASSERT_EQUAL_INT (
      ZLINK_SUBMIT_NOT_SUPPORTED,
      zlink_spot_request_spot_part (spot, &rid, &rid, &msg,
                                    &noop_reply_handler, NULL, ZLINK_DONTWAIT,
                                    ZLINK_PART_FINAL, 1));
    TEST_ASSERT_EQUAL_INT (ENOTSUP, errno);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_close (&msg));
    TEST_ASSERT_EQUAL_UINT (count_before, read_socket_snapshot (node).size ());

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&spot));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

void test_spot_node_routed_mode_disables_pubsub_sockets_without_lazy_create ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    zlink_spot_node_options_t options;
    memset (&options, 0, sizeof (options));
    options.mode = ZLINK_SPOT_NODE_MODE_ROUTED;
    void *node = zlink_spot_node_new (ctx, &options);
    TEST_ASSERT_NOT_NULL (node);
    TEST_ASSERT_TRUE (snapshot_has_socket (node, "internal-router"));
    TEST_ASSERT_TRUE (snapshot_has_socket (node, "external-router"));
    TEST_ASSERT_FALSE (snapshot_has_socket (node, "mesh-pub"));
    TEST_ASSERT_FALSE (snapshot_has_socket (node, "ingress-sub"));

    const size_t count_before = read_socket_snapshot (node).size ();
    void *spot = zlink_spot_new (node);
    TEST_ASSERT_NOT_NULL (spot);
    TEST_ASSERT_FAILURE_RAW_ERRNO (ENOTSUP,
                                   spot_subject_set_subscription (spot,
                                                                  "alpha"));
    TEST_ASSERT_FALSE (snapshot_has_owner_socket (
      node, ZLINK_SPOT_NODE_SOCKET_OWNER_SPOT, "pub"));
    TEST_ASSERT_FALSE (snapshot_has_owner_socket (
      node, ZLINK_SPOT_NODE_SOCKET_OWNER_SPOT, "sub"));
    TEST_ASSERT_EQUAL_UINT (count_before, read_socket_snapshot (node).size ());

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&spot));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

void test_spot_node_internal_socket_snapshot_contract ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *node = zlink_spot_node_new (ctx, NULL);
    TEST_ASSERT_NOT_NULL (node);
    void *spot = zlink_spot_new (node);
    TEST_ASSERT_NOT_NULL (spot);

    errno = 0;
    TEST_ASSERT_EQUAL_INT (
      ZLINK_CONFIG_INVALID_ARGUMENT,
      zlink_spot_node_internal_sockets_snapshot (node, NULL, NULL, NULL));
    TEST_ASSERT_EQUAL_INT (EINVAL, errno);

    size_t count = 0;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_internal_sockets_snapshot (node, NULL, NULL, &count));
    TEST_ASSERT_TRUE (count > 0);

    std::vector<zlink_spot_node_socket_snapshot_entry_t> one (1);
    size_t small_count = 0;
    errno = 0;
    TEST_ASSERT_EQUAL_INT (
      ZLINK_CONFIG_INTERNAL_ERROR,
      zlink_spot_node_internal_sockets_snapshot (node, NULL, &one[0],
                                                 &small_count));
    TEST_ASSERT_EQUAL_INT (ENOBUFS, errno);
    TEST_ASSERT_EQUAL_UINT (count, small_count);

    zlink_spot_node_socket_snapshot_filter_t filter;
    memset (&filter, 0, sizeof (filter));
    filter.owner = static_cast<zlink_spot_node_socket_owner_t> (99);
    errno = 0;
    TEST_ASSERT_EQUAL_INT (
      ZLINK_CONFIG_INVALID_ARGUMENT,
      zlink_spot_node_internal_sockets_snapshot (node, &filter, NULL, &count));
    TEST_ASSERT_EQUAL_INT (EINVAL, errno);

    memset (&filter, 0, sizeof (filter));
    filter.socket_type = static_cast<zlink_socket_type_t> (99);
    errno = 0;
    TEST_ASSERT_EQUAL_INT (
      ZLINK_CONFIG_INVALID_ARGUMENT,
      zlink_spot_node_internal_sockets_snapshot (node, &filter, NULL, &count));
    TEST_ASSERT_EQUAL_INT (EINVAL, errno);

    memset (&filter, 0, sizeof (filter));
    memset (filter.socket_name, 'x', sizeof (filter.socket_name));
    errno = 0;
    TEST_ASSERT_EQUAL_INT (
      ZLINK_CONFIG_INVALID_ARGUMENT,
      zlink_spot_node_internal_sockets_snapshot (node, &filter, NULL, &count));
    TEST_ASSERT_EQUAL_INT (EINVAL, errno);

    const std::vector<zlink_spot_node_socket_snapshot_entry_t> all_rows =
      read_socket_snapshot (node);
    TEST_ASSERT_TRUE (
      count_owner_rows (all_rows, ZLINK_SPOT_NODE_SOCKET_OWNER_NODE) > 0);
    TEST_ASSERT_TRUE (
      count_owner_rows (all_rows, ZLINK_SPOT_NODE_SOCKET_OWNER_SPOT) > 0);

    for (size_t i = 0; i < all_rows.size (); ++i) {
        TEST_ASSERT_TRUE (
          all_rows[i].socket_type == ZLINK_SOCKET_PUB
          || all_rows[i].socket_type == ZLINK_SOCKET_SUB
          || all_rows[i].socket_type == ZLINK_SOCKET_XPUB
          || all_rows[i].socket_type == ZLINK_SOCKET_XSUB
          || all_rows[i].socket_type == ZLINK_SOCKET_DEALER
          || all_rows[i].socket_type == ZLINK_SOCKET_ROUTER
          || all_rows[i].socket_type == ZLINK_SOCKET_STREAM
          || all_rows[i].socket_type == ZLINK_SOCKET_PAIR);
        TEST_ASSERT_TRUE (all_rows[i].snapshot.source_kind
                          == ZLINK_MONITOR_SOURCE_SOCKET
                          || all_rows[i].snapshot.source_kind
                               == ZLINK_MONITOR_SOURCE_SPOT_PUB
                          || all_rows[i].snapshot.source_kind
                               == ZLINK_MONITOR_SOURCE_SPOT_SUB);
    }

    memset (&filter, 0, sizeof (filter));
    filter.owner = ZLINK_SPOT_NODE_SOCKET_OWNER_NODE;
    count = 0;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_internal_sockets_snapshot (node, &filter, NULL, &count));
    TEST_ASSERT_EQUAL_UINT (
      count_owner_rows (all_rows, ZLINK_SPOT_NODE_SOCKET_OWNER_NODE), count);

    memset (&filter, 0, sizeof (filter));
    filter.socket_type = ZLINK_SOCKET_ROUTER;
    count = 0;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_internal_sockets_snapshot (node, &filter, NULL, &count));
    TEST_ASSERT_EQUAL_UINT (count_socket_rows (all_rows, ZLINK_SOCKET_ROUTER),
                            count);

    memset (&filter, 0, sizeof (filter));
    snprintf (filter.socket_name, sizeof (filter.socket_name), "%s",
              "internal-router");
    count = 0;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_internal_sockets_snapshot (node, &filter, NULL, &count));
    TEST_ASSERT_EQUAL_UINT (count_socket_name_rows (all_rows, "internal-router"),
                            count);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&spot));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

void test_spot_node_admission_hwm_socket_snapshot_contract ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *node = zlink_spot_node_new (ctx, NULL);
    TEST_ASSERT_NOT_NULL (node);

    int router_hwm = 23;
    int pubsub_hwm = 17;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_spot_node_option (node, ZLINK_SPOT_NODE_OPT_ROUTER_HWM,
                                  &router_hwm, sizeof (router_hwm)));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_spot_node_option (node, ZLINK_SPOT_NODE_OPT_PUBSUB_HWM,
                                  &pubsub_hwm, sizeof (pubsub_hwm)));

    void *spot = zlink_spot_new (node);
    TEST_ASSERT_NOT_NULL (spot);
    TEST_ASSERT_NOT_NULL (resolve_spot_pub_subject_poller_socket (spot));
    TEST_ASSERT_NOT_NULL (resolve_spot_sub_subject_poller_socket (spot));

    const std::vector<zlink_spot_node_socket_snapshot_entry_t> rows =
      read_socket_snapshot (node);

    const zlink_spot_node_socket_snapshot_entry_t *spot_pub =
      find_socket_row (rows, ZLINK_SPOT_NODE_SOCKET_OWNER_SPOT, "pub");
    const zlink_spot_node_socket_snapshot_entry_t *spot_sub =
      find_socket_row (rows, ZLINK_SPOT_NODE_SOCKET_OWNER_SPOT, "sub");
    const zlink_spot_node_socket_snapshot_entry_t *ingress =
      find_socket_row (rows, ZLINK_SPOT_NODE_SOCKET_OWNER_NODE, "ingress-sub");
    const zlink_spot_node_socket_snapshot_entry_t *fanout =
      find_socket_row (rows, ZLINK_SPOT_NODE_SOCKET_OWNER_NODE, "local-pub");
    const zlink_spot_node_socket_snapshot_entry_t *mesh_pub =
      find_socket_row (rows, ZLINK_SPOT_NODE_SOCKET_OWNER_NODE, "mesh-pub");
    const zlink_spot_node_socket_snapshot_entry_t *mesh_xsub =
      find_socket_row (rows, ZLINK_SPOT_NODE_SOCKET_OWNER_NODE, "mesh-xsub");
    const zlink_spot_node_socket_snapshot_entry_t *internal_router =
      find_socket_row (rows, ZLINK_SPOT_NODE_SOCKET_OWNER_NODE,
                       "internal-router");
    const zlink_spot_node_socket_snapshot_entry_t *external_router =
      find_socket_row (rows, ZLINK_SPOT_NODE_SOCKET_OWNER_NODE,
                       "external-router");

    TEST_ASSERT_NOT_NULL (spot_pub);
    TEST_ASSERT_NOT_NULL (spot_sub);
    TEST_ASSERT_NOT_NULL (ingress);
    TEST_ASSERT_NOT_NULL (fanout);
    TEST_ASSERT_NOT_NULL (mesh_pub);
    TEST_ASSERT_NOT_NULL (mesh_xsub);
    TEST_ASSERT_NOT_NULL (internal_router);
    TEST_ASSERT_NOT_NULL (external_router);

    TEST_ASSERT_EQUAL_INT (pubsub_hwm,
                           spot_pub->snapshot.auto_hwm_applied_sndhwm);
    TEST_ASSERT_EQUAL_INT (0, spot_pub->snapshot.auto_hwm_applied_rcvhwm);
    TEST_ASSERT_EQUAL_INT (0, spot_sub->snapshot.auto_hwm_applied_rcvhwm);
    TEST_ASSERT_EQUAL_INT (pubsub_hwm,
                           ingress->snapshot.auto_hwm_applied_rcvhwm);
    TEST_ASSERT_EQUAL_INT (0, fanout->snapshot.auto_hwm_applied_sndhwm);
    TEST_ASSERT_EQUAL_INT (0, mesh_pub->snapshot.auto_hwm_applied_sndhwm);
    TEST_ASSERT_EQUAL_INT (0, mesh_xsub->snapshot.auto_hwm_applied_rcvhwm);

    TEST_ASSERT_EQUAL_INT (router_hwm,
                           internal_router->snapshot.auto_hwm_applied_rcvhwm);
    TEST_ASSERT_EQUAL_INT (0,
                           internal_router->snapshot.auto_hwm_applied_sndhwm);
    TEST_ASSERT_EQUAL_INT (0,
                           external_router->snapshot.auto_hwm_applied_rcvhwm);
    TEST_ASSERT_EQUAL_INT (0,
                           external_router->snapshot.auto_hwm_applied_sndhwm);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&spot));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

void test_spot_node_admission_hwm_changes_apply_to_new_spots_only ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *node = zlink_spot_node_new (ctx, NULL);
    TEST_ASSERT_NOT_NULL (node);

    void *old_spot = zlink_spot_new (node);
    TEST_ASSERT_NOT_NULL (old_spot);
    TEST_ASSERT_NOT_NULL (resolve_spot_pub_subject_poller_socket (old_spot));

    int pubsub_hwm = 21;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_spot_node_option (node, ZLINK_SPOT_NODE_OPT_PUBSUB_HWM,
                                  &pubsub_hwm, sizeof (pubsub_hwm)));

    void *new_spot = zlink_spot_new (node);
    TEST_ASSERT_NOT_NULL (new_spot);
    TEST_ASSERT_NOT_NULL (resolve_spot_pub_subject_poller_socket (new_spot));

    const std::vector<zlink_spot_node_socket_snapshot_entry_t> rows =
      read_socket_snapshot (node);
    bool saw_old_hwm = false;
    bool saw_new_hwm = false;
    for (size_t i = 0; i < rows.size (); ++i) {
        if (rows[i].owner != ZLINK_SPOT_NODE_SOCKET_OWNER_SPOT
            || strcmp (rows[i].socket_name, "pub") != 0)
            continue;
        if (rows[i].snapshot.auto_hwm_applied_sndhwm == 16)
            saw_old_hwm = true;
        if (rows[i].snapshot.auto_hwm_applied_sndhwm == pubsub_hwm)
            saw_new_hwm = true;
    }

    TEST_ASSERT_TRUE (saw_old_hwm);
    TEST_ASSERT_TRUE (saw_new_hwm);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&new_spot));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&old_spot));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

void test_spot_subject_access_resolves_composite_and_node_poller_sockets ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *node = zlink_spot_node_new (ctx, NULL);
    TEST_ASSERT_NOT_NULL (node);
    void *spot = zlink_spot_new (node);
    TEST_ASSERT_NOT_NULL (spot);

    spot_handle_t *handle = as_spot_handle (spot);
    TEST_ASSERT_NOT_NULL (handle);
    TEST_ASSERT_NOT_NULL (handle->node);
    TEST_ASSERT_NOT_NULL (handle->logical_state);

    TEST_ASSERT_NULL (as_spot_pub_side_handle (spot));
    TEST_ASSERT_NULL (as_spot_sub_side_handle (spot));

    zlink::socket_base_t *spot_pub_socket =
      resolve_spot_pub_subject_poller_socket (spot);
    TEST_ASSERT_NOT_NULL (spot_pub_socket);
    TEST_ASSERT_NOT_NULL (handle->logical_state->pub);
    TEST_ASSERT_EQUAL_PTR (
      handle->logical_state->pub,
      as_spot_pub_side_handle (handle->logical_state->pub));
    TEST_ASSERT_EQUAL_PTR (
      spot_pub_socket, spot_pub_poller_socket (handle->logical_state->pub));

    zlink::socket_base_t *spot_sub_socket =
      resolve_spot_sub_subject_poller_socket (spot);
    TEST_ASSERT_NOT_NULL (spot_sub_socket);
    TEST_ASSERT_NOT_NULL (handle->logical_state->sub);
    TEST_ASSERT_EQUAL_PTR (
      handle->logical_state->sub,
      as_spot_sub_side_handle (handle->logical_state->sub));
    TEST_ASSERT_EQUAL_PTR (
      spot_sub_socket, spot_sub_poller_socket (handle->logical_state->sub));

    zlink::socket_base_t *node_pub_socket =
      resolve_spot_pub_subject_poller_socket (handle->node);
    TEST_ASSERT_NULL (node_pub_socket);
    TEST_ASSERT_EQUAL_INT (ENOTSUP, zlink_errno ());

    zlink::socket_base_t *node_sub_socket =
      resolve_spot_sub_subject_poller_socket (handle->node);
    TEST_ASSERT_NOT_NULL (node_sub_socket);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&spot));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

void test_spot_subject_access_routes_subscription_and_routing_state ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *node = zlink_spot_node_new (ctx, NULL);
    TEST_ASSERT_NOT_NULL (node);
    void *spot = zlink_spot_new (node);
    TEST_ASSERT_NOT_NULL (spot);

    spot_handle_t *handle = as_spot_handle (spot);
    TEST_ASSERT_NOT_NULL (handle);
    TEST_ASSERT_NOT_NULL (handle->logical_state);

    TEST_ASSERT_NOT_NULL (resolve_spot_pub_subject_poller_socket (spot));
    TEST_ASSERT_NOT_NULL (resolve_spot_sub_subject_poller_socket (spot));
    TEST_ASSERT_NOT_NULL (handle->logical_state->pub);
    TEST_ASSERT_NOT_NULL (handle->logical_state->sub);

    const unsigned char rid_bytes[] = {0x11, 0x22, 0x33};
    TEST_ASSERT_SUCCESS_ERRNO (
      spot_subject_set_routing_id (spot, rid_bytes, sizeof (rid_bytes)));

    zlink_routing_id_t rid;
    TEST_ASSERT_SUCCESS_ERRNO (
      spot_subject_get_routing_id (handle->logical_state->pub, &rid));
    TEST_ASSERT_EQUAL_UINT8 (sizeof (rid_bytes), rid.size);
    TEST_ASSERT_EQUAL_MEMORY (rid_bytes, rid.data, sizeof (rid_bytes));

    TEST_ASSERT_SUCCESS_ERRNO (spot_subject_set_subscription (spot, "alpha"));
    TEST_ASSERT_SUCCESS_ERRNO (
      spot_subject_set_subscription (handle->logical_state->sub, "beta*"));

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
      handle->logical_state->sub, 0, filter, &filter_len, &is_pattern));
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
      spot_subject_unset_subscription (handle->logical_state->sub, "beta*"));

    topics_size = sizeof (topics_count);
    TEST_ASSERT_SUCCESS_ERRNO (spot_subject_get_sub_option (
      handle->logical_state->sub, ZLINK_SUB_OPT_TOPICS_COUNT, &topics_count,
      &topics_size));
    TEST_ASSERT_EQUAL_INT (0, topics_count);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&spot));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

void test_spot_subject_access_routes_composite_and_node_options ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *node = zlink_spot_node_new (ctx, NULL);
    TEST_ASSERT_NOT_NULL (node);
    void *spot = zlink_spot_new (node);
    TEST_ASSERT_NOT_NULL (spot);

    spot_handle_t *handle = as_spot_handle (spot);
    TEST_ASSERT_NOT_NULL (handle);
    TEST_ASSERT_NOT_NULL (handle->node);
    TEST_ASSERT_NOT_NULL (handle->logical_state);
    TEST_ASSERT_NOT_NULL (handle->logical_state->pub);

    const int linger = 27;
    TEST_ASSERT_SUCCESS_ERRNO (
      spot_subject_set_common_option (spot, ZLINK_OPT_LINGER, &linger,
                                      sizeof (linger)));
    TEST_ASSERT_NOT_NULL (handle->logical_state->pub);

    TEST_ASSERT_NOT_NULL (resolve_spot_pub_subject_poller_socket (spot));
    TEST_ASSERT_NOT_NULL (resolve_spot_sub_subject_poller_socket (spot));
    TEST_ASSERT_NOT_NULL (handle->logical_state->pub);
    TEST_ASSERT_NOT_NULL (handle->logical_state->sub);

    int actual = 0;
    size_t actual_size = sizeof (actual);
    TEST_ASSERT_SUCCESS_ERRNO (spot_subject_get_common_option (
      handle->logical_state->pub, ZLINK_OPT_LINGER, &actual, &actual_size));
    TEST_ASSERT_EQUAL_INT (linger, actual);

    actual = 0;
    actual_size = sizeof (actual);
    TEST_ASSERT_SUCCESS_ERRNO (spot_subject_get_common_option (
      handle->logical_state->sub, ZLINK_OPT_LINGER, &actual, &actual_size));
    TEST_ASSERT_EQUAL_INT (linger, actual);

    const int sndtimeo = 44;
    TEST_ASSERT_SUCCESS_ERRNO (
      spot_subject_set_common_option (handle->logical_state->pub, ZLINK_OPT_SNDTIMEO,
                                      &sndtimeo, sizeof (sndtimeo)));

    actual = 0;
    actual_size = sizeof (actual);
    TEST_ASSERT_SUCCESS_ERRNO (spot_subject_get_common_option (
      spot, ZLINK_OPT_SNDTIMEO, &actual, &actual_size));
    TEST_ASSERT_EQUAL_INT (sndtimeo, actual);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&spot));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

void test_spot_subject_access_applies_pending_pub_defaults_on_lazy_create ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *node = zlink_spot_node_new (ctx, NULL);
    TEST_ASSERT_NOT_NULL (node);
    void *spot = zlink_spot_new (node);
    TEST_ASSERT_NOT_NULL (spot);

    spot_handle_t *handle = as_spot_handle (spot);
    TEST_ASSERT_NOT_NULL (handle);
    TEST_ASSERT_NOT_NULL (handle->logical_state);
    TEST_ASSERT_NOT_NULL (handle->logical_state->pub);

    const int sndhwm = 321;
    const int sndtimeo = 654;
    const int nodrop = 1;
    TEST_ASSERT_EQUAL_INT (
      -1, spot_subject_set_common_option (spot, ZLINK_OPT_SNDHWM, &sndhwm,
                                          sizeof (sndhwm)));
    TEST_ASSERT_EQUAL_INT (EINVAL, errno);
    TEST_ASSERT_SUCCESS_ERRNO (
      spot_subject_set_common_option (spot, ZLINK_OPT_SNDTIMEO, &sndtimeo,
                                      sizeof (sndtimeo)));
    TEST_ASSERT_SUCCESS_ERRNO (
      spot_subject_set_pub_option (spot, ZLINK_PUB_OPT_NODROP, &nodrop,
                                   sizeof (nodrop)));
    TEST_ASSERT_NOT_NULL (handle->logical_state->pub);
    TEST_ASSERT_NOT_NULL (resolve_spot_pub_subject_poller_socket (spot));

    int actual = 0;
    size_t actual_size = sizeof (actual);
    TEST_ASSERT_EQUAL_INT (
      -1, spot_subject_get_common_option (spot, ZLINK_OPT_SNDHWM, &actual,
                                          &actual_size));
    TEST_ASSERT_EQUAL_INT (EINVAL, errno);

    actual = 0;
    actual_size = sizeof (actual);
    TEST_ASSERT_SUCCESS_ERRNO (spot_subject_get_common_option (
      spot, ZLINK_OPT_SNDTIMEO, &actual, &actual_size));
    TEST_ASSERT_EQUAL_INT (sndtimeo, actual);

    actual = 0;
    actual_size = sizeof (actual);
    TEST_ASSERT_SUCCESS_ERRNO (spot_subject_get_pub_option (
      spot, ZLINK_PUB_OPT_NODROP, &actual, &actual_size));
    TEST_ASSERT_EQUAL_INT (nodrop, actual);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&spot));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}
} // namespace

int main ()
{
    setup_test_environment ();

    UNITY_BEGIN ();
    RUN_TEST (test_spot_node_options_default_zero_and_invalid_mode);
    RUN_TEST (
      test_spot_node_pubsub_mode_disables_routed_sockets_without_lazy_create);
    RUN_TEST (
      test_spot_node_routed_mode_disables_pubsub_sockets_without_lazy_create);
    RUN_TEST (test_spot_node_internal_socket_snapshot_contract);
    RUN_TEST (test_spot_node_admission_hwm_socket_snapshot_contract);
    RUN_TEST (test_spot_node_admission_hwm_changes_apply_to_new_spots_only);
    RUN_TEST (test_spot_subject_access_resolves_composite_and_node_poller_sockets);
    RUN_TEST (test_spot_subject_access_routes_subscription_and_routing_state);
    RUN_TEST (test_spot_subject_access_routes_composite_and_node_options);
    RUN_TEST (test_spot_subject_access_applies_pending_pub_defaults_on_lazy_create);
    return UNITY_END ();
}
