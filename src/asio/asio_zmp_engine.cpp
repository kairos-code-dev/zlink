/* SPDX-License-Identifier: MPL-2.0 */

#include "precompiled.hpp"
#if defined ZMQ_IOTHREAD_POLLER_USE_ASIO

#include "asio_zmp_engine.hpp"
#include "../zmp_protocol.hpp"
#include "../zmp_encoder.hpp"
#include "../zmp_decoder.hpp"
#include "../err.hpp"
#include "../msg.hpp"
#include "../wire.hpp"
#include "../session_base.hpp"
#include "../socket_base.hpp"

#if defined ZMQ_HAVE_ASIO_SSL
#include <boost/asio/ssl.hpp>
#endif

#include <algorithm>

namespace
{
const size_t zmp_hello_min_body = 3;
const size_t zmp_hello_max_body = 3 + 255;
}

zmq::asio_zmp_engine_t::asio_zmp_engine_t (
  fd_t fd_,
  const options_t &options_,
  const endpoint_uri_pair_t &endpoint_uri_pair_) :
    asio_engine_t (fd_, options_, endpoint_uri_pair_),
    _hello_sent (false),
    _hello_received (false),
    _hello_header_bytes (0),
    _hello_body_bytes (0),
    _hello_body_len (0),
    _hello_send_size (0),
    _peer_routing_id_size (0),
    _subscription_required (false),
    _heartbeat_timeout (0)
{
    init_zmp_engine ();
}

zmq::asio_zmp_engine_t::asio_zmp_engine_t (
  fd_t fd_,
  const options_t &options_,
  const endpoint_uri_pair_t &endpoint_uri_pair_,
  std::unique_ptr<i_asio_transport> transport_) :
    asio_engine_t (fd_, options_, endpoint_uri_pair_, std::move (transport_)),
    _hello_sent (false),
    _hello_received (false),
    _hello_header_bytes (0),
    _hello_body_bytes (0),
    _hello_body_len (0),
    _hello_send_size (0),
    _peer_routing_id_size (0),
    _subscription_required (false),
    _heartbeat_timeout (0)
{
    init_zmp_engine ();
}

#if defined ZMQ_HAVE_ASIO_SSL
zmq::asio_zmp_engine_t::asio_zmp_engine_t (
  fd_t fd_,
  const options_t &options_,
  const endpoint_uri_pair_t &endpoint_uri_pair_,
  std::unique_ptr<i_asio_transport> transport_,
  std::unique_ptr<boost::asio::ssl::context> ssl_context_) :
    asio_engine_t (fd_, options_, endpoint_uri_pair_, std::move (transport_)),
    _hello_sent (false),
    _hello_received (false),
    _hello_header_bytes (0),
    _hello_body_bytes (0),
    _hello_body_len (0),
    _hello_send_size (0),
    _peer_routing_id_size (0),
    _subscription_required (false),
    _heartbeat_timeout (0),
    _ssl_context (std::move (ssl_context_))
{
    init_zmp_engine ();
}
#endif

zmq::asio_zmp_engine_t::~asio_zmp_engine_t ()
{
}

void zmq::asio_zmp_engine_t::init_zmp_engine ()
{
    _next_msg = static_cast<int (asio_engine_t::*) (msg_t *)> (
      &asio_zmp_engine_t::pull_msg_from_session);
    _process_msg = static_cast<int (asio_engine_t::*) (msg_t *)> (
      &asio_zmp_engine_t::decode_and_push);

    if (_options.heartbeat_interval > 0) {
        _heartbeat_timeout = _options.heartbeat_timeout;
        if (_heartbeat_timeout == -1)
            _heartbeat_timeout = _options.heartbeat_interval;
    }

    memset (_hello_recv, 0, sizeof (_hello_recv));
    memset (_hello_send, 0, sizeof (_hello_send));
    memset (_peer_routing_id, 0, sizeof (_peer_routing_id));
    _peer_routing_id_size = 0;
}

void zmq::asio_zmp_engine_t::plug_internal ()
{
    set_handshake_timer ();

    const size_t identity_len =
      std::min (static_cast<size_t> (_options.routing_id_size),
                static_cast<size_t> (255));
    const size_t body_len = zmp_hello_min_body + identity_len;
    _hello_send[0] = zmp_magic;
    _hello_send[1] = zmp_version;
    _hello_send[2] = zmp_flag_control;
    _hello_send[3] = 0;
    put_uint32 (_hello_send + 4, static_cast<uint32_t> (body_len));
    _hello_send[zmp_header_size + 0] = zmp_control_hello;
    _hello_send[zmp_header_size + 1] =
      static_cast<unsigned char> (_options.type);
    _hello_send[zmp_header_size + 2] =
      static_cast<unsigned char> (identity_len);
    if (identity_len > 0)
        memcpy (_hello_send + zmp_header_size + zmp_hello_min_body,
                _options.routing_id, identity_len);

    _hello_send_size = zmp_header_size + body_len;
    _outpos = _hello_send;
    _outsize = _hello_send_size;
    _hello_sent = true;

    if (_options.type == ZMQ_PUB || _options.type == ZMQ_XPUB)
        _subscription_required = true;

    start_async_read ();
    start_async_write ();
}

