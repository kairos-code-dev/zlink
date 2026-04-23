/* SPDX-License-Identifier: MPL-2.0 */

#include "../testutil_unity.hpp"

#include "api/service_spot_request_reply_internal.hpp"
#include "api/service_spot_request_reply_utils_internal.hpp"

#include <chrono>
#include <memory>
#include <string>
#include <string.h>
#include <thread>

namespace
{
void test_spot_new_keeps_routed_recv_lazy_until_first_recv ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_ctx_set (ctx, ZLINK_MAX_SOCKETS, 128));

    void *node = zlink_spot_node_new (ctx);
    TEST_ASSERT_NOT_NULL (node);

    void *spot = zlink_spot_new (node);
    TEST_ASSERT_NOT_NULL (spot);

    std::shared_ptr<zlink::spot_reqrep_internal::spot_request_reply_state_t> state =
      zlink::spot_reqrep_internal::try_find_spot_state (spot);
    TEST_ASSERT_NOT_NULL (state.get ());
    TEST_ASSERT_NULL (state->recv.routed_recv_socket);
    TEST_ASSERT_NULL (state->recv.routed_recv_queue.signal.rx);
    TEST_ASSERT_NULL (state->recv.routed_recv_queue.signal.tx);
    TEST_ASSERT_NULL (
      zlink::spot_reqrep_internal::spot_completion_signal_socket (state));

    zlink_msg_t part;
    zlink_msg_init (&part);
    const zlink_routing_id_t *source_node_rid = NULL;
    const zlink_routing_id_t *source_spot_rid = NULL;
    uint64_t request_seq = 0;
    zlink_part_flag_t has_more = ZLINK_PART_FINAL;
    const zlink_recv_result_t rc = zlink_spot_recv_part (
      spot, &source_node_rid, &source_spot_rid, &request_seq, &part,
      &has_more, ZLINK_RECV_FLAGS_DONTWAIT);
    TEST_ASSERT_EQUAL_INT (ZLINK_RECV_NO_DATA, rc);
    TEST_ASSERT_EQUAL_INT (EAGAIN, zlink_errno ());
    zlink_msg_close (&part);

    state = zlink::spot_reqrep_internal::try_find_spot_state (spot);
    TEST_ASSERT_NOT_NULL (state.get ());
    TEST_ASSERT_NOT_NULL (state->recv.routed_recv_socket);
    TEST_ASSERT_NOT_NULL (state->recv.routed_recv_queue.signal.rx);
    TEST_ASSERT_NOT_NULL (state->recv.routed_recv_queue.signal.tx);
    TEST_ASSERT_EQUAL_PTR (state->recv.routed_recv_queue.signal.rx,
                           state->recv.routed_recv_socket);
    TEST_ASSERT_NOT_NULL (
      zlink::spot_reqrep_internal::spot_completion_signal_socket (state));

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&spot));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

void init_string_part_local (zlink_msg_t *part_, const char *text_)
{
    const size_t size = strlen (text_);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_init_size (part_, size));
    memcpy (zlink_msg_data (part_), text_, size);
}

void test_spot_destroy_cleans_identity_with_unread_message ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_ctx_set (ctx, ZLINK_MAX_SOCKETS, 128));

    void *node = zlink_spot_node_new (ctx);
    TEST_ASSERT_NOT_NULL (node);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_routing_id (node, "node-activation",
                            strlen ("node-activation")));

    void *spot = zlink_spot_new (node);
    TEST_ASSERT_NOT_NULL (spot);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_routing_id (spot, "spot-unread", strlen ("spot-unread")));
    TEST_ASSERT_NOT_NULL (
      zlink::spot_reqrep_internal::find_or_create_spot_state (spot).get ());

    zlink::spot_reqrep_internal::routing_pair_t identity;
    TEST_ASSERT_TRUE (
      zlink::spot_reqrep_internal::resolve_spot_identity (spot, &identity));

    zlink_routing_id_t node_rid;
    zlink_routing_id_t spot_rid;
    memset (&node_rid, 0, sizeof (node_rid));
    memset (&spot_rid, 0, sizeof (spot_rid));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_get_routing_id (node, &node_rid));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_get_routing_id (spot, &spot_rid));

    void *router = zlink_socket (ctx, ZLINK_SOCKET_ROUTER);
    TEST_ASSERT_NOT_NULL (router);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_routing_id (router, "router-unread", strlen ("router-unread")));

    zlink_msg_t part;
    init_string_part_local (&part, "pending");
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_router_send_spot (router, &node_rid, &spot_rid, &part, 1, 0));
    std::this_thread::sleep_for (std::chrono::milliseconds (50));

    TEST_ASSERT_NOT_NULL (
      zlink::spot_reqrep_internal::find_spot_state_by_identity (
        identity.node_rid, identity.spot_rid)
        .get ());

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&spot));
    TEST_ASSERT_NULL (
      zlink::spot_reqrep_internal::find_spot_state_by_identity (
        identity.node_rid, identity.spot_rid)
        .get ());

    TEST_ASSERT_SUCCESS_ERRNO (zlink_close (router));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}
}

int main (int argc, char **argv)
{
    UNITY_BEGIN ();
    RUN_TEST (test_spot_new_keeps_routed_recv_lazy_until_first_recv);
    RUN_TEST (test_spot_destroy_cleans_identity_with_unread_message);
    return UNITY_END ();
}
