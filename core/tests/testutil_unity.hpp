#pragma once

/* SPDX-License-Identifier: MPL-2.0 */

#include "../include/zlink.h"
#include "../src/utils/precompiled.hpp"
#include "../src/core/recv_internal.hpp"
#include "../src/sockets/stream.hpp"

#include "testutil.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <thread>
#include <unity.h>

#ifndef ZLINK_SNDMORE
#define ZLINK_SNDMORE ((zlink_send_flags_t) 0x0002u)
#endif

#ifdef __cplusplus
inline int zlink_msg_send (zlink_msg_t *msg_,
                           void *s_,
                           zlink_send_flags_t flags_)
{
    return zlink_compat_msg_send (msg_, s_, flags_);
}

inline int zlink_msg_recv (zlink_msg_t *msg_,
                           void *s_,
                           zlink_send_flags_t flags_)
{
    return zlink_compat_msg_recv (msg_, s_, flags_);
}

inline int zlink_stream_send (void *s_,
                              const zlink_routing_id_t *rid_,
                              const void *data_,
                              size_t size_,
                              zlink_send_flags_t flags_)
{
    return zlink_compat_stream_send (s_, rid_, data_, size_, flags_);
}

inline int zlink_stream_send_msg (void *s_,
                                  const zlink_routing_id_t *rid_,
                                  zlink_msg_t *msg_,
                                  zlink_send_flags_t flags_)
{
    return zlink_compat_stream_send_msg (s_, rid_, msg_, flags_);
}

inline int zlink_send (void *s_,
                       const void *buf_,
                       size_t len_,
                       zlink_send_flags_t flags_)
{
    zlink_msg_t msg;
    if (zlink_msg_init_size (&msg, len_) != 0)
        return -1;
    if (len_ > 0 && buf_)
        memcpy (zlink_msg_data (&msg), buf_, len_);

    int type = 0;
    size_t type_size = sizeof (type);
    int rc = 0;
    if (zlink_getsockopt (s_, ZLINK_TYPE, &type, &type_size) == 0)
        rc = zlink_compat_msg_send (&msg, s_, flags_);
    else
        rc = ::zlink_send (s_, &msg, 1, flags_ & ZLINK_DONTWAIT);

    if (rc < 0) {
        const int err = errno;
        zlink_msg_close (&msg);
        errno = err;
        return -1;
    }
    return static_cast<int> (len_);
}

inline int zlink_recv (void *s_,
                       void *buf_,
                       size_t len_,
                       zlink_send_flags_t flags_)
{
    int type = 0;
    size_t type_size = sizeof (type);
    if (zlink_getsockopt (s_, ZLINK_TYPE, &type, &type_size) == 0)
        return zlink::recv_buffer_internal (s_, buf_, len_, flags_);

    zlink_msg_t *parts = NULL;
    size_t part_count = 0;
    const int rc = ::zlink_recv (s_, NULL, &parts, &part_count, flags_);
    if (rc != 0)
        return -1;

    const size_t copy_len =
      part_count > 0 ? std::min (len_, zlink_msg_size (&parts[0])) : 0;
    if (buf_ && copy_len > 0)
        memcpy (buf_, zlink_msg_data (&parts[0]), copy_len);
    const int result =
      part_count > 0 ? static_cast<int> (zlink_msg_size (&parts[0])) : 0;
    zlink_multipart_close (parts, part_count);
    return result;
}

inline int zlink_gateway_send (void *gateway_,
                               zlink_msg_t *parts_,
                               size_t part_count_,
                               zlink_send_flags_t flags_)
{
    return ::zlink_send (gateway_, parts_, part_count_, flags_);
}

inline int zlink_gateway_send_rid (void *gateway_,
                                   const zlink_routing_id_t *routing_id_,
                                   zlink_msg_t *parts_,
                                   size_t part_count_,
                                   zlink_send_flags_t flags_)
{
    return ::zlink_send_rid (gateway_, routing_id_, parts_, part_count_, flags_);
}

