/* SPDX-License-Identifier: MPL-2.0 */

/*
 * Test suite for the ASIO WebSocket infrastructure (Phase 3: WebSocket Support)
 *
 * These tests verify that the Boost.Beast WebSocket layer works correctly
 * for WebSocket connections. The actual ZMQ WebSocket integration will
 * use ws_transport_t which wraps these primitives.
 *
 * NOTE: These tests require Boost.Beast and are only compiled when
 * ZMQ_HAVE_ASIO_WS is defined.
 *
 * TODO (Phase 7): Add wss:// (WebSocket over SSL/TLS) tests.
 *   This requires combining SSL streams with WebSocket streams:
 *   websocket::stream<ssl::stream<tcp::socket>>
 */

#include "testutil.hpp"
#include "testutil_unity.hpp"

#include <unity.h>

#if defined ZMQ_IOTHREAD_POLLER_USE_ASIO && defined ZMQ_HAVE_ASIO_WS

#include <boost/asio.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>

#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <string.h>
#include <atomic>
#include <vector>
#include <thread>

#ifndef ZMQ_HAVE_WINDOWS
#include <unistd.h>
#else
#include <direct.h>
#endif

namespace beast = boost::beast;
namespace websocket = beast::websocket;
namespace net = boost::asio;
using tcp = net::ip::tcp;

//  Include test certificates (embedded PEM strings)
#include "certs/test_certs.hpp"

struct tls_files_t
{
    std::string dir;
    std::string ca_cert;
    std::string server_cert;
    std::string server_key;
};

static bool write_pem_file (const std::string &path_, const char *pem_)
{
    FILE *fp = fopen (path_.c_str (), "wb");
    if (!fp)
        return false;
    const size_t len = strlen (pem_);
    const size_t written = fwrite (pem_, 1, len, fp);
    fclose (fp);
    return written == len;
}

static tls_files_t create_tls_files ()
{
    tls_files_t files;
#ifdef ZMQ_HAVE_WINDOWS
    char tmp_dir[MAX_PATH] = "";
    TEST_ASSERT_SUCCESS_RAW_ERRNO (tmpnam_s (tmp_dir));
    TEST_ASSERT_SUCCESS_RAW_ERRNO (_mkdir (tmp_dir));
    files.dir.assign (tmp_dir);
#else
    char tmp_dir[] = "zmq_tls_XXXXXX";
    char *dir = mkdtemp (tmp_dir);
    TEST_ASSERT_NOT_NULL (dir);
    files.dir.assign (dir);
#endif

    files.ca_cert = files.dir + "/ca.crt";
    files.server_cert = files.dir + "/server.crt";
    files.server_key = files.dir + "/server.key";

    TEST_ASSERT_TRUE (write_pem_file (files.ca_cert,
                                      zmq::test_certs::ca_cert_pem));
    TEST_ASSERT_TRUE (write_pem_file (files.server_cert,
                                      zmq::test_certs::server_cert_pem));
    TEST_ASSERT_TRUE (write_pem_file (files.server_key,
                                      zmq::test_certs::server_key_pem));

    return files;
}

static void cleanup_tls_files (const tls_files_t &files_)
{
    remove (files_.ca_cert.c_str ());
    remove (files_.server_cert.c_str ());
    remove (files_.server_key.c_str ());
#ifdef ZMQ_HAVE_WINDOWS
    _rmdir (files_.dir.c_str ());
#else
    rmdir (files_.dir.c_str ());
#endif
}

void setUp ()
{
}

void tearDown ()
{
}

//  Test 1: WebSocket stream creation
void test_ws_stream_creation ()
{
    net::io_context io_context;

    //  Create WebSocket stream over TCP socket
    websocket::stream<tcp::socket> ws_stream (io_context);

    //  Check that the lowest layer (TCP socket) exists
    TEST_ASSERT_FALSE (ws_stream.next_layer ().is_open ());

    //  Configure stream for binary mode
    ws_stream.binary (true);
}

