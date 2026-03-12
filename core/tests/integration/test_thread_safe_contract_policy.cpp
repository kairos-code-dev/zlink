/* SPDX-License-Identifier: MPL-2.0 */

#include "testutil.hpp"
#include "testutil_unity.hpp"

#include <stdio.h>
#include <string>

#ifndef TEST_ZLINK_HEADER_PATH
#define TEST_ZLINK_HEADER_PATH ""
#endif

namespace
{
std::string read_text_file (const char *path_)
{
    TEST_ASSERT_NOT_NULL (path_);
    TEST_ASSERT_TRUE (path_[0] != '\0');

    FILE *fp = fopen (path_, "rb");
    TEST_ASSERT_NOT_NULL_MESSAGE (fp, "failed to open public header");

    std::string content;
    char buf[4096];
    while (true) {
        const size_t n = fread (buf, 1, sizeof (buf), fp);
        if (n > 0)
            content.append (buf, n);
        if (n < sizeof (buf))
            break;
    }

    const int file_error = ferror (fp);
    fclose (fp);
    TEST_ASSERT_EQUAL_INT_MESSAGE (0, file_error,
                                   "failed while reading public header");
    return content;
}

void assert_text_absent (const std::string &text_, const char *needle_)
{
    TEST_ASSERT_NOT_NULL (needle_);

    char msg[192];
    snprintf (msg, sizeof (msg),
              "public header must not expose forbidden contract token: %s",
              needle_);
    TEST_ASSERT_TRUE_MESSAGE (text_.find (needle_) == std::string::npos, msg);
}

void test_public_header_omits_selectable_thread_mode_contract ()
{
    const std::string header = read_text_file (TEST_ZLINK_HEADER_PATH);
    TEST_ASSERT_FALSE (header.empty ());

    assert_text_absent (header, "zlink_thread_mode_t");
    assert_text_absent (header, "thread_mode");
    assert_text_absent (header, "_set_thread_mode");
    assert_text_absent (header, "set_recv_handler");
    assert_text_absent (header, "_set_handler");
    assert_text_absent (header, "_use_lock");
}
}

int main ()
{
    setup_test_environment ();

    UNITY_BEGIN ();
    RUN_TEST (test_public_header_omits_selectable_thread_mode_contract);
    return UNITY_END ();
}