inline int zlink_gateway_recv (void *gateway_,
                               zlink_routing_id_t *source_rid_out_,
                               zlink_msg_t **parts_,
                               size_t *part_count_,
                               int flags_)
{
    return ::zlink_recv (gateway_, source_rid_out_, parts_, part_count_, flags_);
}

inline int zlink_subscribe_recv (void *subject_,
                                 zlink_msg_t **parts_,
                                 size_t *part_count_,
                                 int flags_,
                                 char *topic_id_out_,
                                 size_t *topic_id_len_)
{
    return ::zlink_subscribe_recv (subject_, NULL, parts_, part_count_,
                                   topic_id_out_, topic_id_len_, flags_);
}
#endif

// Internal helper functions that are not intended to be directly called from
// tests. They must be declared in the header since they are used by macros.

int test_assert_success_message_errno_helper (int rc_,
                                              const char *msg_,
                                              const char *expr_,
                                              int line);

int test_assert_success_message_raw_errno_helper (
  int rc_, const char *msg_, const char *expr_, int line, bool zero_ = false);

int test_assert_success_message_raw_zero_errno_helper (int rc_,
                                                       const char *msg_,
                                                       const char *expr_,
                                                       int line);

int test_assert_failure_message_raw_errno_helper (
  int rc_, int expected_errno_, const char *msg_, const char *expr_, int line);

/////////////////////////////////////////////////////////////////////////////
// Macros extending Unity's TEST_ASSERT_* macros in a similar fashion.
/////////////////////////////////////////////////////////////////////////////

// For TEST_ASSERT_SUCCESS_ERRNO, TEST_ASSERT_SUCCESS_MESSAGE_ERRNO and
// TEST_ASSERT_FAILURE_ERRNO, 'expr' must be an expression evaluating
// to a result in the style of a libzlink API function, i.e. an integer which
// is non-negative in case of success, and -1 in case of a failure, and sets
// the value returned by zlink_errno () to the error code.
// TEST_ASSERT_SUCCESS_RAW_ERRNO and TEST_ASSERT_FAILURE_RAW_ERRNO are similar,
// but used with the native socket API functions, and expect that the error
// code can be retrieved in the native way (i.e. WSAGetLastError on Windows,
// and errno otherwise).

