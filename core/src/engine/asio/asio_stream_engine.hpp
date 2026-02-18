/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_ASIO_STREAM_ENGINE_HPP_INCLUDED__
#define __ZLINK_ASIO_STREAM_ENGINE_HPP_INCLUDED__

#include "core/poller.hpp"
#if defined ZLINK_IOTHREAD_POLLER_USE_ASIO

#include <boost/asio.hpp>

#include <memory>
#include <vector>

#include "core/options.hpp"
#include "core/endpoint.hpp"
#include "core/msg.hpp"
#include "engine/i_engine.hpp"
#include "engine/asio/i_asio_transport.hpp"
#include "engine/asio/handler_allocator.hpp"
#include "utils/fd.hpp"

#if defined ZLINK_HAVE_ASIO_SSL
#include <boost/asio/ssl.hpp>
#endif

namespace zlink
{
class io_thread_t;
class session_base_t;
class socket_base_t;

//  STREAM-specialized ASIO engine with cppserver-like receive/send buffers.
//  The engine keeps one async read and one async write pending and uses
//  main/flush send-buffer swapping to minimize lock/contention overhead.
class asio_stream_engine_t ZLINK_FINAL : public i_engine
{
  public:
    asio_stream_engine_t (fd_t fd_,
                          const options_t &options_,
                          const endpoint_uri_pair_t &endpoint_uri_pair_);
    asio_stream_engine_t (fd_t fd_,
                          const options_t &options_,
                          const endpoint_uri_pair_t &endpoint_uri_pair_,
                          std::unique_ptr<i_asio_transport> transport_);
#if defined ZLINK_HAVE_ASIO_SSL
    asio_stream_engine_t (fd_t fd_,
                          const options_t &options_,
                          const endpoint_uri_pair_t &endpoint_uri_pair_,
                          std::unique_ptr<i_asio_transport> transport_,
                          std::unique_ptr<boost::asio::ssl::context> ssl_context_);
#endif
    ~asio_stream_engine_t () ZLINK_OVERRIDE;

    bool has_handshake_stage () ZLINK_OVERRIDE;
    void plug (io_thread_t *io_thread_, session_base_t *session_) ZLINK_OVERRIDE;
    void terminate () ZLINK_OVERRIDE;
    bool restart_input () ZLINK_OVERRIDE;
    void restart_output () ZLINK_OVERRIDE;
    const endpoint_uri_pair_t &get_endpoint () const ZLINK_OVERRIDE;

    //  Direct IO: queue a message for sending, bypassing the outbound pipe.
    int queue_direct_send (class msg_t *msg_) ZLINK_OVERRIDE;

    //  Direct IO: restart a deferred async read after zero-copy msgs consumed.
    void restart_deferred_read () ZLINK_OVERRIDE;

  private:
    void start_transport_handshake ();
    void on_transport_handshake (const boost::system::error_code &ec);
    void complete_handshake ();

    void start_async_read ();
    void on_read_complete (const boost::system::error_code &ec,
                           std::size_t bytes_transferred);
    bool process_input_buffer ();
    bool push_one_frame (const unsigned char *data_, size_t size_);

    void fill_send_main_buffer ();
    void start_async_write ();
    void on_write_complete (const boost::system::error_code &ec,
                            std::size_t bytes_transferred);

    void unplug ();
    void error (i_engine::error_reason_t reason_);

    const options_t _options;
    const endpoint_uri_pair_t _endpoint_uri_pair;

    fd_t _fd;
    std::unique_ptr<i_asio_transport> _transport;
    boost::asio::io_context *_io_context;

    bool _has_handshake_stage;
    bool _handshaking;
    bool _plugged;
    bool _terminating;
    bool _io_error;
    bool _read_pending;
    bool _write_pending;
    bool _input_stopped;
    bool _output_stopped;

    std::vector<unsigned char> _recv_buffer;
    size_t _recv_offset;
    size_t _recv_size;
    size_t _recv_limit;

    std::vector<unsigned char> _send_buffer_main;
    std::vector<unsigned char> _send_buffer_flush;
    size_t _send_buffer_flush_offset;
    size_t _send_buffer_limit;

    handler_allocator _recv_allocator;
    handler_allocator _send_allocator;

    //  Echo loopback: when enabled, incoming data frames are copied directly
    //  to the send buffer without crossing through pipes/app-thread.
    //  Activated via ZLINK_STREAM_ECHO_LOOPBACK env var.
    bool _echo_loopback;

    //  Serialized raw echo: read → write (from same buffer) → read.
    //  No overlap, no memcpy, no heap handler allocation.
    //  Bypasses transport abstraction for maximum I/O throughput.
    void start_raw_echo_read ();
    void start_raw_echo_write ();
    size_t _echo_write_size;
    size_t _echo_write_offset;

    session_base_t *_session;
    socket_base_t *_socket;

    //  Direct IO pipe bypass: routing_id of this connection, cached at handshake.
    //  Non-zero activates the direct recv/send bypass path.
    uint32_t _connection_routing_id;

    //  Direct IO zero-copy: when true, async read was deferred to protect
    //  recv buffer data referenced by zero-copy messages in the socket's
    //  direct recv queue.  Cleared by restart_deferred_read().
    bool _read_deferred;

    //  Set to true during process_input_buffer when any frame used zero-copy.
    //  Controls whether reads need to be deferred in on_read_complete.
    bool _zero_copy_active;

    //  True when this engine runs on a worker io_thread (background worker).
    bool _on_worker_context;

    //  Per-engine reusable send buffer for worker write path (non-zero-copy).
    std::vector<unsigned char> _direct_write_buf;

#if defined ZLINK_HAVE_ASIO_SSL
    std::unique_ptr<boost::asio::ssl::context> _ssl_context;
#endif

    ZLINK_NON_COPYABLE_NOR_MOVABLE (asio_stream_engine_t)
};
}

#endif  // ZLINK_IOTHREAD_POLLER_USE_ASIO

#endif
