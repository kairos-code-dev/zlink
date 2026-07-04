/* SPDX-License-Identifier: MPL-2.0 */

#include "../testutil_unity.hpp"

#include "runtime/services/actor/gateway/service_spot_actor_gateway_protocol_internal.hpp"

#include <string.h>

namespace
{
zlink_routing_id_t make_rid (const char *value_)
{
    zlink_routing_id_t rid;
    memset (&rid, 0, sizeof (rid));
    rid.size = static_cast<uint8_t> (strlen (value_));
    memcpy (rid.data, value_, rid.size);
    return rid;
}

void test_no_bind_control_frame_round_trips_request_metadata ()
{
    zlink_routing_id_t caller = make_rid ("caller-endpoint");
    zlink_msg_t control;
    TEST_ASSERT_TRUE (zlink::spot_actor_gateway::init_control_msg (
      zlink::spot_actor_gateway::packet_server_to_actor_no_bind, caller, "actor-a", 77,
      ZLINK_PART_FINAL, &control, 0x0102030405060708ULL, 1));

    zlink::spot_actor_gateway::frame_t parsed;
    TEST_ASSERT_TRUE (zlink::spot_actor_gateway::parse_control_msg (&control, &parsed));
    TEST_ASSERT_EQUAL_UINT8 (zlink::spot_actor_gateway::packet_server_to_actor_no_bind,
                             parsed.kind);
    TEST_ASSERT_EQUAL_UINT8 (ZLINK_PART_FINAL, parsed.part_flag);
    TEST_ASSERT_EQUAL_UINT64 (77, parsed.generation);
    TEST_ASSERT_EQUAL_UINT64 (0x0102030405060708ULL, parsed.request_id);
    TEST_ASSERT_EQUAL_INT32 (1, parsed.join_result_code);
    TEST_ASSERT_EQUAL_STRING ("actor-a", parsed.actor_id);
    TEST_ASSERT_EQUAL_UINT8 (caller.size, parsed.session_rid.size);
    TEST_ASSERT_EQUAL_MEMORY (caller.data, parsed.session_rid.data, caller.size);

    zlink_msg_close (&control);
}

void test_no_bind_reply_control_frame_round_trips_result_code ()
{
    zlink_routing_id_t caller = make_rid ("caller-reply");
    zlink_msg_t control;
    TEST_ASSERT_TRUE (zlink::spot_actor_gateway::init_control_msg (
      zlink::spot_actor_gateway::packet_actor_to_server_no_bind_reply, caller, "actor-b", 9,
      ZLINK_PART_FINAL, &control, 42, ZLINK_REQUEST_CONFLICT));

    zlink::spot_actor_gateway::frame_t parsed;
    TEST_ASSERT_TRUE (zlink::spot_actor_gateway::parse_control_msg (&control, &parsed));
    TEST_ASSERT_EQUAL_UINT8 (zlink::spot_actor_gateway::packet_actor_to_server_no_bind_reply,
                             parsed.kind);
    TEST_ASSERT_EQUAL_UINT64 (42, parsed.request_id);
    TEST_ASSERT_EQUAL_INT32 (ZLINK_REQUEST_CONFLICT, parsed.join_result_code);
    TEST_ASSERT_EQUAL_STRING ("actor-b", parsed.actor_id);

    zlink_msg_close (&control);
}

void test_parser_keeps_rejecting_empty_session_slot ()
{
    zlink_routing_id_t empty;
    memset (&empty, 0, sizeof (empty));
    zlink_msg_t control;
    TEST_ASSERT_FALSE (zlink::spot_actor_gateway::init_control_msg (
      zlink::spot_actor_gateway::packet_server_to_actor_no_bind, empty, "actor-c", 1,
      ZLINK_PART_FINAL, &control, 1, 0));
    TEST_ASSERT_EQUAL_INT (EINVAL, zlink_errno ());
}

void test_existing_gateway_kind_values_do_not_move ()
{
    TEST_ASSERT_EQUAL_UINT8 (1, zlink::spot_actor_gateway::packet_session_to_actor);
    TEST_ASSERT_EQUAL_UINT8 (2, zlink::spot_actor_gateway::packet_actor_to_session);
    TEST_ASSERT_EQUAL_UINT8 (3, zlink::spot_actor_gateway::packet_entry_join_request);
    TEST_ASSERT_EQUAL_UINT8 (4, zlink::spot_actor_gateway::packet_entry_join_reply);
    TEST_ASSERT_EQUAL_UINT8 (5, zlink::spot_actor_gateway::packet_spot_join_request);
    TEST_ASSERT_EQUAL_UINT8 (6, zlink::spot_actor_gateway::packet_spot_join_reply);
    TEST_ASSERT_EQUAL_UINT8 (7, zlink::spot_actor_gateway::packet_server_to_actor_no_bind);
    TEST_ASSERT_EQUAL_UINT8 (8, zlink::spot_actor_gateway::packet_actor_to_server_no_bind_reply);
}
}

int main ()
{
    UNITY_BEGIN ();
    RUN_TEST (test_no_bind_control_frame_round_trips_request_metadata);
    RUN_TEST (test_no_bind_reply_control_frame_round_trips_result_code);
    RUN_TEST (test_parser_keeps_rejecting_empty_session_slot);
    RUN_TEST (test_existing_gateway_kind_values_do_not_move);
    return UNITY_END ();
}