// Asserts that the libzlink API 'expr' is successful. In case of a failure, the
// assertion message includes the literal 'expr', the error number as
// determined by zlink_errno(), and the additional 'msg'.
// In case of success, the result of the macro is the result of 'expr'.
#define TEST_ASSERT_SUCCESS_MESSAGE_ERRNO(expr, msg)                           \
    test_assert_success_message_errno_helper (expr, msg, #expr, __LINE__)

// Asserts that the libzlink API 'expr' is successful. In case of a failure, the
// assertion message includes the literal 'expr' and the error code.
// A typical use would be:
//   TEST_ASSERT_SUCCESS_ERRNO (zlink_connect (socket, endpoint));
// In case of success, the result of the macro is the result of 'expr'.
//
// If an additional message should be displayed in case of a failure, use
// TEST_ASSERT_SUCCESS_MESSAGE_ERRNO.
#define TEST_ASSERT_SUCCESS_ERRNO(expr)                                        \
    test_assert_success_message_errno_helper (expr, NULL, #expr, __LINE__)

// Asserts that the socket API 'expr' is successful. In case of a failure, the
// assertion message includes the literal 'expr' and the error code.
// A typical use would be:
//   TEST_ASSERT_SUCCESS_RAW_ERRNO (send (fd, buffer, 64, 0));
// In case of success, the result of the macro is the result of 'expr'.
// Success is strictly defined by a return value different from -1, as opposed
// to checking that it is 0, like TEST_ASSERT_FAILURE_RAW_ZERO_ERRNO does.
#define TEST_ASSERT_SUCCESS_RAW_ERRNO(expr)                                    \
    test_assert_success_message_raw_errno_helper (expr, NULL, #expr, __LINE__)

// Asserts that the socket API 'expr' is successful. In case of a failure, the
// assertion message includes the literal 'expr' and the error code.
// A typical use would be:
//   TEST_ASSERT_SUCCESS_RAW_ZERO_ERRNO (send (fd, buffer, 64, 0));
// In case of success, the result of the macro is the result of 'expr'.
// Success is strictly defined by a return value of 0, as opposed to checking
// that it is not -1, like TEST_ASSERT_FAILURE_RAW_ERRNO does.
#define TEST_ASSERT_SUCCESS_RAW_ZERO_ERRNO(expr)                               \
    test_assert_success_message_raw_zero_errno_helper (expr, NULL, #expr,      \
                                                       __LINE__)

// Asserts that the socket API 'expr' is not successful, and the error code is
// 'error_code'. In case of an unexpected succces, or a failure with an
// unexpected error code, the assertion message includes the literal 'expr'
// and, in case of a failure, the actual error code.
#define TEST_ASSERT_FAILURE_RAW_ERRNO(error_code, expr)                        \
    test_assert_failure_message_raw_errno_helper (expr, error_code, NULL,      \
                                                  #expr, __LINE__)

// Asserts that the libzlink API 'expr' is not successful, and the error code is
// 'error_code'. In case of an unexpected succces, or a failure with an
// unexpected error code, the assertion message includes the literal 'expr'
// and, in case of a failure, the actual error code.
#define TEST_ASSERT_FAILURE_ERRNO(error_code, expr)                            \
    {                                                                          \
        int _rc = (expr);                                                      \
        TEST_ASSERT_EQUAL_INT (-1, _rc);                                       \
        TEST_ASSERT_EQUAL_INT (error_code, errno);                             \
    }

/////////////////////////////////////////////////////////////////////////////
// Utility functions for testing sending and receiving.
/////////////////////////////////////////////////////////////////////////////

// Sends a string via a libzlink socket, and expects the operation to be
// successful (the meaning of which depends on the socket type and configured
// options, and might include dropping the message). Otherwise, a Unity test
// assertion is triggered.
// 'socket_' must be the libzlink socket to use for sending.
// 'str_' must be a 0-terminated string.
// 'flags_' are as documented by the zlink_send function.
void send_string_expect_success (void *socket_, const char *str_, int flags_);

// Receives a message via a libzlink socket, and expects the operation to be
// successful, and the message to be a given string. Otherwise, a Unity test
// assertion is triggered.
// 'socket_' must be the libzlink socket to use for receiving.
// 'str_' must be a 0-terminated string.
// 'flags_' are as documented by the zlink_recv function.
void recv_string_expect_success (void *socket_, const char *str_, int flags_);

// Sends a byte array via a libzlink socket, and expects the operation to be
// successful (the meaning of which depends on the socket type and configured
// options, and might include dropping the message). Otherwise, a Unity test
// assertion is triggered.
// 'socket_' must be the libzlink socket to use for sending.
// 'array_' must be a C uint8_t array. The array size is automatically
// determined via template argument deduction.
// 'flags_' are as documented by the zlink_send function.
template <size_t SIZE>
void send_array_expect_success (void *socket_,
                                const uint8_t (&array_)[SIZE],
                                int flags_)
{
    const int rc = zlink_send (socket_, array_, SIZE, flags_);
    TEST_ASSERT_EQUAL_INT (static_cast<int> (SIZE), rc);
}

// Receives a message via a libzlink socket, and expects the operation to be
// successful, and the message to be a given byte array. Otherwise, a Unity
// test assertion is triggered.
// 'socket_' must be the libzlink socket to use for receiving.
// 'array_' must be a C uint8_t array. The array size is automatically
// determined via template argument deduction.
// 'flags_' are as documented by the zlink_recv function.
template <size_t SIZE>
void recv_array_expect_success (void *socket_,
                                const uint8_t (&array_)[SIZE],
                                int flags_)
{
    char buffer[255];
    TEST_ASSERT_LESS_OR_EQUAL_MESSAGE (sizeof (buffer), SIZE,
                                       "recv_string_expect_success cannot be "
                                       "used for strings longer than 255 "
                                       "characters");

    const int rc = TEST_ASSERT_SUCCESS_ERRNO (
      zlink::recv_buffer_internal (socket_, buffer, sizeof (buffer), flags_));
    TEST_ASSERT_EQUAL_INT (static_cast<int> (SIZE), rc);
    TEST_ASSERT_EQUAL_UINT8_ARRAY (array_, buffer, SIZE);
}

// Attaches a discard handler appropriate for the given socket type.
// Returns 0 on success, -1 on failure.
int test_attach_discard_handler_for_type (void *socket_, int type_);

/////////////////////////////////////////////////////////////////////////////
// Utility function for handling a test libzlink context, that is set up and
// torn down for each Unity test case, such that a clean context is available
// for each test case, and some consistency checks can be performed.
/////////////////////////////////////////////////////////////////////////////

// Use this is an test executable to perform a default setup and teardown of
// the test context, which is appropriate for many libzlink test cases.
#define SETUP_TEARDOWN_TESTCONTEXT                                             \
    void setUp ()                                                              \
    {                                                                          \
        setup_test_context ();                                                 \
    }                                                                          \
    void tearDown ()                                                           \
    {                                                                          \
        teardown_test_context ();                                              \
    }

// The maximum number of sockets that can be managed by the test context.
#define MAX_TEST_SOCKETS 128

// Expected to be called during Unity's setUp function.
void setup_test_context ();

// Returns the test context, e.g. to create sockets in another thread using
// zlink_socket, or set context options.
void *get_test_context ();

// Expected to be called during Unity's tearDown function. Checks that all
// sockets created via test_context_socket have been properly closed using
// test_context_socket_close or test_context_socket_close_zero_linger, and generates a warning otherwise.
void teardown_test_context ();

// Creates a libzlink socket on the test context, and tracks its lifecycle.
// You MUST use test_context_socket_close or test_context_socket_close_zero_linger
// to close a socket created via this function, otherwise undefined behaviour
// will result.
// CAUTION: this function is not thread-safe, and may only be used from the
// main thread.
void *test_context_socket (int type_);

// Closes a socket created via test_context_socket.
// CAUTION: this function is not thread-safe, and may only be used from the
// main thread.
void *test_context_socket_close (void *socket_);

// Closes a socket created via test_context_socket after setting its linger
// timeout to 0.
// CAUTION: this function is not thread-safe, and may only be used from the
// main thread.
void *test_context_socket_close_zero_linger (void *socket_);

/////////////////////////////////////////////////////////////////////////////
// Utility function for handling wildcard binds.
/////////////////////////////////////////////////////////////////////////////

// All function binds a socket to some wildcard address, and retrieve the bound
// endpoint via the ZLINK_SOCKOPT_LAST_ENDPOINT socket option to a given buffer.
// Triggers a Unity test assertion in case of a failure (including the buffer
// being too small for the resulting endpoint string).

// Binds to an explicitly given (wildcard) address.
// TODO redesign such that this function is not necessary to be exposed, but
// the protocol to use is rather specified via an enum value
void test_bind (void *socket_,
                const char *bind_address_,
                char *my_endpoint_,
                size_t len_);

// Binds to a tcp endpoint using the ipv4 or ipv6 loopback wildcard address.
void bind_loopback (void *socket_, int ipv6_, char *my_endpoint_, size_t len_);

typedef void (*bind_function_t) (void *socket_,
                                 char *my_endpoint_,
                                 size_t len_);

// Binds to a tcp endpoint using the ipv4 loopback wildcard address.
void bind_loopback_ipv4 (void *socket_, char *my_endpoint_, size_t len_);

// Binds to a tcp endpoint using the ipv6 loopback wildcard address.
void bind_loopback_ipv6 (void *socket_, char *my_endpoint_, size_t len_);

// Binds to an ipc endpoint using the ipc wildcard address.
// Note that the returned address cannot be reused to bind a second socket.
// If you need to do this, use make_random_ipc_endpoint instead.
void bind_loopback_ipc (void *socket_, char *my_endpoint_, size_t len_);

#if defined(ZLINK_HAVE_IPC)
// utility function to create a random IPC endpoint, similar to what a ipc://*
// wildcard binding does, but in a way it can be reused for multiple binds
// TODO also add a len parameter here
void make_random_ipc_endpoint (char *out_endpoint_);
#endif
