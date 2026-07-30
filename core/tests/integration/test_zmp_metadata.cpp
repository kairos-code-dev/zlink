/* SPDX-License-Identifier: MPL-2.0 */

#include "testutil.hpp"
#include "testutil_unity.hpp"

#include "protocol/wire.hpp"
#include "protocol/zmp_metadata.hpp"
#include "protocol/zmp_protocol.hpp"

#include <errno.h>
#include <string.h>
#include <vector>

#ifndef ZLINK_HAVE_WINDOWS
#include <sys/time.h>
#endif

SETUP_TEARDOWN_TESTCONTEXT

namespace
{
void set_recv_timeout (fd_t fd_, int timeout_ms_)
{
#if defined ZLINK_HAVE_WINDOWS
    DWORD timeout = static_cast<DWORD> (timeout_ms_);
    TEST_ASSERT_SUCCESS_RAW_ERRNO (setsockopt (
      fd_, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char *> (&timeout), sizeof (timeout)));
#else
    struct timeval tv;
    tv.tv_sec = timeout_ms_ / 1000;
    tv.tv_usec = (timeout_ms_ % 1000) * 1000;
    TEST_ASSERT_SUCCESS_RAW_ERRNO (setsockopt (fd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof (tv)));
#endif
}

enum recv_status_t
{
    recv_ok = 0,
    recv_closed,
    recv_timeout,
    recv_error
};

bool send_all (fd_t fd_, const unsigned char *buf_, size_t size_)
{
    size_t offset = 0;
    while (offset < size_) {
#if defined ZLINK_HAVE_WINDOWS
        const int rc = send (fd_, reinterpret_cast<const char *> (buf_ + offset),
                             static_cast<int> (size_ - offset), 0);
        if (rc <= 0)
            return false;
#else
        const ssize_t rc = send (fd_, buf_ + offset, size_ - offset, MSG_NOSIGNAL);
        if (rc <= 0)
            return false;
#endif
        offset += static_cast<size_t> (rc);
    }
    return true;
}

recv_status_t recv_all (fd_t fd_, unsigned char *buf_, size_t size_)
{
    size_t offset = 0;
    while (offset < size_) {
#if defined ZLINK_HAVE_WINDOWS
        const int rc = recv (fd_, reinterpret_cast<char *> (buf_ + offset),
                             static_cast<int> (size_ - offset), 0);
        if (rc == 0)
            return recv_closed;
        if (rc < 0) {
            const int err = WSAGetLastError ();
            if (err == WSAETIMEDOUT)
                return recv_timeout;
            return recv_error;
        }
#else
        const ssize_t rc = recv (fd_, buf_ + offset, size_ - offset, MSG_NOSIGNAL);
        if (rc == 0)
            return recv_closed;
        if (rc < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                return recv_timeout;
            return recv_error;
        }
#endif
        offset += static_cast<size_t> (rc);
    }
    return recv_ok;
}

bool send_zmp_frame (fd_t fd_, unsigned char flags_, const unsigned char *body_, size_t body_len_)
{
    unsigned char header[zlink::zmp_header_size];
    header[0] = zlink::zmp_magic;
    header[1] = zlink::zmp_version;
    header[2] = flags_;
    header[3] = 0;
    zlink::put_uint32 (header + 4, static_cast<uint32_t> (body_len_));

    if (!send_all (fd_, header, sizeof (header)))
        return false;
    if (body_len_ > 0)
        return send_all (fd_, body_, body_len_);
    return true;
}

bool send_zmp_control (fd_t fd_, const unsigned char *body_, size_t body_len_)
{
    return send_zmp_frame (fd_, zlink::zmp_flag_control, body_, body_len_);
}

bool send_paired_dealer_handshake (fd_t fd_,
                                   const char *routing_id_,
                                   uint64_t pair_id_,
                                   uint64_t generation_,
                                   unsigned char lane_)
{
    unsigned char hello[3 + 255];
    const size_t routing_id_size = strlen (routing_id_);
    if (routing_id_size > 255)
        return false;
    hello[0] = zlink::zmp_control_hello;
    hello[1] = static_cast<unsigned char> (ZLINK_SOCKET_DEALER);
    hello[2] = static_cast<unsigned char> (routing_id_size);
    memcpy (hello + 3, routing_id_, routing_id_size);
    if (!send_zmp_control (fd_, hello, 3 + routing_id_size))
        return false;

    std::vector<unsigned char> ready;
    ready.push_back (zlink::zmp_control_ready);
    zlink::zmp_metadata::append_property (
      ready, "Socket-Type", "DEALER", strlen ("DEALER"));
    zlink::zmp_metadata::append_property (
      ready, "Routing-Id", routing_id_, routing_id_size);

    unsigned char pair_id[8];
    unsigned char generation[8];
    zlink::put_uint64 (pair_id, pair_id_);
    zlink::put_uint64 (generation, generation_);
    zlink::zmp_metadata::append_property (
      ready, "Zlink-Pair-Id", pair_id, sizeof (pair_id));
    zlink::zmp_metadata::append_property (
      ready, "Zlink-Pair-Generation", generation, sizeof (generation));
    zlink::zmp_metadata::append_property (
      ready, "Zlink-Lane", &lane_, sizeof (lane_));
    return send_zmp_control (fd_, &ready[0], ready.size ());
}

bool read_zmp_frame (fd_t fd_,
                     unsigned char &flags_,
                     std::vector<unsigned char> &body_,
                     bool &closed_)
{
    unsigned char header[zlink::zmp_header_size];
    const recv_status_t header_rc = recv_all (fd_, header, sizeof (header));
    if (header_rc == recv_closed) {
        closed_ = true;
        return false;
    }
    if (header_rc != recv_ok)
        return false;

    const uint32_t body_len = zlink::get_uint32 (header + 4);
    if (body_len > 1024)
        return false;

    body_.assign (body_len, 0);
    if (body_len > 0) {
        const recv_status_t body_rc = recv_all (fd_, &body_[0], body_len);
        if (body_rc == recv_closed) {
            closed_ = true;
            return false;
        }
        if (body_rc != recv_ok)
            return false;
    }

    flags_ = header[2];
    return true;
}
}

