/* SPDX-License-Identifier: MPL-2.0 */

#include "../tests/testutil.hpp"

#include "protocol/raw_decoder.hpp"
#include "protocol/wire.hpp"
#include "utils/ip.hpp"

#include <unity.h>
#include <vector>

void setUp ()
{
}

void tearDown ()
{
}

static void append_frame (std::vector<unsigned char> &buf_,
                          const unsigned char *data_,
                          size_t size_)
{
    const size_t offset = buf_.size ();
    buf_.resize (offset + 4 + size_);
    zmq::put_uint32 (&buf_[offset], static_cast<uint32_t> (size_));
    if (size_ > 0)
        memcpy (&buf_[offset + 4], data_, size_);
}

void test_decode_two_messages_single_buffer ()
{
    zmq::raw_decoder_t decoder (64, -1);

    std::vector<unsigned char> buf;
    const unsigned char msg1[] = "abc";
    const unsigned char msg2[] = "de";
    append_frame (buf, msg1, sizeof (msg1) - 1);
    append_frame (buf, msg2, sizeof (msg2) - 1);

    size_t processed = 0;
    int rc = decoder.decode (&buf[0], buf.size (), processed);
    TEST_ASSERT_EQUAL_INT (1, rc);
    TEST_ASSERT_EQUAL_INT (3, decoder.msg ()->size ());
    TEST_ASSERT_EQUAL_STRING_LEN ("abc",
                                  static_cast<const char *> (decoder.msg ()->data ()),
                                  decoder.msg ()->size ());

    size_t processed2 = 0;
    rc = decoder.decode (&buf[0] + processed, buf.size () - processed, processed2);
    TEST_ASSERT_EQUAL_INT (1, rc);
    TEST_ASSERT_EQUAL_INT (2, decoder.msg ()->size ());
    TEST_ASSERT_EQUAL_STRING_LEN ("de",
                                  static_cast<const char *> (decoder.msg ()->data ()),
                                  decoder.msg ()->size ());
}

void test_decode_zero_length ()
{
    zmq::raw_decoder_t decoder (64, -1);

    unsigned char header[4];
    zmq::put_uint32 (header, 0);
    size_t processed = 0;
    const int rc = decoder.decode (header, sizeof (header), processed);
    TEST_ASSERT_EQUAL_INT (1, rc);
    TEST_ASSERT_EQUAL_INT (0, decoder.msg ()->size ());
}

void test_body_too_large ()
{
    zmq::raw_decoder_t decoder (64, 4);

    unsigned char header[4];
    zmq::put_uint32 (header, 8);
    size_t processed = 0;
    const int rc = decoder.decode (header, sizeof (header), processed);
    TEST_ASSERT_EQUAL_INT (-1, rc);
    TEST_ASSERT_EQUAL_INT (EMSGSIZE, errno);
}

int main (void)
{
    UNITY_BEGIN ();

    zmq::initialize_network ();
    setup_test_environment ();

    RUN_TEST (test_decode_two_messages_single_buffer);
    RUN_TEST (test_decode_zero_length);
    RUN_TEST (test_body_too_large);

    zmq::shutdown_network ();

    return UNITY_END ();
}