//  Test 2: WebSocket stream configuration options
void test_ws_stream_options ()
{
    net::io_context io_context;
    websocket::stream<tcp::socket> ws_stream (io_context);

    //  Test binary mode setting
    ws_stream.binary (true);

    //  Test auto-fragmentation
    ws_stream.auto_fragment (false);

    //  Test message size limit
    ws_stream.read_message_max (16 * 1024 * 1024);  // 16 MB
}

//  Test 3: TCP connection baseline (without WebSocket upgrade)
void test_tcp_connection_baseline ()
{
    net::io_context io_context;

    //  Create acceptor on ephemeral port
    tcp::acceptor acceptor (io_context, tcp::endpoint (
                              net::ip::make_address ("127.0.0.1"), 0));
    auto endpoint = acceptor.local_endpoint ();

    //  Server socket
    tcp::socket server_socket (io_context);

    //  Client socket
    tcp::socket client_socket (io_context);

    //  State tracking
    std::atomic<bool> all_done{false};
    std::atomic<int> completed{0};
    boost::system::error_code accept_ec, connect_ec;

    //  Async accept
    acceptor.async_accept (server_socket,
                           [&] (const boost::system::error_code &ec) {
                               accept_ec = ec;
                               if (++completed >= 2)
                                   all_done = true;
                           });

    //  Async connect
    client_socket.async_connect (endpoint,
                                 [&] (const boost::system::error_code &ec) {
                                     connect_ec = ec;
                                     if (++completed >= 2)
                                         all_done = true;
                                 });

    //  Run io_context with timeout using thread
    std::thread io_thread ([&] () { io_context.run (); });

    //  Wait for completion with timeout (5 seconds)
    for (int i = 0; i < 50 && !all_done; ++i) {
        msleep (100);
    }

    if (!all_done) {
        io_context.stop ();
    }
    io_thread.join ();

    TEST_ASSERT_TRUE_MESSAGE (all_done.load (), "TCP connection timed out");
    TEST_ASSERT_FALSE_MESSAGE (accept_ec.failed (), accept_ec.message ().c_str ());
    TEST_ASSERT_FALSE_MESSAGE (connect_ec.failed (),
                               connect_ec.message ().c_str ());
    TEST_ASSERT_TRUE (server_socket.is_open ());
    TEST_ASSERT_TRUE (client_socket.is_open ());
}

//  Test 4: WebSocket handshake (client/server)
void test_ws_handshake ()
{
    net::io_context io_context;

    //  Create work guard to keep io_context running
    auto work = net::make_work_guard (io_context);

    //  Create acceptor on ephemeral port
    tcp::acceptor acceptor (io_context, tcp::endpoint (
                              net::ip::make_address ("127.0.0.1"), 0));
    auto endpoint = acceptor.local_endpoint ();

    //  Build host string for handshake
    std::string host =
      "127.0.0.1:" + std::to_string (endpoint.port ());

    //  Server WebSocket stream
    websocket::stream<tcp::socket> server_ws (io_context);

    //  Client WebSocket stream
    websocket::stream<tcp::socket> client_ws (io_context);

    //  State tracking
    std::atomic<bool> all_done{false};
    std::atomic<int> stage{0};
    boost::system::error_code server_hs_ec, client_hs_ec;

    //  Set up the chain of operations
    acceptor.async_accept (server_ws.next_layer (),
                           [&] (const boost::system::error_code &ec) {
                               if (ec) {
                                   all_done = true;
                                   work.reset ();
                                   return;
                               }

                               //  Server: accept WebSocket upgrade
                               server_ws.async_accept (
                                 [&] (const boost::system::error_code &ec) {
                                     server_hs_ec = ec;
                                     if (++stage >= 2) {
                                         all_done = true;
                                         work.reset ();
                                     }
                                 });
                           });

    client_ws.next_layer ().async_connect (
      endpoint, [&] (const boost::system::error_code &ec) {
          if (ec) {
              all_done = true;
              work.reset ();
              return;
          }

          //  Client: perform WebSocket handshake
          client_ws.async_handshake (host, "/zmq",
                                     [&] (const boost::system::error_code &ec) {
                                         client_hs_ec = ec;
                                         if (++stage >= 2) {
                                             all_done = true;
                                             work.reset ();
                                         }
                                     });
      });

    //  Run io_context in a separate thread
    std::thread io_thread ([&] () { io_context.run (); });

    //  Wait for completion with timeout (5 seconds)
    for (int i = 0; i < 50 && !all_done; ++i) {
        msleep (100);
    }

    if (!all_done) {
        work.reset ();
        io_context.stop ();
    }
    io_thread.join ();

    TEST_ASSERT_TRUE_MESSAGE (all_done.load (), "WebSocket handshake timed out");
    TEST_ASSERT_FALSE_MESSAGE (server_hs_ec.failed (),
                               server_hs_ec.message ().c_str ());
    TEST_ASSERT_FALSE_MESSAGE (client_hs_ec.failed (),
                               client_hs_ec.message ().c_str ());

    //  Note: Don't call sync close - it blocks when io_context is stopped
    //  Let the streams destruct naturally
}

