/* SPDX-License-Identifier: MPL-2.0 */

#include "../tests/testutil.hpp"

#include "utils/ip.hpp"
#include "core/msg.hpp"
#include "protocol/wire.hpp"
#include "protocol/zmp_decoder.hpp"
#include "protocol/zmp_metadata.hpp"
#include "protocol/zmp_protocol.hpp"

#include <unity.h>
#include <vector>

void setUp ()
{
}

void tearDown ()
{
}

static void build_header (unsigned char *buf_,
                          unsigned char flags_,
                          uint32_t body_len_)
{
    buf_[0] = zmq::zmp_magic;
    buf_[1] = zmq::zmp_version;
    buf_[2] = flags_;
    buf_[3] = 0;
    zmq::put_uint32 (buf_ + 4, body_len_);
}

void test_invalid_magic ()
{
    zmq::zmp_decoder_t decoder (64, -1);
    unsigned char buf[zmq::zmp_header_size];
    build_header (buf, 0, 0);
    buf[0] = 0x00;
    size_t processed = 0;
    const int rc = decoder.decode (buf, sizeof (buf), processed);
    TEST_ASSERT_EQUAL_INT (-1, rc);
    TEST_ASSERT_EQUAL_INT (EPROTO, errno);
    TEST_ASSERT_EQUAL_UINT8 (zmq::zmp_error_invalid_magic,
                             decoder.error_code ());
}

void test_version_mismatch ()
{
    zmq::zmp_decoder_t decoder (64, -1);
    unsigned char buf[zmq::zmp_header_size];
    build_header (buf, 0, 0);
    buf[1] = static_cast<unsigned char> (zmq::zmp_version + 1);
    size_t processed = 0;
    const int rc = decoder.decode (buf, sizeof (buf), processed);
    TEST_ASSERT_EQUAL_INT (-1, rc);
    TEST_ASSERT_EQUAL_INT (EPROTO, errno);
    TEST_ASSERT_EQUAL_UINT8 (zmq::zmp_error_version_mismatch,
                             decoder.error_code ());
}

void test_flags_invalid ()
{
    zmq::zmp_decoder_t decoder (64, -1);
    unsigned char buf[zmq::zmp_header_size];
    build_header (buf, zmq::zmp_flag_control | zmq::zmp_flag_more, 0);
    size_t processed = 0;
    const int rc = decoder.decode (buf, sizeof (buf), processed);
    TEST_ASSERT_EQUAL_INT (-1, rc);
    TEST_ASSERT_EQUAL_INT (EPROTO, errno);
    TEST_ASSERT_EQUAL_UINT8 (zmq::zmp_error_flags_invalid,
                             decoder.error_code ());
}

void test_subscribe_cancel_invalid ()
{
    zmq::zmp_decoder_t decoder (64, -1);
    unsigned char buf[zmq::zmp_header_size];
    build_header (buf, zmq::zmp_flag_subscribe | zmq::zmp_flag_cancel, 0);
    size_t processed = 0;
    const int rc = decoder.decode (buf, sizeof (buf), processed);
    TEST_ASSERT_EQUAL_INT (-1, rc);
    TEST_ASSERT_EQUAL_INT (EPROTO, errno);
    TEST_ASSERT_EQUAL_UINT8 (zmq::zmp_error_flags_invalid,
                             decoder.error_code ());
}

void test_body_too_large ()
{
    zmq::zmp_decoder_t decoder (64, 16);
    unsigned char buf[zmq::zmp_header_size];
    build_header (buf, 0, 32);
    size_t processed = 0;
    const int rc = decoder.decode (buf, sizeof (buf), processed);
    TEST_ASSERT_EQUAL_INT (-1, rc);
    TEST_ASSERT_EQUAL_INT (EMSGSIZE, errno);
    TEST_ASSERT_EQUAL_UINT8 (zmq::zmp_error_body_too_large,
                             decoder.error_code ());
}

void test_more_identity_allowed ()
{
    zmq::zmp_decoder_t decoder (64, -1);
    unsigned char buf[zmq::zmp_header_size];
    build_header (buf, zmq::zmp_flag_more | zmq::zmp_flag_identity, 0);
    size_t processed = 0;
    const int rc = decoder.decode (buf, sizeof (buf), processed);
    TEST_ASSERT_EQUAL_INT (1, rc);
    const unsigned char flags = decoder.msg ()->flags ();
    TEST_ASSERT_TRUE (flags & zmq::msg_t::more);
    TEST_ASSERT_TRUE (flags & zmq::msg_t::routing_id);
}

void test_metadata_parse_valid ()
{
    std::vector<unsigned char> buf;
    zmq::zmp_metadata::append_property (buf, "Socket-Type", "PAIR", 4);

    zmq::metadata_t::dict_t out;
    const int rc = zmq::zmp_metadata::parse (&buf[0], buf.size (), out);
    TEST_ASSERT_EQUAL_INT (0, rc);
    TEST_ASSERT_EQUAL_STRING ("PAIR", out["Socket-Type"].c_str ());
}

void test_metadata_parse_invalid ()
{
    std::vector<unsigned char> buf;
    zmq::zmp_metadata::append_property (buf, "Socket-Type", "PAIR", 4);
    if (!buf.empty ())
        buf.pop_back ();

    zmq::metadata_t::dict_t out;
    const int rc = zmq::zmp_metadata::parse (&buf[0], buf.size (), out);
    TEST_ASSERT_EQUAL_INT (-1, rc);
    TEST_ASSERT_EQUAL_INT (EPROTO, errno);
}

void test_metadata_add_basic_properties ()
{
    zmq::options_t options;
    options.type = ZMQ_ROUTER;
    const char *routing_id = "RID";
    memcpy (options.routing_id, routing_id, 3);
    options.routing_id_size = 3;

    std::vector<unsigned char> buf;
    zmq::zmp_metadata::add_basic_properties (options, buf);

    zmq::metadata_t::dict_t out;
    const int rc = zmq::zmp_metadata::parse (&buf[0], buf.size (), out);
    TEST_ASSERT_EQUAL_INT (0, rc);
    TEST_ASSERT_EQUAL_STRING ("ROUTER", out["Socket-Type"].c_str ());
    TEST_ASSERT_EQUAL_STRING ("RID", out["Identity"].c_str ());
}

void test_effective_ttl ()
{
    TEST_ASSERT_EQUAL_UINT16 (30, zmq::zmp_effective_ttl_ds (0, 30));
    TEST_ASSERT_EQUAL_UINT16 (20, zmq::zmp_effective_ttl_ds (20, 30));
    TEST_ASSERT_EQUAL_UINT16 (20, zmq::zmp_effective_ttl_ds (50, 20));
    TEST_ASSERT_EQUAL_UINT16 (0, zmq::zmp_effective_ttl_ds (50, 0));
    TEST_ASSERT_EQUAL_UINT16 (0, zmq::zmp_effective_ttl_ds (0, 0));
}

int main (void)
{
    UNITY_BEGIN ();

    zmq::initialize_network ();
    setup_test_environment ();

    RUN_TEST (test_invalid_magic);
    RUN_TEST (test_version_mismatch);
    RUN_TEST (test_flags_invalid);
    RUN_TEST (test_subscribe_cancel_invalid);
    RUN_TEST (test_body_too_large);
    RUN_TEST (test_more_identity_allowed);
    RUN_TEST (test_metadata_parse_valid);
    RUN_TEST (test_metadata_parse_invalid);
    RUN_TEST (test_metadata_add_basic_properties);
    RUN_TEST (test_effective_ttl);

    zmq::shutdown_network ();

    return UNITY_END ();
}
