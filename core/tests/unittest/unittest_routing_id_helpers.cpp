/* SPDX-License-Identifier: MPL-2.0 */

#include "../testutil.hpp"
#include "../testutil_unity.hpp"

#include <string.h>

static void test_routing_id_to_u32_from_u32_round_trip ()
{
    zlink_routing_id_t rid;
    memset (&rid, 0, sizeof (rid));

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_routing_id_from_u32 (&rid, 0x01020304u));
    TEST_ASSERT_EQUAL_UINT8 (4, rid.size);
    TEST_ASSERT_EQUAL_UINT8 (0x01, rid.data[0]);
    TEST_ASSERT_EQUAL_UINT8 (0x02, rid.data[1]);
    TEST_ASSERT_EQUAL_UINT8 (0x03, rid.data[2]);
    TEST_ASSERT_EQUAL_UINT8 (0x04, rid.data[3]);

    uint32_t value = 0;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_routing_id_to_u32 (&rid, &value));
    TEST_ASSERT_EQUAL_UINT32 (0x01020304u, value);

    rid.size = 3;
    memcpy (rid.data, "abc", 3);
    const zlink_config_result_t rc_u32 = zlink_routing_id_to_u32 (&rid, &value);
    TEST_ASSERT_EQUAL_INT (ZLINK_CONFIG_INVALID_ARGUMENT, rc_u32);
    TEST_ASSERT_EQUAL_INT (EINVAL, errno);
}

static void test_routing_id_from_to_text_round_trip ()
{
    zlink_routing_id_t rid;
    memset (&rid, 0, sizeof (rid));

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_routing_id_from_text (&rid, "stream-router"));
    TEST_ASSERT_EQUAL_UINT (13, rid.size);

    char text[32];
    size_t text_len = sizeof (text);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_routing_id_to_text (&rid, text, &text_len));
    TEST_ASSERT_EQUAL_UINT (14, text_len);
    TEST_ASSERT_EQUAL_STRING ("stream-router", text);

    char query_text[1];
    size_t query_len = sizeof (query_text);
    const zlink_config_result_t rc_text =
      zlink_routing_id_to_text (&rid, query_text, &query_len);
    TEST_ASSERT_EQUAL_INT (ZLINK_CONFIG_INVALID_ARGUMENT, rc_text);
    TEST_ASSERT_EQUAL_INT (EINVAL, errno);
    TEST_ASSERT_EQUAL_UINT (14, query_len);
}

static void test_routing_id_from_text_rejects_empty ()
{
    zlink_routing_id_t rid;
    memset (&rid, 0, sizeof (rid));

    const zlink_config_result_t rc_empty = zlink_routing_id_from_text (&rid, "");
    TEST_ASSERT_EQUAL_INT (ZLINK_CONFIG_INVALID_ARGUMENT, rc_empty);
    TEST_ASSERT_EQUAL_INT (EINVAL, errno);
}

static void test_routing_id_to_text_rejects_invalid_utf8 ()
{
    zlink_routing_id_t rid;
    memset (&rid, 0, sizeof (rid));
    rid.size = 2;
    rid.data[0] = static_cast<uint8_t> (0xC3);
    rid.data[1] = static_cast<uint8_t> (0x28);

    char text[4];
    size_t text_len = sizeof (text);
    const zlink_config_result_t rc_bad_utf8 =
      zlink_routing_id_to_text (&rid, text, &text_len);
    TEST_ASSERT_EQUAL_INT (ZLINK_CONFIG_INVALID_ARGUMENT, rc_bad_utf8);
    TEST_ASSERT_EQUAL_INT (EINVAL, errno);
}

static void test_routing_id_from_text_rejects_invalid_utf8 ()
{
    zlink_routing_id_t rid;
    memset (&rid, 0, sizeof (rid));

    static const char invalid_utf8[] = {(char) 0xC3, (char) 0x28, '\0'};
    const zlink_config_result_t rc_bad_utf8 =
      zlink_routing_id_from_text (&rid, invalid_utf8);
    TEST_ASSERT_EQUAL_INT (ZLINK_CONFIG_INVALID_ARGUMENT, rc_bad_utf8);
    TEST_ASSERT_EQUAL_INT (EINVAL, errno);
}