//  Test 5: WebSocket binary message exchange
void test_ws_binary_message ()
{
    net::io_context io_context;

    //  Create work guard to keep io_context running
    auto work = net::make_work_guard (io_context);

    //  Create acceptor on ephemeral port
    tcp::acceptor acceptor (io_context, tcp::endpoint (
                              net::ip::make_address ("127.0.0.1"), 0));
    auto endpoint = acceptor.local_endpoint ();

    std::string host =
      "127.0.0.1:" + std::to_string (endpoint.port ());

    //  Server and client WebSocket streams
    websocket::stream<tcp::socket> server_ws (io_context);
    websocket::stream<tcp::socket> client_ws (io_context);

    //  Enable binary mode for both
    server_ws.binary (true);
    client_ws.binary (true);

    //  Test data
    const unsigned char test_data[] = {0x01, 0x02, 0x03, 0x04, 0x05,
                                       0x00, 0xFF, 0xFE, 0xFD};
    const std::size_t test_data_size = sizeof (test_data);

    //  State tracking
    std::atomic<bool> all_done{false};
    beast::flat_buffer recv_buffer;
    boost::system::error_code write_ec, read_ec;
    std::size_t bytes_written = 0;
    std::size_t bytes_read = 0;

    //  Set up the chain of operations
    acceptor.async_accept (server_ws.next_layer (),
                           [&] (const boost::system::error_code &ec) {
                               if (ec) {
                                   all_done = true;
                                   work.reset ();
                                   return;
                               }

                               //  Server: accept WebSocket upgrade, then read
                               server_ws.async_accept (
                                 [&] (const boost::system::error_code &ec) {
                                     if (ec) {
                                         all_done = true;
                                         work.reset ();
                                         return;
                                     }

                                     //  Server reads binary data
                                     server_ws.async_read (
                                       recv_buffer,
                                       [&] (const boost::system::error_code &ec,
                                            std::size_t n) {
                                           read_ec = ec;
                                           bytes_read = n;
                                           all_done = true;
                                           work.reset ();
                                       });
                                 });
                           });

    client_ws.next_layer ().async_connect (
      endpoint, [&] (const boost::system::error_code &ec) {
          if (ec) {
              all_done = true;
              work.reset ();
              return;
          }

          //  Client: perform WebSocket handshake, then write
          client_ws.async_handshake (host, "/zmq",
                                     [&] (const boost::system::error_code &ec) {
                                         if (ec) {
                                             all_done = true;
                                             work.reset ();
                                             return;
                                         }

                                         //  Client writes binary data
                                         client_ws.async_write (
                                           net::buffer (test_data, test_data_size),
                                           [&] (const boost::system::error_code &ec,
                                                std::size_t n) {
                                               write_ec = ec;
                                               bytes_written = n;
                                           });
                                     });
      });

    //  Run io_context in a separate thread
    std::thread io_thread ([&] () { io_context.run (); });

    //  Wait for completion with timeout (5 seconds)
    for (int i = 0; i < 50 && !all_done; ++i) {
        msleep (100);
    }

    if (!all_done) {
        work.reset ();
        io_context.stop ();
    }
    io_thread.join ();

    TEST_ASSERT_TRUE_MESSAGE (all_done.load (),
                              "WebSocket message exchange timed out");
    TEST_ASSERT_FALSE_MESSAGE (write_ec.failed (), write_ec.message ().c_str ());
    TEST_ASSERT_FALSE_MESSAGE (read_ec.failed (), read_ec.message ().c_str ());
    TEST_ASSERT_EQUAL_UINT64 (test_data_size, bytes_written);
    TEST_ASSERT_EQUAL_UINT64 (test_data_size, bytes_read);

    //  Verify received data
    const auto &data = recv_buffer.data ();
    TEST_ASSERT_EQUAL_MEMORY (test_data, data.data (), test_data_size);

    //  Verify binary mode
    TEST_ASSERT_TRUE (server_ws.got_binary ());

    //  Note: Don't call sync close - it blocks when io_context is stopped
    //  Let the streams destruct naturally
}

