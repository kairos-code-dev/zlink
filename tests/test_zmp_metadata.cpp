/* SPDX-License-Identifier: MPL-2.0 */

#include "testutil.hpp"
#include "testutil_unity.hpp"

#include "../src/wire.hpp"
#include "../src/zmp_protocol.hpp"

#include <errno.h>
#include <string.h>
#include <vector>

#ifndef ZMQ_HAVE_WINDOWS
#include <sys/time.h>
#endif

SETUP_TEARDOWN_TESTCONTEXT

namespace
{
void set_zlink_protocol_zmp ()
{
#if defined ZMQ_HAVE_WINDOWS
    TEST_ASSERT_EQUAL_INT (0, _putenv_s ("ZLINK_PROTOCOL", "zmp"));
#else
    TEST_ASSERT_SUCCESS_RAW_ERRNO (setenv ("ZLINK_PROTOCOL", "zmp", 1));
#endif
}

void set_recv_timeout (fd_t fd_, int timeout_ms_)
{
#if defined ZMQ_HAVE_WINDOWS
    DWORD timeout = static_cast<DWORD> (timeout_ms_);
    TEST_ASSERT_SUCCESS_RAW_ERRNO (setsockopt (
      fd_, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char *> (&timeout),
      sizeof (timeout)));
#else
    struct timeval tv;
    tv.tv_sec = timeout_ms_ / 1000;
    tv.tv_usec = (timeout_ms_ % 1000) * 1000;
    TEST_ASSERT_SUCCESS_RAW_ERRNO (
      setsockopt (fd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof (tv)));
#endif
}

enum recv_status_t
{
    recv_ok = 0,
    recv_closed,
    recv_timeout,
    recv_error
};

recv_status_t recv_all (fd_t fd_, unsigned char *buf_, size_t size_)
{
    size_t offset = 0;
    while (offset < size_) {
#if defined ZMQ_HAVE_WINDOWS
        const int rc = recv (
          fd_, reinterpret_cast<char *> (buf_ + offset),
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
        const ssize_t rc = recv (fd_, buf_ + offset, size_ - offset,
                                 MSG_NOSIGNAL);
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

bool read_zmp_frame (fd_t fd_,
                     unsigned char &flags_,
                     std::vector<unsigned char> &body_,
                     bool &closed_)
{
    unsigned char header[zmq::zmp_header_size];
    const recv_status_t header_rc = recv_all (fd_, header, sizeof (header));
    if (header_rc == recv_closed) {
        closed_ = true;
        return false;
    }
    if (header_rc != recv_ok)
        return false;

    const uint32_t body_len = zmq::get_uint32 (header + 4);
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

void test_zmp_metadata_enabled ()
{
    set_zlink_protocol_zmp ();

    void *server = test_context_socket (ZMQ_PAIR);
    void *client = test_context_socket (ZMQ_PAIR);

    int enabled = 1;
    TEST_ASSERT_SUCCESS_ERRNO (
      zmq_setsockopt (client, ZMQ_ZMP_METADATA, &enabled, sizeof (enabled)));

    char endpoint[MAX_SOCKET_STRING];
    bind_loopback_ipv4 (server, endpoint, sizeof (endpoint));
    TEST_ASSERT_SUCCESS_ERRNO (zmq_connect (client, endpoint));

    send_string_expect_success (client, "A", 0);

    zmq_msg_t msg;
    TEST_ASSERT_SUCCESS_ERRNO (zmq_msg_init (&msg));
    TEST_ASSERT_SUCCESS_ERRNO (zmq_msg_recv (&msg, server, 0));
    const char *sock_type = zmq_msg_gets (&msg, "Socket-Type");
    TEST_ASSERT_NOT_NULL (sock_type);
    TEST_ASSERT_EQUAL_STRING ("PAIR", sock_type);
    TEST_ASSERT_SUCCESS_ERRNO (zmq_msg_close (&msg));

    test_context_socket_close (client);
    test_context_socket_close (server);
}

void test_zmp_metadata_disabled ()
{
    set_zlink_protocol_zmp ();

    void *server = test_context_socket (ZMQ_PAIR);
    void *client = test_context_socket (ZMQ_PAIR);

    int disabled = 0;
    TEST_ASSERT_SUCCESS_ERRNO (
      zmq_setsockopt (client, ZMQ_ZMP_METADATA, &disabled, sizeof (disabled)));

    char endpoint[MAX_SOCKET_STRING];
    bind_loopback_ipv4 (server, endpoint, sizeof (endpoint));
    TEST_ASSERT_SUCCESS_ERRNO (zmq_connect (client, endpoint));

    send_string_expect_success (client, "B", 0);

    zmq_msg_t msg;
    TEST_ASSERT_SUCCESS_ERRNO (zmq_msg_init (&msg));
    TEST_ASSERT_SUCCESS_ERRNO (zmq_msg_recv (&msg, server, 0));
    errno = 0;
    const char *sock_type = zmq_msg_gets (&msg, "Socket-Type");
    TEST_ASSERT_NULL (sock_type);
    TEST_ASSERT_EQUAL_INT (EINVAL, errno);
    TEST_ASSERT_SUCCESS_ERRNO (zmq_msg_close (&msg));

    test_context_socket_close (client);
    test_context_socket_close (server);
}

void test_zmp_error_invalid_hello ()
{
    set_zlink_protocol_zmp ();

    void *server = test_context_socket (ZMQ_PAIR);
    char endpoint[MAX_SOCKET_STRING];
    bind_loopback_ipv4 (server, endpoint, sizeof (endpoint));

    fd_t raw = connect_socket (endpoint, AF_INET, IPPROTO_TCP);
    TEST_ASSERT_NOT_EQUAL (retired_fd, raw);
    set_recv_timeout (raw, 2000);

    unsigned char buf[zmq::zmp_header_size + 3];
    memset (buf, 0, sizeof (buf));
    buf[0] = 0x00; // invalid magic
    buf[1] = zmq::zmp_version;
    buf[2] = zmq::zmp_flag_control;
    buf[3] = 0;
    zmq::put_uint32 (buf + 4, 3);
    buf[zmq::zmp_header_size + 0] = zmq::zmp_control_hello;
    buf[zmq::zmp_header_size + 1] = ZMQ_PAIR;
    buf[zmq::zmp_header_size + 2] = 0;

    const int send_rc = send (raw, reinterpret_cast<const char *> (buf),
                              sizeof (buf), MSG_NOSIGNAL);
    TEST_ASSERT_EQUAL_INT (static_cast<int> (sizeof (buf)), send_rc);

    bool closed = false;
    bool saw_error = false;
    for (int i = 0; i < 4 && !saw_error && !closed; ++i) {
        unsigned char flags = 0;
        std::vector<unsigned char> body;
        if (!read_zmp_frame (raw, flags, body, closed))
            continue;
        if ((flags & zmq::zmp_flag_control) && !body.empty ()
            && body[0] == zmq::zmp_control_error) {
            TEST_ASSERT_TRUE (body.size () >= 3);
            TEST_ASSERT_EQUAL_UINT8 (zmq::zmp_error_invalid_magic, body[1]);
            saw_error = true;
        }
    }

    TEST_ASSERT_TRUE_MESSAGE (saw_error, "expected ERROR frame");

    close (raw);
    test_context_socket_close (server);
}

int main (void)
{
    UNITY_BEGIN ();

    setup_test_environment ();

    RUN_TEST (test_zmp_metadata_enabled);
    RUN_TEST (test_zmp_metadata_disabled);
    RUN_TEST (test_zmp_error_invalid_hello);

    return UNITY_END ();
}
