/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZMQ_ASIO_WS_ENGINE_HPP_INCLUDED__
#define __ZMQ_ASIO_WS_ENGINE_HPP_INCLUDED__

#include "../poller.hpp"
#if defined ZMQ_IOTHREAD_POLLER_USE_ASIO && defined ZMQ_HAVE_WS

#include <boost/asio.hpp>
#include <memory>
#include <string>
#include <vector>

#include "../fd.hpp"
#include "../i_engine.hpp"
#include "../options.hpp"
#include "../endpoint.hpp"
#include "../i_encoder.hpp"
#include "../i_decoder.hpp"
#include "../msg.hpp"
#include "../metadata.hpp"
#include "../zmp_metadata.hpp"
#include "i_asio_transport.hpp"

#if defined ZMQ_HAVE_ASIO_SSL
namespace boost
{
namespace asio
{
namespace ssl
{
class context;
}
}
}
#endif

namespace zmq
{

class io_thread_t;
class session_base_t;
class mechanism_t;

//  Protocol revisions for ZMTP over WebSocket
enum
{
    WS_ZMTP_1_0 = 0,
    WS_ZMTP_2_0 = 1,
    WS_ZMTP_3_x = 3
};

//  WebSocket ZMTP Engine
//
//  This engine implements ZMTP protocol over WebSocket transport.
//  It uses ws_transport_t for WebSocket framing and Beast I/O,
//  while implementing the ZMTP handshake and message protocol.
//
//  The engine handles:
//  1. WebSocket handshake (client or server via ws_transport)
//  2. ZMTP greeting and handshake over WebSocket frames
//  3. ZMTP message encoding/decoding with WebSocket transport

class asio_ws_engine_t ZMQ_FINAL : public i_engine
{
  public:
    //  Create WebSocket engine for an already-connected socket
    //  fd_: The socket file descriptor (TCP connection already established)
    //  options_: ZMQ socket options
    //  endpoint_uri_pair_: Local and remote endpoint URIs
    //  host_: WebSocket host for client handshake
    //  path_: WebSocket path (e.g., "/zmq")
    //  is_client_: true for client-side (zmq_connect), false for server-side (zmq_bind)
    asio_ws_engine_t (fd_t fd_,
                      const options_t &options_,
                      const endpoint_uri_pair_t &endpoint_uri_pair_,
                      bool is_client_,
                      std::unique_ptr<i_asio_transport> transport_);
#if defined ZMQ_HAVE_ASIO_SSL
    asio_ws_engine_t (fd_t fd_,
                      const options_t &options_,
                      const endpoint_uri_pair_t &endpoint_uri_pair_,
                      bool is_client_,
                      std::unique_ptr<i_asio_transport> transport_,
                      std::unique_ptr<boost::asio::ssl::context> ssl_context_);
#endif

    ~asio_ws_engine_t () ZMQ_OVERRIDE;

    //  i_engine interface implementation
    bool has_handshake_stage () ZMQ_OVERRIDE { return true; }
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

    void error (error_reason_t reason_);

    int next_handshake_command (msg_t *msg_);
    int process_handshake_command (msg_t *msg_);

    int pull_msg_from_session (msg_t *msg_);
    int push_msg_to_session (msg_t *msg_);

    int pull_and_encode (msg_t *msg_);
    int decode_and_push (msg_t *msg_);
    int push_one_then_decode_and_push (msg_t *msg_);

    int process_command_message (msg_t *msg_);
    int produce_ping_message (msg_t *msg_);
    int process_heartbeat_message (msg_t *msg_);
    int produce_pong_message (msg_t *msg_);
    void set_last_error (uint8_t code_, const char *reason_);
    void send_error_frame (uint8_t code_, const char *reason_);

  private:
    //  Start WebSocket handshake
    void start_ws_handshake ();

    //  WebSocket handshake completion callback
    void on_ws_handshake_complete (const boost::system::error_code &ec);

    //  Start ZMTP handshake after WebSocket handshake completes
    void start_zmtp_handshake ();
    void start_zmp_handshake ();

    //  ZMTP handshake methods
    bool handshake ();
    bool handshake_zmp ();
    int receive_greeting ();
    void receive_greeting_versioned ();

    typedef bool (asio_ws_engine_t::*handshake_fun_t) ();
    static handshake_fun_t select_handshake_fun (bool unversioned,
                                                 unsigned char revision,
                                                 unsigned char minor);

    bool handshake_v1_0_unversioned ();
    bool handshake_v1_0 ();
    bool handshake_v2_0 ();
    bool handshake_v3_x (bool downgrade_sub);
    bool handshake_v3_0 ();
    bool handshake_v3_1 ();

    bool receive_hello ();
    bool parse_hello (const unsigned char *data_, size_t size_);
    bool is_socket_type_compatible (int peer_type_) const;
    bool process_zmp_handshake_input ();
    int process_ready_message (msg_t *msg_);
    int process_error_message (msg_t *msg_);

    int routing_id_msg (msg_t *msg_);
    int process_routing_id_msg (msg_t *msg_);