//  ============================================================================
//  Phase 3-B: ZMQ WebSocket Integration Tests
//  ============================================================================
//
//  The following tests verify that ZMQ API works with WebSocket transport:
//  - zmq_bind("ws://...") should work without errno 93 (Protocol not supported)
//  - zmq_connect("ws://...") should work and establish connection
//  - Message exchange should work over WebSocket transport

#if defined ZMQ_HAVE_WS

//  Global ZMQ context for Phase 3-B tests
static void *g_ctx = NULL;

static void setup_zmq_ctx ()
{
    g_ctx = zmq_ctx_new ();
    TEST_ASSERT_NOT_NULL (g_ctx);
}

static void teardown_zmq_ctx ()
{
    if (g_ctx) {
        int rc = zmq_ctx_term (g_ctx);
        TEST_ASSERT_EQUAL_INT (0, rc);
        g_ctx = NULL;
    }
}

//  Test 6: ZMQ WebSocket bind should not return EPROTONOSUPPORT
void test_zmq_ws_bind ()
{
    setup_zmq_ctx ();

    void *socket = zmq_socket (g_ctx, ZMQ_PAIR);
    TEST_ASSERT_NOT_NULL (socket);

    //  Bind to WebSocket endpoint - this should NOT return -1 with errno 93
    int rc = zmq_bind (socket, "ws://127.0.0.1:*");
    if (rc == -1) {
        int err = zmq_errno ();
        char msg[256];
        snprintf (msg, sizeof (msg), "zmq_bind(ws://) failed with errno %d: %s",
                  err, zmq_strerror (err));
        TEST_FAIL_MESSAGE (msg);
    }
    TEST_ASSERT_EQUAL_INT (0, rc);

    //  Get the actual bound endpoint
    char endpoint[256];
    size_t endpoint_len = sizeof (endpoint);
    rc = zmq_getsockopt (socket, ZMQ_LAST_ENDPOINT, endpoint, &endpoint_len);
    TEST_ASSERT_EQUAL_INT (0, rc);

    //  Verify it's a ws:// endpoint
    TEST_ASSERT_TRUE (strncmp (endpoint, "ws://", 5) == 0);

    zmq_close (socket);
    teardown_zmq_ctx ();
}