bool zmq::asio_zmp_engine_t::handshake ()
{
    if (!_hello_received) {
        if (!receive_hello ()) {
            errno = EAGAIN;
            return false;
        }
    }

    if (!_hello_sent)
        return false;

    _encoder = new (std::nothrow) zmp_encoder_t (_options.out_batch_size);
    alloc_assert (_encoder);

    _decoder = new (std::nothrow)
      zmp_decoder_t (_options.in_batch_size, _options.maxmsgsize);
    alloc_assert (_decoder);

    _input_in_decoder_buffer = false;

    if (_options.heartbeat_interval > 0 && !_has_heartbeat_timer) {
        add_timer (_options.heartbeat_interval, heartbeat_ivl_timer_id);
        _has_heartbeat_timer = true;
    }

    if (_has_handshake_stage) {
        session ()->engine_ready ();
        _has_handshake_stage = false;
    }

    if (_options.recv_routing_id) {
        msg_t routing_id;
        const int rc = routing_id.init_size (_peer_routing_id_size);
        errno_assert (rc == 0);
        if (_peer_routing_id_size > 0)
            memcpy (routing_id.data (), _peer_routing_id, _peer_routing_id_size);
        routing_id.set_flags (msg_t::routing_id);
        const int push_rc = session ()->push_msg (&routing_id);
        errno_assert (push_rc == 0);
        session ()->flush ();
    }

    properties_t properties;
    init_properties (properties);
    if (!properties.empty ()) {
        _metadata = new (std::nothrow) metadata_t (properties);
        alloc_assert (_metadata);
    }

    if (_has_handshake_timer) {
        cancel_timer (handshake_timer_id);
        _has_handshake_timer = false;
    }

    socket ()->event_handshake_succeeded (_endpoint_uri_pair, 0);

    if (_output_stopped)
        restart_output ();
    else
        start_async_write ();

    return true;
}

bool zmq::asio_zmp_engine_t::receive_hello ()
{
    while (_insize > 0) {
        if (_hello_header_bytes < zmp_header_size) {
            const size_t to_copy =
              std::min (_insize, zmp_header_size - _hello_header_bytes);
            memcpy (_hello_recv + _hello_header_bytes, _inpos, to_copy);
            _hello_header_bytes += to_copy;
            _inpos += to_copy;
            _insize -= to_copy;
            if (_hello_header_bytes < zmp_header_size)
                return false;
            _hello_body_len = get_uint32 (_hello_recv + 4);
            if (_hello_body_len < zmp_hello_min_body
                || _hello_body_len > zmp_hello_max_body) {
                errno = EPROTO;
                error (protocol_error);
                return false;
            }
        }

        if (_hello_body_bytes < _hello_body_len) {
            const size_t to_copy =
              std::min (_insize, static_cast<size_t> (_hello_body_len)
                                    - _hello_body_bytes);
            memcpy (_hello_recv + zmp_header_size + _hello_body_bytes, _inpos,
                    to_copy);
            _hello_body_bytes += to_copy;
            _inpos += to_copy;
            _insize -= to_copy;
            if (_hello_body_bytes < _hello_body_len)
                return false;
        }

        if (parse_hello (_hello_recv, zmp_header_size + _hello_body_len)) {
            _hello_received = true;
            return true;
        }

        return false;
    }

    return false;
}

bool zmq::asio_zmp_engine_t::parse_hello (const unsigned char *data_,
                                          size_t size_)
{
    if (size_ < zmp_header_size + zmp_hello_min_body) {
        errno = EPROTO;
        error (protocol_error);
        return false;
    }

    if (data_[0] != zmp_magic || data_[1] != zmp_version) {
        errno = EPROTO;
        socket ()->event_handshake_failed_protocol (
          session ()->get_endpoint (),
          ZMQ_PROTOCOL_ERROR_ZMTP_MALFORMED_COMMAND_HELLO);
        error (protocol_error);
        return false;
    }

    const unsigned char flags = data_[2];
    if (data_[3] != 0 || flags != zmp_flag_control) {
        errno = EPROTO;
        socket ()->event_handshake_failed_protocol (
          session ()->get_endpoint (),
          ZMQ_PROTOCOL_ERROR_ZMTP_MALFORMED_COMMAND_HELLO);
        error (protocol_error);
        return false;
    }

    const uint32_t body_len = get_uint32 (data_ + 4);
    if (body_len + zmp_header_size != size_) {
        errno = EPROTO;
        error (protocol_error);
        return false;
    }

    const unsigned char *body = data_ + zmp_header_size;
    if (body[0] != zmp_control_hello) {
        errno = EPROTO;
        error (protocol_error);
        return false;
    }

    const int peer_type = body[1];
    if (!is_socket_type_compatible (peer_type)) {
        errno = EPROTO;
        error (protocol_error);
        return false;
    }

    const unsigned char identity_len = body[2];
    if (body_len != static_cast<uint32_t> (zmp_hello_min_body + identity_len)) {
        errno = EPROTO;
        error (protocol_error);
        return false;
    }

    _peer_routing_id_size = identity_len;
    if (identity_len > 0)
        memcpy (_peer_routing_id, body + zmp_hello_min_body, identity_len);

    return true;
}

