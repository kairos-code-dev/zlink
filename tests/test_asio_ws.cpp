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
 */

#include "testutil.hpp"
#include "testutil_unity.hpp"

#include <unity.h>

#if defined ZMQ_IOTHREAD_POLLER_USE_ASIO && defined ZMQ_HAVE_ASIO_WS

#include <boost/asio.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>

#include <string.h>
#include <atomic>
#include <vector>
#include <thread>

namespace beast = boost::beast;
namespace websocket = beast::websocket;
namespace net = boost::asio;
using tcp = net::ip::tcp;

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
                              net::ip::address::from_string ("127.0.0.1"), 0));
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
                              net::ip::address::from_string ("127.0.0.1"), 0));
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
                              net::ip::address::from_string ("127.0.0.1"), 0));
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
    //  Run basic tests
    RUN_TEST (test_ws_stream_creation);
    RUN_TEST (test_ws_stream_options);
    //  Network-based tests
    RUN_TEST (test_tcp_connection_baseline);
    RUN_TEST (test_ws_handshake);
    RUN_TEST (test_ws_binary_message);
#else
    RUN_TEST (test_asio_ws_not_enabled);
#endif

    return UNITY_END ();
}