//  Test 7: ZMQ WebSocket connect should not return EPROTONOSUPPORT
void test_zmq_ws_connect ()
{
    setup_zmq_ctx ();

    //  First create a bind socket
    void *bind_socket = zmq_socket (g_ctx, ZMQ_PAIR);
    TEST_ASSERT_NOT_NULL (bind_socket);

    int rc = zmq_bind (bind_socket, "ws://127.0.0.1:*");
    TEST_ASSERT_EQUAL_INT (0, rc);

    //  Get the actual bound endpoint
    char endpoint[256];
    size_t endpoint_len = sizeof (endpoint);
    rc = zmq_getsockopt (bind_socket, ZMQ_LAST_ENDPOINT, endpoint, &endpoint_len);
    TEST_ASSERT_EQUAL_INT (0, rc);

    //  Create connect socket
    void *connect_socket = zmq_socket (g_ctx, ZMQ_PAIR);
    TEST_ASSERT_NOT_NULL (connect_socket);

    //  Connect to the WebSocket endpoint
    rc = zmq_connect (connect_socket, endpoint);
    if (rc == -1) {
        int err = zmq_errno ();
        char msg[256];
        snprintf (msg, sizeof (msg),
                  "zmq_connect(ws://) failed with errno %d: %s", err,
                  zmq_strerror (err));
        TEST_FAIL_MESSAGE (msg);
    }
    TEST_ASSERT_EQUAL_INT (0, rc);

    zmq_close (connect_socket);
    zmq_close (bind_socket);
    teardown_zmq_ctx ();
}

//  Test 8: ZMQ WebSocket PAIR message exchange
void test_zmq_ws_pair_message ()
{
    setup_zmq_ctx ();

    //  Create bind socket
    void *bind_socket = zmq_socket (g_ctx, ZMQ_PAIR);
    TEST_ASSERT_NOT_NULL (bind_socket);

    int rc = zmq_bind (bind_socket, "ws://127.0.0.1:*");
    TEST_ASSERT_EQUAL_INT (0, rc);

    //  Get the actual bound endpoint
    char endpoint[256];
    size_t endpoint_len = sizeof (endpoint);
    rc = zmq_getsockopt (bind_socket, ZMQ_LAST_ENDPOINT, endpoint, &endpoint_len);
    TEST_ASSERT_EQUAL_INT (0, rc);

    //  Create connect socket
    void *connect_socket = zmq_socket (g_ctx, ZMQ_PAIR);
    TEST_ASSERT_NOT_NULL (connect_socket);

    rc = zmq_connect (connect_socket, endpoint);
    TEST_ASSERT_EQUAL_INT (0, rc);

    //  Give time for connection to establish (including WS handshake)
    msleep (200);

    //  Send a message from connect to bind
    const char *test_msg = "Hello WebSocket";
    rc = zmq_send (connect_socket, test_msg, strlen (test_msg), 0);
    TEST_ASSERT_EQUAL_INT (static_cast<int> (strlen (test_msg)), rc);

    //  Receive the message
    char recv_buf[256];
    rc = zmq_recv (bind_socket, recv_buf, sizeof (recv_buf), 0);
    TEST_ASSERT_EQUAL_INT (static_cast<int> (strlen (test_msg)), rc);
    recv_buf[rc] = '\0';
    TEST_ASSERT_EQUAL_STRING (test_msg, recv_buf);

    //  Send a message back from bind to connect
    const char *reply_msg = "Hello from bind";
    rc = zmq_send (bind_socket, reply_msg, strlen (reply_msg), 0);
    TEST_ASSERT_EQUAL_INT (static_cast<int> (strlen (reply_msg)), rc);

    //  Receive the reply
    rc = zmq_recv (connect_socket, recv_buf, sizeof (recv_buf), 0);
    TEST_ASSERT_EQUAL_INT (static_cast<int> (strlen (reply_msg)), rc);
    recv_buf[rc] = '\0';
    TEST_ASSERT_EQUAL_STRING (reply_msg, recv_buf);

    zmq_close (connect_socket);
    zmq_close (bind_socket);
    teardown_zmq_ctx ();
}

