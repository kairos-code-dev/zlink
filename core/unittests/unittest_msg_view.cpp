/* SPDX-License-Identifier: MPL-2.0 */

#include "../tests/testutil.hpp"

#include "core/msg.hpp"
#include "utils/ip.hpp"

#include <cstdlib>
#include <cstring>
#include <unity.h>

void setUp ()
{
}

void tearDown ()
{
}

void test_init_view_lmsg_shares_storage ()
{
    zlink::msg_t src;
    zlink::msg_t view;
    TEST_ASSERT_EQUAL_INT (0, src.init_size (128));
    TEST_ASSERT_EQUAL_INT (0, view.init ());

    unsigned char *src_data = static_cast<unsigned char *> (src.data ());
    for (size_t i = 0; i < src.size (); ++i)
        src_data[i] = static_cast<unsigned char> (i & 0xFF);

    TEST_ASSERT_EQUAL_INT (0, view.init_view (src, 8, 64));
    TEST_ASSERT_TRUE (std::memcmp (view.data (), src_data + 8, 64) == 0);

    src_data[10] = 0xAB;
    TEST_ASSERT_EQUAL_UINT8 (0xAB,
                             static_cast<unsigned char *> (view.data ())[2]);

    TEST_ASSERT_EQUAL_INT (0, src.close ());
    TEST_ASSERT_EQUAL_UINT8 (0xAB,
                             static_cast<unsigned char *> (view.data ())[2]);
    TEST_ASSERT_EQUAL_INT (0, view.close ());
}

void test_init_view_vsm_fallback_copy ()
{
    zlink::msg_t src;
    zlink::msg_t view;
    TEST_ASSERT_EQUAL_INT (0, src.init_size (8));
    TEST_ASSERT_EQUAL_INT (0, view.init ());

    unsigned char *src_data = static_cast<unsigned char *> (src.data ());
    for (size_t i = 0; i < src.size (); ++i)
        src_data[i] = static_cast<unsigned char> (0x10 + i);

    TEST_ASSERT_EQUAL_INT (0, view.init_view (src, 2, 4));
    TEST_ASSERT_TRUE (std::memcmp (view.data (), src_data + 2, 4) == 0);

    src_data[3] = 0xEE;
    TEST_ASSERT_TRUE (static_cast<unsigned char *> (view.data ())[1] != 0xEE);

    TEST_ASSERT_EQUAL_INT (0, view.close ());
    TEST_ASSERT_EQUAL_INT (0, src.close ());
}

void test_init_view_invalid_range ()
{
    zlink::msg_t src;
    zlink::msg_t view;
    TEST_ASSERT_EQUAL_INT (0, src.init_size (16));
    TEST_ASSERT_EQUAL_INT (0, view.init ());

    errno = 0;
    TEST_ASSERT_EQUAL_INT (-1, view.init_view (src, 10, 7));
    TEST_ASSERT_EQUAL_INT (EINVAL, errno);

    TEST_ASSERT_EQUAL_INT (0, view.close ());
    TEST_ASSERT_EQUAL_INT (0, src.close ());
}

void on_msg_view_test_free (void *data_, void *hint_)
{
    if (hint_) {
        int *counter = static_cast<int *> (hint_);
        ++(*counter);
    }
    std::free (data_);
}

void test_init_view_from_zcmsg ()
{
    zlink::msg_t src;
    zlink::msg_t view;
    zlink::msg_t::content_t content;
    int free_counter = 0;

    unsigned char *payload =
      static_cast<unsigned char *> (std::malloc (96));
    TEST_ASSERT_TRUE (payload != NULL);
    std::memset (payload, 0x42, 96);

    TEST_ASSERT_EQUAL_INT (0, src.init ());
    TEST_ASSERT_EQUAL_INT (
      0, src.init (payload, 96, on_msg_view_test_free, &free_counter, &content));
    TEST_ASSERT_TRUE (src.is_zcmsg ());

    TEST_ASSERT_EQUAL_INT (0, view.init ());
    TEST_ASSERT_EQUAL_INT (0, view.init_view (src, 4, 32));
    TEST_ASSERT_EQUAL_UINT (32, (unsigned int) view.size ());
    TEST_ASSERT_TRUE (std::memcmp (view.data (), payload + 4, 32) == 0);

    TEST_ASSERT_EQUAL_INT (0, src.close ());
    TEST_ASSERT_EQUAL_INT (0, free_counter);
    TEST_ASSERT_EQUAL_INT (0, view.close ());
    TEST_ASSERT_EQUAL_INT (1, free_counter);
}

int main (void)
{
    UNITY_BEGIN ();

    zlink::initialize_network ();
    setup_test_environment ();

    RUN_TEST (test_init_view_lmsg_shares_storage);
    RUN_TEST (test_init_view_vsm_fallback_copy);
    RUN_TEST (test_init_view_invalid_range);
    RUN_TEST (test_init_view_from_zcmsg);

    zlink::shutdown_network ();
    return UNITY_END ();
}