void test_zmp_error_invalid_hello ()
{
    void *server = test_context_socket (ZLINK_SOCKET_PAIR);
    char endpoint[MAX_SOCKET_STRING];
    bind_loopback_ipv4 (server, endpoint, sizeof (endpoint));

    fd_t raw = connect_socket (endpoint, AF_INET, IPPROTO_TCP);
    TEST_ASSERT_NOT_EQUAL (retired_fd, raw);
    set_recv_timeout (raw, 2000);

    unsigned char body[3];
    body[0] = zlink::zmp_control_hello;
    body[1] = ZLINK_SOCKET_PAIR;
    body[2] = 0;

    unsigned char header[zlink::zmp_header_size];
    header[0] = 0x00;
    header[1] = zlink::zmp_version;
    header[2] = zlink::zmp_flag_control;
    header[3] = 0;
    zlink::put_uint32 (header + 4, sizeof (body));
    TEST_ASSERT_TRUE (send_all (raw, header, sizeof (header)));
    TEST_ASSERT_TRUE (send_all (raw, body, sizeof (body)));

    bool closed = false;
    bool saw_error = false;
    for (int i = 0; i < 4 && !saw_error && !closed; ++i) {
        unsigned char flags = 0;
        std::vector<unsigned char> body;
        if (!read_zmp_frame (raw, flags, body, closed))
            continue;
        if ((flags & zlink::zmp_flag_control) && !body.empty ()
            && body[0] == zlink::zmp_control_error) {
            TEST_ASSERT_TRUE (body.size () >= 3);
            TEST_ASSERT_EQUAL_UINT8 (zlink::zmp_error_invalid_magic, body[1]);
            saw_error = true;
        }
    }

    TEST_ASSERT_TRUE_MESSAGE (saw_error, "expected ERROR frame");

    close (raw);
    test_context_socket_close (server);
}

void test_paired_ready_generation_mismatch_is_not_attached ()
{
    void *server = test_context_socket (ZLINK_SOCKET_ROUTER);
    char endpoint[MAX_SOCKET_STRING];
    bind_loopback_ipv4 (server, endpoint, sizeof (endpoint));

    fd_t application = connect_socket (endpoint, AF_INET, IPPROTO_TCP);
    fd_t completion = connect_socket (endpoint, AF_INET, IPPROTO_TCP);
    TEST_ASSERT_NOT_EQUAL (retired_fd, application);
    TEST_ASSERT_NOT_EQUAL (retired_fd, completion);

    //  Both lanes claim the same pair and peer, but a different generation.
    //  The server must not combine them into a dispatchable Application pipe.
    TEST_ASSERT_TRUE (
      send_paired_dealer_handshake (application, "raw-paired-peer", 41, 7, 0));
    TEST_ASSERT_TRUE (
      send_paired_dealer_handshake (completion, "raw-paired-peer", 41, 8, 1));

    const unsigned char payload[] = {'s', 't', 'a', 'l', 'e'};
    TEST_ASSERT_TRUE (
      send_zmp_frame (application, 0, payload, sizeof (payload)));
    msleep (SETTLE_TIME * 10);

    zlink_msg_t msg;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_init (&msg));
    const zlink_routing_id_t *source_rid = NULL;
    uint64_t request_seq = 0;
    zlink_part_flag_t has_more = ZLINK_PART_FINAL;
    errno = 0;
    TEST_ASSERT_EQUAL_INT (
      ZLINK_RECV_NO_DATA,
      zlink_router_recv_part (
        server, &source_rid, &request_seq, &msg, &has_more,
        static_cast<zlink_recv_flags_t> (ZLINK_DONTWAIT)));
    TEST_ASSERT_EQUAL_INT (EAGAIN, errno);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_close (&msg));

    close (completion);
    close (application);
    test_context_socket_close_zero_linger (server);
}

int main (void)
{
    UNITY_BEGIN ();

    setup_test_environment ();

    RUN_TEST (test_zmp_error_invalid_hello);
    RUN_TEST (test_paired_ready_generation_mismatch_is_not_attached);

    return UNITY_END ();
}