//  Test 9: ZMQ WebSocket PUB/SUB pattern
void test_zmq_ws_pubsub ()
{
    setup_zmq_ctx ();

    //  Create PUB socket
    void *pub_socket = zmq_socket (g_ctx, ZMQ_PUB);
    TEST_ASSERT_NOT_NULL (pub_socket);

    int rc = zmq_bind (pub_socket, "ws://127.0.0.1:*");
    TEST_ASSERT_EQUAL_INT (0, rc);

    //  Get the actual bound endpoint
    char endpoint[256];
    size_t endpoint_len = sizeof (endpoint);
    rc = zmq_getsockopt (pub_socket, ZMQ_LAST_ENDPOINT, endpoint, &endpoint_len);
    TEST_ASSERT_EQUAL_INT (0, rc);

    //  Create SUB socket
    void *sub_socket = zmq_socket (g_ctx, ZMQ_SUB);
    TEST_ASSERT_NOT_NULL (sub_socket);

    //  Subscribe to all messages
    rc = zmq_setsockopt (sub_socket, ZMQ_SUBSCRIBE, "", 0);
    TEST_ASSERT_EQUAL_INT (0, rc);

    rc = zmq_connect (sub_socket, endpoint);
    TEST_ASSERT_EQUAL_INT (0, rc);

    //  Give time for connection and subscription to propagate
    msleep (300);

    //  Send a message from PUB
    const char *test_msg = "WebSocket PubSub Test";
    rc = zmq_send (pub_socket, test_msg, strlen (test_msg), 0);
    TEST_ASSERT_EQUAL_INT (static_cast<int> (strlen (test_msg)), rc);

    //  Receive the message on SUB
    char recv_buf[256];
    rc = zmq_recv (sub_socket, recv_buf, sizeof (recv_buf), 0);
    TEST_ASSERT_EQUAL_INT (static_cast<int> (strlen (test_msg)), rc);
    recv_buf[rc] = '\0';
    TEST_ASSERT_EQUAL_STRING (test_msg, recv_buf);

    zmq_close (sub_socket);
    zmq_close (pub_socket);
    teardown_zmq_ctx ();
}

//  Test 10: ZMQ WebSocket with path
void test_zmq_ws_with_path ()
{
    setup_zmq_ctx ();

    void *socket = zmq_socket (g_ctx, ZMQ_PAIR);
    TEST_ASSERT_NOT_NULL (socket);

    //  Bind to WebSocket endpoint with custom path
    int rc = zmq_bind (socket, "ws://127.0.0.1:*/my/custom/path");
    TEST_ASSERT_EQUAL_INT (0, rc);

    //  Get the actual bound endpoint
    char endpoint[256];
    size_t endpoint_len = sizeof (endpoint);
    rc = zmq_getsockopt (socket, ZMQ_LAST_ENDPOINT, endpoint, &endpoint_len);
    TEST_ASSERT_EQUAL_INT (0, rc);

    //  Verify the path is included in the endpoint
    TEST_ASSERT_NOT_NULL (strstr (endpoint, "/my/custom/path"));

    zmq_close (socket);
    teardown_zmq_ctx ();
}

