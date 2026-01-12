/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZMQ_ASIO_ENGINE_HPP_INCLUDED__
#define __ZMQ_ASIO_ENGINE_HPP_INCLUDED__

#include "../poller.hpp"
#if defined ZMQ_IOTHREAD_POLLER_USE_ASIO

#include <boost/asio.hpp>
#if !defined ZMQ_HAVE_WINDOWS
#include <boost/asio/posix/stream_descriptor.hpp>
#endif

#include <memory>
#include <vector>

#include "../fd.hpp"
#include "../i_engine.hpp"
#include "../options.hpp"
#include "../endpoint.hpp"
#include "../i_encoder.hpp"
#include "../i_decoder.hpp"
#include "../msg.hpp"
#include "../metadata.hpp"

namespace zmq
{
class io_thread_t;
class session_base_t;
class mechanism_t;

//  Phase 1-C: True Proactor Mode ASIO Engine
//
//  This engine uses Boost.Asio's async_read/async_write for true
//  asynchronous I/O operations, as opposed to the reactor mode
//  (async_wait for readiness) used by asio_poller.
//
//  The engine manages read/write buffers internally and handles
//  completion callbacks to drive the ZMTP protocol.

class asio_engine_t : public i_engine
{
  public:
    asio_engine_t (fd_t fd_,
                   const options_t &options_,
                   const endpoint_uri_pair_t &endpoint_uri_pair_);
    ~asio_engine_t () ZMQ_OVERRIDE;

    //  i_engine interface implementation.
    bool has_handshake_stage () ZMQ_OVERRIDE { return _has_handshake_stage; }
    void plug (zmq::io_thread_t *io_thread_,
               zmq::session_base_t *session_) ZMQ_OVERRIDE;
    void terminate () ZMQ_OVERRIDE;
    bool restart_input () ZMQ_OVERRIDE;
    void restart_output () ZMQ_OVERRIDE;
    void zap_msg_available () ZMQ_OVERRIDE;
    const endpoint_uri_pair_t &get_endpoint () const ZMQ_OVERRIDE;

  protected:
    typedef metadata_t::dict_t properties_t;
    bool init_properties (properties_t &properties_);

    //  Function to handle network disconnections.
    virtual void error (error_reason_t reason_);

    int next_handshake_command (msg_t *msg_);
    int process_handshake_command (msg_t *msg_);

    int pull_msg_from_session (msg_t *msg_);
    int push_msg_to_session (msg_t *msg_);

    int pull_and_encode (msg_t *msg_);
    virtual int decode_and_push (msg_t *msg_);
    int push_one_then_decode_and_push (msg_t *msg_);

    virtual bool handshake () { return true; }
    virtual void plug_internal () {}

    virtual int process_command_message (msg_t *msg_)
    {
        LIBZMQ_UNUSED (msg_);
        return -1;
    }
    virtual int produce_ping_message (msg_t *msg_)
    {
        LIBZMQ_UNUSED (msg_);
        return -1;
    }
    virtual int process_heartbeat_message (msg_t *msg_)
    {
        LIBZMQ_UNUSED (msg_);
        return -1;
    }
    virtual int produce_pong_message (msg_t *msg_)
    {
        LIBZMQ_UNUSED (msg_);
        return -1;
    }

    //  Start asynchronous read operation
    void start_async_read ();

    //  Start asynchronous write operation
    void start_async_write ();

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

    mechanism_t *_mechanism;

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

  private:
    //  Process incoming data after async read completes
    bool process_input ();

    //  Fill output buffer and start async write
    void process_output ();

    //  Unplug the engine from the session.
    void unplug ();

    int write_credential (msg_t *msg_);

    void mechanism_ready ();

    //  Pointer to io_context (set during plug())
    boost::asio::io_context *_io_context;

#if defined ZMQ_HAVE_WINDOWS
    //  Windows: Use socket handle (allocated during plug())
    std::unique_ptr<boost::asio::ip::tcp::socket> _socket_handle;
#else
    //  POSIX: Use stream_descriptor for FD (allocated during plug())
    std::unique_ptr<boost::asio::posix::stream_descriptor> _stream_descriptor;
#endif

    //  Timers for handshake and heartbeat (allocated during plug())
    std::unique_ptr<boost::asio::steady_timer> _timer;

    //  Current timer ID
    int _current_timer_id;

    //  Internal read buffer for async operations
    static const size_t read_buffer_size = 8192;
    std::vector<unsigned char> _read_buffer;

    //  Internal write buffer for async operations
    std::vector<unsigned char> _write_buffer;

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

    //  True if engine is being terminated (prevents callback processing)
    bool _terminating;

    //  Buffer pointer for current async read (points to where data was read)
    unsigned char *_read_buffer_ptr;

    //  The session this engine is attached to.
    zmq::session_base_t *_session;

    //  Socket
    zmq::socket_base_t *_socket;

    ZMQ_NON_COPYABLE_NOR_MOVABLE (asio_engine_t)
};
}  // namespace zmq

#endif  // ZMQ_IOTHREAD_POLLER_USE_ASIO

#endif  // __ZMQ_ASIO_ENGINE_HPP_INCLUDED__
