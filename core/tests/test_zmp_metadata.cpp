/* SPDX-License-Identifier: MPL-2.0 */

#include "testutil.hpp"
#include "testutil_unity.hpp"

#include "protocol/wire.hpp"
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

bool send_all (fd_t fd_, const unsigned char *buf_, size_t size_)
{
    size_t offset = 0;
    while (offset < size_) {
#if defined ZLINK_HAVE_WINDOWS
        const int rc = send (
          fd_, reinterpret_cast<const char *> (buf_ + offset),
          static_cast<int> (size_ - offset), 0);
        if (rc <= 0)
            return false;
#else
        const ssize_t rc =
          send (fd_, buf_ + offset, size_ - offset, MSG_NOSIGNAL);
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

bool send_zmp_frame (fd_t fd_,
                     unsigned char flags_,
                     const unsigned char *body_,
                     size_t body_len_)
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

bool send_zmp_control (fd_t fd_,
                       const unsigned char *body_,
                       size_t body_len_)
{
    return send_zmp_frame (fd_, zlink::zmp_flag_control, body_, body_len_);
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

void test_zmp_metadata_enabled ()
{
    void *server = test_context_socket (ZLINK_PAIR);
    void *client = test_context_socket (ZLINK_PAIR);

    int enabled = 1;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_setsockopt (client, ZLINK_ZMP_METADATA, &enabled, sizeof (enabled)));

    char endpoint[MAX_SOCKET_STRING];
    bind_loopback_ipv4 (server, endpoint, sizeof (endpoint));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_connect (client, endpoint));

    send_string_expect_success (client, "A", 0);

    zlink_msg_t msg;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_init (&msg));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_recv (&msg, server, 0));
    const char *sock_type = zlink_msg_gets (&msg, "Socket-Type");
    TEST_ASSERT_NOT_NULL (sock_type);
    TEST_ASSERT_EQUAL_STRING ("PAIR", sock_type);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_close (&msg));

    test_context_socket_close (client);
    test_context_socket_close (server);
}

void test_zmp_metadata_disabled ()
{
    void *server = test_context_socket (ZLINK_PAIR);
    void *client = test_context_socket (ZLINK_PAIR);

    int disabled = 0;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_setsockopt (client, ZLINK_ZMP_METADATA, &disabled, sizeof (disabled)));

    char endpoint[MAX_SOCKET_STRING];
    bind_loopback_ipv4 (server, endpoint, sizeof (endpoint));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_connect (client, endpoint));

    send_string_expect_success (client, "B", 0);

    zlink_msg_t msg;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_init (&msg));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_recv (&msg, server, 0));
    errno = 0;
    const char *sock_type = zlink_msg_gets (&msg, "Socket-Type");
    TEST_ASSERT_NULL (sock_type);
    TEST_ASSERT_EQUAL_INT (EINVAL, errno);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_close (&msg));

    test_context_socket_close (client);
    test_context_socket_close (server);
}

void test_zmp_error_invalid_hello ()
{
    void *server = test_context_socket (ZLINK_PAIR);
    char endpoint[MAX_SOCKET_STRING];
    bind_loopback_ipv4 (server, endpoint, sizeof (endpoint));

    fd_t raw = connect_socket (endpoint, AF_INET, IPPROTO_TCP);
    TEST_ASSERT_NOT_EQUAL (retired_fd, raw);
    set_recv_timeout (raw, 2000);

    unsigned char body[3];
    body[0] = zlink::zmp_control_hello;
    body[1] = ZLINK_PAIR;
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

void test_zmp_heartbeat_ttl_min ()
{
    void *server = test_context_socket (ZLINK_PAIR);
    int ttl_local_ms = 300;
#if defined ZLINK_HAVE_WINDOWS
    // Windows scheduling/timer granularity can delay TTL handling.
    ttl_local_ms = 1000;
#endif
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_setsockopt (server, ZLINK_HEARTBEAT_TTL, &ttl_local_ms,
                      sizeof (ttl_local_ms)));

    char endpoint[MAX_SOCKET_STRING];
    bind_loopback_ipv4 (server, endpoint, sizeof (endpoint));

    fd_t raw = connect_socket (endpoint, AF_INET, IPPROTO_TCP);
    TEST_ASSERT_NOT_EQUAL (retired_fd, raw);
    set_recv_timeout (raw, 200);

    unsigned char hello_body[3];
    hello_body[0] = zlink::zmp_control_hello;
    hello_body[1] = ZLINK_PAIR;
    hello_body[2] = 0;
    TEST_ASSERT_TRUE (send_zmp_control (raw, hello_body, sizeof (hello_body)));

    unsigned char ready_body[1];
    ready_body[0] = zlink::zmp_control_ready;
    TEST_ASSERT_TRUE (send_zmp_control (raw, ready_body, sizeof (ready_body)));

    bool closed = false;
    bool saw_hello = false;
    bool saw_ready = false;
    for (int i = 0; i < 4 && !(saw_hello && saw_ready) && !closed; ++i) {
        unsigned char flags = 0;
        std::vector<unsigned char> body;
        if (!read_zmp_frame (raw, flags, body, closed))
            continue;
        if ((flags & zlink::zmp_flag_control) && !body.empty ()) {
            if (body[0] == zlink::zmp_control_hello)
                saw_hello = true;
            if (body[0] == zlink::zmp_control_ready)
                saw_ready = true;
        }
    }

    unsigned char heartbeat[5];
    heartbeat[0] = zlink::zmp_control_heartbeat;
    zlink::put_uint16 (heartbeat + 1, 20); // remote TTL 2s (deciseconds)
    heartbeat[3] = 1;
    heartbeat[4] = 'A';
    TEST_ASSERT_TRUE (send_zmp_control (raw, heartbeat, sizeof (heartbeat)));

    int recv_timeout_ms = 1200;
    int recv_attempts = 6;
#if defined ZLINK_HAVE_WINDOWS
    recv_timeout_ms = 3000;
    recv_attempts = 10;
#endif
    set_recv_timeout (raw, recv_timeout_ms);
    bool saw_error = false;
    for (int i = 0; i < recv_attempts && !saw_error && !closed; ++i) {
        unsigned char flags = 0;
        std::vector<unsigned char> body;
        if (!read_zmp_frame (raw, flags, body, closed))
            continue;
        if ((flags & zlink::zmp_flag_control) && !body.empty ()) {
            if (body[0] == zlink::zmp_control_error)
                saw_error = true;
        }
    }

    TEST_ASSERT_TRUE_MESSAGE (
      saw_error || closed,
      "expected timeout disconnect within local TTL window");

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
    RUN_TEST (test_zmp_heartbeat_ttl_min);

    return UNITY_END ();
}