#if defined ZMQ_HAVE_WSS
void test_zmq_wss_pair_message ()
{
    setup_zmq_ctx ();

    const tls_files_t files = create_tls_files ();

    void *server = zmq_socket (g_ctx, ZMQ_PAIR);
    void *client = zmq_socket (g_ctx, ZMQ_PAIR);
    TEST_ASSERT_NOT_NULL (server);
    TEST_ASSERT_NOT_NULL (client);

    const int zero = 0;
    TEST_ASSERT_SUCCESS_ERRNO (
      zmq_setsockopt (server, ZMQ_LINGER, &zero, sizeof (zero)));
    TEST_ASSERT_SUCCESS_ERRNO (
      zmq_setsockopt (client, ZMQ_LINGER, &zero, sizeof (zero)));

    const int trust_system = 0;
    TEST_ASSERT_SUCCESS_ERRNO (zmq_setsockopt (
      client, ZMQ_TLS_TRUST_SYSTEM, &trust_system, sizeof (trust_system)));

    TEST_ASSERT_SUCCESS_ERRNO (zmq_setsockopt (
      server, ZMQ_TLS_CERT, files.server_cert.c_str (),
      files.server_cert.size ()));
    TEST_ASSERT_SUCCESS_ERRNO (
      zmq_setsockopt (server, ZMQ_TLS_KEY, files.server_key.c_str (),
                      files.server_key.size ()));
    TEST_ASSERT_SUCCESS_ERRNO (zmq_setsockopt (
      client, ZMQ_TLS_CA, files.ca_cert.c_str (), files.ca_cert.size ()));

    const char hostname[] = "localhost";
    TEST_ASSERT_SUCCESS_ERRNO (
      zmq_setsockopt (client, ZMQ_TLS_HOSTNAME, hostname, strlen (hostname)));

    //  Bind server to WSS endpoint
    int rc = zmq_bind (server, "wss://127.0.0.1:*");
    TEST_ASSERT_SUCCESS_ERRNO (rc);

    char endpoint[256];
    size_t endpoint_len = sizeof (endpoint);
    rc = zmq_getsockopt (server, ZMQ_LAST_ENDPOINT, endpoint, &endpoint_len);
    TEST_ASSERT_SUCCESS_ERRNO (rc);

    //  Connect client
    TEST_ASSERT_SUCCESS_ERRNO (zmq_connect (client, endpoint));

    send_string_expect_success (client, "wss-hello", 0);
    recv_string_expect_success (server, "wss-hello", 0);

    zmq_close (client);
    zmq_close (server);
    cleanup_tls_files (files);
    teardown_zmq_ctx ();
}
#endif  // ZMQ_HAVE_WSS

#endif  // ZMQ_HAVE_WS

#else  // !ZMQ_IOTHREAD_POLLER_USE_ASIO || !ZMQ_HAVE_ASIO_WS

void setUp ()
{
}

void tearDown ()
{
}

void test_asio_ws_not_enabled ()
{
    //  Skip tests when Asio WS is not enabled
    TEST_IGNORE_MESSAGE ("Asio WebSocket not enabled, skipping tests");
}

#endif  // ZMQ_IOTHREAD_POLLER_USE_ASIO && ZMQ_HAVE_ASIO_WS

int main ()
{
    setup_test_environment ();

    UNITY_BEGIN ();

#if defined ZMQ_IOTHREAD_POLLER_USE_ASIO && defined ZMQ_HAVE_ASIO_WS
    //  Phase 3: Beast WebSocket infrastructure tests
    RUN_TEST (test_ws_stream_creation);
    RUN_TEST (test_ws_stream_options);
    //  Network-based Beast tests
    RUN_TEST (test_tcp_connection_baseline);
    RUN_TEST (test_ws_handshake);
    RUN_TEST (test_ws_binary_message);

#if defined ZMQ_HAVE_WS
    //  Phase 3-B: ZMQ WebSocket API integration tests
    RUN_TEST (test_zmq_ws_bind);
    RUN_TEST (test_zmq_ws_connect);
    RUN_TEST (test_zmq_ws_pair_message);
    RUN_TEST (test_zmq_ws_pubsub);
    RUN_TEST (test_zmq_ws_with_path);
#if defined ZMQ_HAVE_WSS
    RUN_TEST (test_zmq_wss_pair_message);
#endif
#endif  // ZMQ_HAVE_WS

#else
    RUN_TEST (test_asio_ws_not_enabled);
#endif

    return UNITY_END ();
}