static void test_routing_id_helpers_reject_invalid_buffers ()
{
    zlink_routing_id_t rid;
    memset (&rid, 0, sizeof (rid));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_routing_id_from_text (&rid, "route-a"));

    TEST_ASSERT_EQUAL_INT (ZLINK_CONFIG_INVALID_HANDLE,
                           zlink_routing_id_from_u32 (NULL, 7));
    TEST_ASSERT_EQUAL_INT (EFAULT, errno);

    uint32_t value = 0;
    TEST_ASSERT_EQUAL_INT (ZLINK_CONFIG_INVALID_HANDLE,
                           zlink_routing_id_to_u32 (&rid, NULL));
    TEST_ASSERT_EQUAL_INT (EFAULT, errno);

    size_t out_len = 0;
    TEST_ASSERT_EQUAL_INT (ZLINK_CONFIG_INVALID_HANDLE,
                           zlink_routing_id_to_text (NULL, NULL, &out_len));
    TEST_ASSERT_EQUAL_INT (EFAULT, errno);

    TEST_ASSERT_EQUAL_INT (ZLINK_CONFIG_INVALID_HANDLE,
                           zlink_routing_id_to_hex (NULL, NULL, &out_len));
    TEST_ASSERT_EQUAL_INT (EFAULT, errno);
}

static void test_routing_id_to_hex_always_succeeds ()
{
    zlink_routing_id_t rid;
    memset (&rid, 0, sizeof (rid));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_routing_id_from_text (&rid, "xyz"));

    char hex[16];
    size_t hex_len = sizeof (hex);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_routing_id_to_hex (&rid, hex, &hex_len));
    TEST_ASSERT_EQUAL_UINT (7, hex_len);
    TEST_ASSERT_EQUAL_STRING ("78797a", hex);

    rid.size = 0;
    char empty_hex[2];
    size_t empty_hex_len = sizeof (empty_hex);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_routing_id_to_hex (&rid, empty_hex, &empty_hex_len));
    TEST_ASSERT_EQUAL_UINT (1, empty_hex_len);
    TEST_ASSERT_EQUAL_STRING ("", empty_hex);

    size_t small = 0;
    const zlink_config_result_t rc_small =
      zlink_routing_id_to_hex (&rid, empty_hex, &small);
    TEST_ASSERT_EQUAL_INT (ZLINK_CONFIG_INVALID_ARGUMENT, rc_small);
    TEST_ASSERT_EQUAL_INT (EINVAL, errno);
    TEST_ASSERT_EQUAL_UINT (1, small);
}

static void test_routing_id_round_trip_empty_bytes_query_paths ()
{
    zlink_routing_id_t rid;
    memset (&rid, 0, sizeof (rid));
    rid.size = 0;

    size_t text_len = 0;
    const zlink_config_result_t rc_text_query =
      zlink_routing_id_to_text (&rid, NULL, &text_len);
    TEST_ASSERT_EQUAL_INT (ZLINK_CONFIG_INVALID_ARGUMENT, rc_text_query);
    TEST_ASSERT_EQUAL_INT (EINVAL, errno);
    TEST_ASSERT_EQUAL_UINT (0, text_len);

    size_t hex_len = 0;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_routing_id_to_hex (&rid, NULL, &hex_len));
    TEST_ASSERT_EQUAL_UINT (1, hex_len);
}

int main ()
{
    setup_test_environment ();

    UNITY_BEGIN ();
    RUN_TEST (test_routing_id_to_u32_from_u32_round_trip);
    RUN_TEST (test_routing_id_from_to_text_round_trip);
    RUN_TEST (test_routing_id_from_text_rejects_empty);
    RUN_TEST (test_routing_id_to_text_rejects_invalid_utf8);
    RUN_TEST (test_routing_id_from_text_rejects_invalid_utf8);
    RUN_TEST (test_routing_id_helpers_reject_invalid_buffers);
    RUN_TEST (test_routing_id_to_hex_always_succeeds);
    RUN_TEST (test_routing_id_round_trip_empty_bytes_query_paths);
    return UNITY_END ();
}
