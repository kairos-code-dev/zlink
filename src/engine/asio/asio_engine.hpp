/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_ASIO_ENGINE_HPP_INCLUDED__
#define __ZLINK_ASIO_ENGINE_HPP_INCLUDED__

#include "core/poller.hpp"
#if defined ZLINK_IOTHREAD_POLLER_USE_ASIO

#include <boost/asio.hpp>

#include <memory>
#include <vector>
#include <deque>

#include "utils/fd.hpp"
#include "engine/i_engine.hpp"
#include "core/options.hpp"
#include "core/endpoint.hpp"
#include "protocol/i_encoder.hpp"
#include "protocol/i_decoder.hpp"
#include "core/msg.hpp"
#include "protocol/metadata.hpp"
#include "engine/asio/i_asio_transport.hpp"

namespace zlink
{
class io_thread_t;
class session_base_t;
class i_asio_transport;

//  True Proactor Mode ASIO Engine
//
//  This engine uses Boost.Asio's async_read/async_write for true
//  asynchronous I/O operations, as opposed to the reactor mode
//  (async_wait for readiness) used by asio_poller.
//
//  The engine manages read/write buffers internally and handles
//  completion callbacks to drive the ZMP protocol.

class asio_engine_t : public i_engine
{
  public:
    asio_engine_t (fd_t fd_,
                   const options_t &options_,
                   const endpoint_uri_pair_t &endpoint_uri_pair_,
                   std::unique_ptr<i_asio_transport> transport_ =
                     std::unique_ptr<i_asio_transport> ());
    ~asio_engine_t () ZLINK_OVERRIDE;

    //  i_engine interface implementation.
    bool has_handshake_stage () ZLINK_OVERRIDE { return _has_handshake_stage; }
    void plug (zlink::io_thread_t *io_thread_,
               zlink::session_base_t *session_) ZLINK_OVERRIDE;
    void terminate () ZLINK_OVERRIDE;
    bool restart_input () ZLINK_OVERRIDE;
    void restart_output () ZLINK_OVERRIDE;
    const endpoint_uri_pair_t &get_endpoint () const ZLINK_OVERRIDE;

  protected:
    typedef metadata_t::dict_t properties_t;
    bool init_properties (properties_t &properties_);

    //  Function to handle network disconnections.
    virtual void error (error_reason_t reason_);

    int pull_msg_from_session (msg_t *msg_);
    int push_msg_to_session (msg_t *msg_);

    virtual int decode_and_push (msg_t *msg_);
    int push_one_then_decode_and_push (msg_t *msg_);

    virtual bool handshake () { return true; }
    virtual void plug_internal () {}

    virtual int process_command_message (msg_t *msg_)
    {
        LIBZLINK_UNUSED (msg_);
        return -1;
    }
    virtual int produce_ping_message (msg_t *msg_)
    {
        LIBZLINK_UNUSED (msg_);
        return -1;
    }
    virtual int process_heartbeat_message (msg_t *msg_)
    {
        LIBZLINK_UNUSED (msg_);
        return -1;
    }
    virtual int produce_pong_message (msg_t *msg_)
    {
        LIBZLINK_UNUSED (msg_);
        return -1;
    }

    //  Build protocol-specific header for gather write.
    //  Returns true on success and sets header_size_.
    virtual bool build_gather_header (const msg_t &msg_,
                                      unsigned char *buffer_,
                                      size_t buffer_size_,
                                      size_t &header_size_);

    //  Start asynchronous read operation
    void start_async_read ();

    //  Start asynchronous write operation
    void start_async_write ();

    //  Speculative (synchronous) write attempt.
    //  Tries to write immediately using transport->write_some().
    //  Falls back to async write if would_block or partial write occurs.
    void speculative_write ();

    //  Handle read completion
    void on_read_complete (const boost::system::error_code &ec,
                           std::size_t bytes_transferred);

    //  Handle write completion
    void on_write_complete (const boost::system::error_code &ec,
                            std::size_t bytes_transferred);

    //  Set up handshake timer
    void set_handshake_timer ();

    //  Cancel handshake timer
    void cancel_handshake_timer ();

    //  Timer callback
    void on_timer (int id_, const boost::system::error_code &ec);

    //  Access to session and socket
    session_base_t *session () { return _session; }
    socket_base_t *socket () { return _socket; }
    i_asio_transport *transport () { return _transport.get (); }
    bool is_handshaking () const { return _handshaking; }

    const options_t _options;

    //  Buffers for async I/O
    unsigned char *_inpos;
    size_t _insize;
    i_decoder *_decoder;
    //  True when _inpos/_insize refer to data read into the decoder buffer.
    bool _input_in_decoder_buffer;

    unsigned char *_outpos;
    size_t _outsize;
    i_encoder *_encoder;

    int (asio_engine_t::*_next_msg) (msg_t *msg_);
    int (asio_engine_t::*_process_msg) (msg_t *msg_);