bool zmq::asio_zmp_engine_t::is_socket_type_compatible (int peer_type_) const
{
    switch (_options.type) {
        case ZMQ_DEALER:
            return peer_type_ == ZMQ_DEALER || peer_type_ == ZMQ_ROUTER;
        case ZMQ_ROUTER:
            return peer_type_ == ZMQ_DEALER || peer_type_ == ZMQ_ROUTER;
        case ZMQ_PUB:
            return peer_type_ == ZMQ_SUB || peer_type_ == ZMQ_XSUB;
        case ZMQ_SUB:
            return peer_type_ == ZMQ_PUB || peer_type_ == ZMQ_XPUB;
        case ZMQ_XPUB:
            return peer_type_ == ZMQ_SUB || peer_type_ == ZMQ_XSUB;
        case ZMQ_XSUB:
            return peer_type_ == ZMQ_PUB || peer_type_ == ZMQ_XPUB;
        case ZMQ_PAIR:
            return peer_type_ == ZMQ_PAIR;
        default:
            break;
    }
    return false;
}

int zmq::asio_zmp_engine_t::decode_and_push (msg_t *msg_)
{
    if (_has_timeout_timer) {
        _has_timeout_timer = false;
        cancel_timer (heartbeat_timeout_timer_id);
    }

    if (_has_ttl_timer) {
        _has_ttl_timer = false;
        cancel_timer (heartbeat_ttl_timer_id);
    }

    const unsigned char msg_flags = msg_->flags ();
    if (msg_flags & msg_t::command) {
        const int rc = process_command_message (msg_);
        if (rc != 0)
            return -1;
        return 0;
    }

    if ((msg_flags & msg_t::routing_id) && !_options.recv_routing_id) {
        int rc = msg_->close ();
        errno_assert (rc == 0);
        rc = msg_->init ();
        errno_assert (rc == 0);
        return 0;
    }

    if (_metadata)
        msg_->set_metadata (_metadata);

    if (session ()->push_msg (msg_) == -1) {
        if (errno == EAGAIN)
            _process_msg = static_cast<int (asio_engine_t::*) (msg_t *)> (
              &asio_zmp_engine_t::push_one_then_decode);
        return -1;
    }
    return 0;
}

int zmq::asio_zmp_engine_t::process_command_message (msg_t *msg_)
{
    if (msg_->size () < 1)
        return -1;

    const uint8_t type = *(static_cast<const uint8_t *> (msg_->data ()));
    if (type == zmp_control_heartbeat || type == zmp_control_heartbeat_ack)
        return process_heartbeat_message (msg_);

    errno = EPROTO;
    return -1;
}

int zmq::asio_zmp_engine_t::produce_ping_message (msg_t *msg_)
{
    int rc = msg_->init_size (1);
    errno_assert (rc == 0);
    msg_->set_flags (msg_t::command);
    *static_cast<unsigned char *> (msg_->data ()) = zmp_control_heartbeat;

    _next_msg = static_cast<int (asio_engine_t::*) (msg_t *)> (
      &asio_zmp_engine_t::pull_msg_from_session);
    if (!_has_timeout_timer && _heartbeat_timeout > 0) {
        add_timer (_heartbeat_timeout, heartbeat_timeout_timer_id);
        _has_timeout_timer = true;
    }

    return 0;
}

int zmq::asio_zmp_engine_t::process_heartbeat_message (msg_t *msg_)
{
    if (msg_->size () != 1) {
        errno = EPROTO;
        return -1;
    }
    return 0;
}

int zmq::asio_zmp_engine_t::push_one_then_decode (msg_t *msg_)
{
    const int rc = session ()->push_msg (msg_);
    if (rc == 0)
        _process_msg = static_cast<int (asio_engine_t::*) (msg_t *)> (
          &asio_zmp_engine_t::decode_and_push);
    return rc;
}

#endif  // ZMQ_IOTHREAD_POLLER_USE_ASIO