    //  Async I/O methods
    void start_async_read ();
    void start_async_write ();

    //  Speculative (synchronous) write attempt.
    //  Tries to write immediately using transport->write_some().
    //  Falls back to async write if would_block or partial write occurs.
    void speculative_write ();

    //  Prepare output buffer from encoder.
    //  Returns true if data is available in _outpos/_outsize.
    bool prepare_output_buffer ();

    void on_read_complete (const boost::system::error_code &ec,
                           std::size_t bytes_transferred);
    void on_write_complete (const boost::system::error_code &ec,
                            std::size_t bytes_transferred);

    //  Internal data processing
    bool process_input ();
    void process_output ();

    void unplug ();
    void mechanism_ready ();

    int write_credential (msg_t *msg_);

    //  Timer management
    void add_timer (int timeout_, int id_);
    void cancel_timer (int id_);
    void set_handshake_timer ();
    void cancel_handshake_timer ();
    void on_timer (int id_, const boost::system::error_code &ec);

    //  WebSocket transport layer
    std::unique_ptr<i_asio_transport> _transport;

    //  WebSocket-specific data
    bool _is_client;
    bool _ws_handshake_complete;

    //  Session and socket
    session_base_t *_session;
    socket_base_t *_socket;

    //  File descriptor for initial open
    fd_t _fd;

    //  IO context pointer (set during plug)
    boost::asio::io_context *_io_context;

    //  Options
    const options_t _options;

    //  Endpoint pair
    const endpoint_uri_pair_t _endpoint_uri_pair;

    //  Peer address string
    const std::string _peer_address;

    //  Buffers for ZMTP I/O
    unsigned char *_inpos;
    size_t _insize;
    i_decoder *_decoder;
    bool _input_in_decoder_buffer;

    unsigned char *_outpos;
    size_t _outsize;
    i_encoder *_encoder;

    //  ZMTP mechanism
    mechanism_t *_mechanism;

    //  Message function pointers
    int (asio_ws_engine_t::*_next_msg) (msg_t *msg_);
    int (asio_ws_engine_t::*_process_msg) (msg_t *msg_);

    //  Metadata attached to received messages
    metadata_t *_metadata;

    //  State flags
    bool _plugged;
    bool _handshaking;
    bool _input_stopped;
    bool _output_stopped;
    bool _io_error;
    bool _read_pending;
    bool _write_pending;
    bool _terminating;
    uint8_t _last_error_code;
    std::string _last_error_reason;

    //  Internal read buffer
    static const size_t read_buffer_size = 8192;
    std::vector<unsigned char> _read_buffer;
    unsigned char *_read_buffer_ptr;

    //  Internal write buffer
    std::vector<unsigned char> _write_buffer;

    //  Outgoing message
    msg_t _tx_msg;

    //  ZMTP greeting
    static const size_t signature_size = 10;
    static const size_t v2_greeting_size = 12;
    static const size_t v3_greeting_size = 64;
    static const size_t zmp_hello_buf_size = 272;

    size_t _greeting_size;
    unsigned char _greeting_recv[v3_greeting_size];
    unsigned char _greeting_send[v3_greeting_size];
    unsigned int _greeting_bytes_read;

    bool _zmp_mode;
    bool _hello_sent;
    bool _hello_received;
    bool _ready_sent;
    bool _ready_received;
    size_t _hello_header_bytes;
    size_t _hello_body_bytes;
    uint32_t _hello_body_len;
    unsigned char _hello_recv[zmp_hello_buf_size];
    unsigned char _hello_send[zmp_hello_buf_size];
    size_t _hello_send_size;
    std::vector<unsigned char> _ready_send;
    unsigned char _peer_routing_id[256];
    size_t _peer_routing_id_size;

    bool _subscription_required;
    int _heartbeat_timeout;
    std::vector<unsigned char> _heartbeat_ctx;

    //  Routing ID message
    msg_t _routing_id_msg;

    //  PONG message (for heartbeat)
    msg_t _pong_msg;

#if defined ZMQ_HAVE_ASIO_SSL
    std::unique_ptr<boost::asio::ssl::context> _ssl_context;
#endif

    //  Timer
    std::unique_ptr<boost::asio::steady_timer> _timer;
    int _current_timer_id;

    //  Timer IDs
    enum
    {
        handshake_timer_id = 0x40,
        heartbeat_ivl_timer_id = 0x80,
        heartbeat_timeout_timer_id = 0x81,
        heartbeat_ttl_timer_id = 0x82
    };

    bool _has_handshake_timer;
    bool _has_ttl_timer;
    bool _has_timeout_timer;
    bool _has_heartbeat_timer;

    ZMQ_NON_COPYABLE_NOR_MOVABLE (asio_ws_engine_t)
};

}  // namespace zmq

#endif  // ZMQ_IOTHREAD_POLLER_USE_ASIO && ZMQ_HAVE_WS

#endif  // __ZMQ_ASIO_WS_ENGINE_HPP_INCLUDED__