    //  Metadata to be attached to received messages. May be NULL.
    metadata_t *_metadata;

    //  True iff the engine couldn't consume the last decoded message.
    bool _input_stopped;

    //  True iff the engine doesn't have any message to encode.
    bool _output_stopped;

    //  Representation of the connected endpoints.
    const endpoint_uri_pair_t _endpoint_uri_pair;

    //  ID of the handshake timer
    enum
    {
        handshake_timer_id = 0x40
    };

    //  True if handshake timer is running.
    bool _has_handshake_timer;

    //  Heartbeat stuff
    enum
    {
        heartbeat_ivl_timer_id = 0x80,
        heartbeat_timeout_timer_id = 0x81,
        heartbeat_ttl_timer_id = 0x82
    };
    bool _has_ttl_timer;
    bool _has_timeout_timer;
    bool _has_heartbeat_timer;

    const std::string _peer_address;

    //  Indicate if engine has a handshake stage
    bool _has_handshake_stage;

    //  Add a timer using ASIO
    void add_timer (int timeout_, int id_);

    //  Cancel a timer
    void cancel_timer (int id_);

    //  Start transport handshake if required
    void start_transport_handshake ();

    //  Transport handshake completion handler
    void on_transport_handshake (const boost::system::error_code &ec);

    //  Complete protocol handshake immediately for raw/handshake-less engines.
    void complete_handshake ();

  private:
    //  Process incoming data after async read completes
    bool process_input ();

    //  Fill output buffer and start async write
    void process_output ();

    //  Internal implementation of restart_input
    bool restart_input_internal ();

    //  Attempt a synchronous read to drain immediately available data.
    //  Returns true if a read was attempted or an error occurred.
    bool speculative_read ();

    //  Prepare output buffer from encoder (called by speculative_write).
    //  Returns true if data is available in _outpos/_outsize.
    bool prepare_output_buffer ();

    //  Prepare gather write (header + body) for large messages.
    //  Returns true if gather write was scheduled or output was stopped.
    bool prepare_gather_output ();

    //  Finalize message state after gather write completion.
    void finish_gather_output ();

    //  Unplug the engine from the session.
    void unplug ();

    //  Pointer to io_context (set during plug())
    boost::asio::io_context *_io_context;

    //  Transport abstraction (TCP/SSL/etc)
    std::unique_ptr<i_asio_transport> _transport;

    //  Timers for handshake and heartbeat (allocated during plug())
    std::unique_ptr<boost::asio::steady_timer> _timer;

    //  Current timer ID
    int _current_timer_id;

    //  Internal read buffer for async operations
    static const size_t read_buffer_size = 8192;
    std::vector<unsigned char> _read_buffer;

    //  Internal write buffer for async operations
    std::vector<unsigned char> _write_buffer;

    //  True Proactor Pattern: Pending buffers for backpressure handling.
    //  When _input_stopped is true, incoming data is stored here instead of
    //  being processed. This allows async_read to continue without blocking,
    //  eliminating unnecessary recvfrom() calls and EAGAIN errors.
    std::deque<std::vector<unsigned char>> _pending_buffers;

    //  Backpressure read buffers (avoid extra copy into _pending_buffers).
    std::vector<std::vector<unsigned char> > _pending_buffer_pool;
    std::vector<unsigned char> _pending_read_buffer;
    bool _read_from_pending_pool;
    enum { pending_buffer_pool_max = 4 };

    //  Total bytes in _pending_buffers (O(1) tracking instead of O(n) iteration)
    size_t _total_pending_bytes;

    //  Maximum total size of pending buffers (10MB default)
    static const size_t max_pending_buffer_size = 10 * 1024 * 1024;

    fd_t _fd;

    bool _plugged;

    //  When true, we are still trying to determine whether
    //  the peer is using versioned protocol, and if so, which
    //  version. When false, normal message flow has started.
    bool _handshaking;

    msg_t _tx_msg;

    bool _io_error;

    //  True if async read is in progress
    bool _read_pending;

    //  True if async write is in progress
    bool _write_pending;
    bool _async_zero_copy;
    bool _async_gather;

    unsigned char _gather_header[64];
    size_t _gather_header_size;
    const unsigned char *_gather_body;
    size_t _gather_body_size;

    //  True if engine is being terminated (prevents callback processing)
    bool _terminating;

    //  Buffer pointer for current async read (points to where data was read)
    unsigned char *_read_buffer_ptr;

    //  The session this engine is attached to.
    zlink::session_base_t *_session;

    //  Socket
    zlink::socket_base_t *_socket;

    ZLINK_NON_COPYABLE_NOR_MOVABLE (asio_engine_t)
};
}  // namespace zlink

#endif  // ZLINK_IOTHREAD_POLLER_USE_ASIO

#endif  // __ZLINK_ASIO_ENGINE_HPP_INCLUDED__
