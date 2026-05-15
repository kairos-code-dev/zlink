/* SPDX-License-Identifier: MPL-2.0 */

#include "../testutil_unity.hpp"

#include "../../src/api/message/bind_result_internal.hpp"
#include "../../src/api/core/close_result_internal.hpp"
#include "../../src/api/core/config_result_internal.hpp"
#include "../../src/api/message/connect_result_internal.hpp"
#include "../../src/api/message/handler_result_internal.hpp"
#include "../../src/api/message/recv_result_internal.hpp"
#include "../../src/api/message/request_result_internal.hpp"
#include "../../src/api/message/submit_result_internal.hpp"

#include <unity.h>

void setUp ()
{
}

void tearDown ()
{
}

void test_recv_unknown_errno_maps_to_internal_error ()
{
    TEST_ASSERT_EQUAL_INT (ZLINK_RECV_INTERNAL_ERROR,
                           zlink::recv_result_internal::from_errno (EPROTO));
    TEST_ASSERT_EQUAL_INT (ZLINK_RECV_INTERNAL_ERROR,
                           zlink::recv_result_internal::from_errno (ENOMEM));
}

void test_request_unknown_errno_maps_to_internal_error ()
{
    TEST_ASSERT_EQUAL_INT (
      ZLINK_REQUEST_INTERNAL_ERROR,
      zlink::request_result_internal::from_errno (ENOMEM));
    TEST_ASSERT_EQUAL_INT (
      ZLINK_REQUEST_PROTOCOL_ERROR,
      zlink::request_result_internal::from_errno (EPROTO));
}

void test_config_unknown_errno_maps_to_internal_error ()
{
    TEST_ASSERT_EQUAL_INT (
      ZLINK_CONFIG_INTERNAL_ERROR,
      zlink::config_result_internal::from_errno (ENOMEM));
    TEST_ASSERT_EQUAL_INT (
      ZLINK_CONFIG_INVALID_STATE,
      zlink::config_result_internal::from_errno (EBUSY));
    TEST_ASSERT_EQUAL_INT (
      ZLINK_CONFIG_NOT_FOUND,
      zlink::config_result_internal::from_errno (ENOENT));
}

void test_handler_unknown_errno_maps_to_internal_error ()
{
    TEST_ASSERT_EQUAL_INT (
      ZLINK_HANDLER_INTERNAL_ERROR,
      zlink::handler_result_internal::from_errno (ENOMEM));
}

void test_connect_bind_close_unknown_errno_map_to_internal_error ()
{
    TEST_ASSERT_EQUAL_INT (
      ZLINK_CONNECT_INTERNAL_ERROR,
      zlink::connect_result_internal::from_errno (EIO));
    TEST_ASSERT_EQUAL_INT (ZLINK_BIND_INTERNAL_ERROR,
                           zlink::bind_result_internal::from_errno (
                             EADDRNOTAVAIL));
    TEST_ASSERT_EQUAL_INT (
      ZLINK_CLOSE_INTERNAL_ERROR,
      zlink::close_result_internal::from_errno (EINTR));
}

void test_connect_result_maps_peer_disconnect_errnos ()
{
    TEST_ASSERT_EQUAL_INT (
      ZLINK_CONNECT_NOT_FOUND,
      zlink::connect_result_internal::from_errno (ENOENT));
    TEST_ASSERT_EQUAL_INT (
      ZLINK_CONNECT_CONFLICT,
      zlink::connect_result_internal::from_errno (EADDRINUSE));
    TEST_ASSERT_EQUAL_INT (
      ZLINK_CONNECT_BUSY,
      zlink::connect_result_internal::from_errno (EBUSY));
}

void test_submit_unknown_errno_is_normalized ()
{
    TEST_ASSERT_EQUAL_INT (
      ZLINK_SUBMIT_INTERNAL_ERROR,
      zlink::submit_result_internal::from_errno (EINTR));
    TEST_ASSERT_EQUAL_INT (
      ZLINK_SUBMIT_OUT_OF_MEMORY,
      zlink::submit_result_internal::from_errno (ENOMEM));
}

int main (int argc, char *argv[])
{
    UNITY_BEGIN ();
    RUN_TEST (test_recv_unknown_errno_maps_to_internal_error);
    RUN_TEST (test_request_unknown_errno_maps_to_internal_error);
    RUN_TEST (test_config_unknown_errno_maps_to_internal_error);
    RUN_TEST (test_handler_unknown_errno_maps_to_internal_error);
    RUN_TEST (test_connect_bind_close_unknown_errno_map_to_internal_error);
    RUN_TEST (test_connect_result_maps_peer_disconnect_errnos);
    RUN_TEST (test_submit_unknown_errno_is_normalized);
    return UNITY_END ();
}
