/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"
#if defined ZMQ_IOTHREAD_POLLER_USE_ASIO

#include "engine/asio/asio_zmp_engine.hpp"
#include "protocol/zmp_protocol.hpp"
#include "protocol/zmp_metadata.hpp"
#include "protocol/zmp_encoder.hpp"
#include "protocol/zmp_decoder.hpp"
#include "utils/err.hpp"
#include "core/msg.hpp"
#include "protocol/wire.hpp"
#include "core/session_base.hpp"
#include "sockets/socket_base.hpp"

#if defined ZMQ_HAVE_ASIO_SSL
#include <boost/asio/ssl.hpp>
#endif

#include <algorithm>
#include <limits.h>
#include <string.h>

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
    _ready_sent (false),
    _ready_received (false),
    _hello_header_bytes (0),
    _hello_body_bytes (0),
    _hello_body_len (0),
    _hello_send_size (0),
    _peer_routing_id_size (0),
    _subscription_required (false),
    _heartbeat_timeout (0),
    _last_error_code (0)
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
    _ready_sent (false),
    _ready_received (false),
    _hello_header_bytes (0),
    _hello_body_bytes (0),
    _hello_body_len (0),
    _hello_send_size (0),
    _peer_routing_id_size (0),
    _subscription_required (false),
    _heartbeat_timeout (0),
    _last_error_code (0)
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
    _ready_sent (false),
    _ready_received (false),
    _hello_header_bytes (0),
    _hello_body_bytes (0),
    _hello_body_len (0),
    _hello_send_size (0),
    _peer_routing_id_size (0),
    _subscription_required (false),
    _heartbeat_timeout (0),
    _last_error_code (0),
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
    _ready_send.clear ();
    _ready_sent = false;
    _ready_received = false;
    _heartbeat_ctx.clear ();
    _last_error_code = 0;
    _last_error_reason.clear ();
}

void zmq::asio_zmp_engine_t::set_last_error (uint8_t code_,
                                             const char *reason_)
{
    _last_error_code = code_;
    if (reason_ && *reason_)
        _last_error_reason.assign (reason_);
    else
        _last_error_reason.assign (zmp_error_reason (code_));
}

void zmq::asio_zmp_engine_t::send_error_frame (uint8_t code_,
                                               const char *reason_)
{
    i_asio_transport *tr = transport ();
    if (!tr || !tr->is_open ())
        return;

    const char *reason = reason_;
    if (!reason || !*reason)
        reason = zmp_error_reason (code_);

    const size_t reason_len =
      std::min (strlen (reason), static_cast<size_t> (UCHAR_MAX));
    const size_t body_len = 3 + reason_len;
    unsigned char buffer[zmp_header_size + 3 + UCHAR_MAX];

    buffer[0] = zmp_magic;
    buffer[1] = zmp_version;
    buffer[2] = zmp_flag_control;
    buffer[3] = 0;
    put_uint32 (buffer + 4, static_cast<uint32_t> (body_len));
    buffer[zmp_header_size + 0] = zmp_control_error;
    buffer[zmp_header_size + 1] = code_;
    buffer[zmp_header_size + 2] = static_cast<unsigned char> (reason_len);
    if (reason_len > 0)
        memcpy (buffer + zmp_header_size + 3, reason, reason_len);

    const size_t total = zmp_header_size + body_len;
    size_t offset = 0;
    while (offset < total) {
        const std::size_t written =
          tr->write_some (buffer + offset, total - offset);
        if (written == 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                break;
            break;
        }
        offset += written;
    }
}

void zmq::asio_zmp_engine_t::error (error_reason_t reason_)
{
    if (reason_ == timeout_error) {
        if (is_handshaking ())
            set_last_error (zmp_error_handshake_timeout, NULL);
        else if (_last_error_code == 0)
            set_last_error (zmp_error_internal, NULL);
    } else if (reason_ == protocol_error && _last_error_code == 0) {
        zmp_decoder_t *decoder = dynamic_cast<zmp_decoder_t *> (_decoder);
        if (decoder && decoder->error_code () != 0)
            set_last_error (decoder->error_code (), NULL);
        else
            set_last_error (zmp_error_internal, NULL);
    }

    if (reason_ == timeout_error || reason_ == protocol_error) {
        const uint8_t code =
          _last_error_code ? _last_error_code : zmp_error_internal;
        send_error_frame (code, _last_error_reason.c_str ());
    }

    asio_engine_t::error (reason_);
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
    _ready_send.clear ();
    _ready_send.reserve (_hello_send_size + zmp_header_size + 1);
    _ready_send.insert (_ready_send.end (), _hello_send,
                        _hello_send + _hello_send_size);

    std::vector<unsigned char> ready_body;
    ready_body.push_back (zmp_control_ready);
    if (_options.zmp_metadata)
        zmp_metadata::add_basic_properties (_options, ready_body);

    std::vector<unsigned char> ready_frame;
    ready_frame.resize (zmp_header_size + ready_body.size ());
    ready_frame[0] = zmp_magic;
    ready_frame[1] = zmp_version;
    ready_frame[2] = zmp_flag_control;
    ready_frame[3] = 0;
    put_uint32 (&ready_frame[4],
                static_cast<uint32_t> (ready_body.size ()));
    memcpy (&ready_frame[zmp_header_size], &ready_body[0],
            ready_body.size ());

    _ready_send.insert (_ready_send.end (), ready_frame.begin (),
                        ready_frame.end ());
    _outpos = &_ready_send[0];
    _outsize = _ready_send.size ();
    _hello_sent = true;
    _ready_sent = true;

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

    if (_encoder == NULL) {
        _encoder = new (std::nothrow) zmp_encoder_t (_options.out_batch_size);
        alloc_assert (_encoder);
    }

    if (_decoder == NULL) {
        _decoder = new (std::nothrow)
          zmp_decoder_t (_options.in_batch_size, _options.maxmsgsize);
        alloc_assert (_decoder);
        _input_in_decoder_buffer = false;
    }

    if (!process_handshake_input ())
        return false;

    if (!_ready_received)
        return false;

    if (_options.heartbeat_interval > 0 && !_has_heartbeat_timer) {
        add_timer (_options.heartbeat_interval, heartbeat_ivl_timer_id);
        _has_heartbeat_timer = true;
    }

    if (_has_handshake_stage) {
        session ()->engine_ready ();
        _has_handshake_stage = false;
    }

    session ()->set_peer_routing_id (_peer_routing_id, _peer_routing_id_size);

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

    if (_has_handshake_timer) {
        cancel_timer (handshake_timer_id);
        _has_handshake_timer = false;
    }

    socket ()->event_connection_ready (_endpoint_uri_pair, _peer_routing_id,
                                       _peer_routing_id_size);

    if (_output_stopped)
        restart_output ();
    else
        start_async_write ();

    _last_error_code = 0;
    _last_error_reason.clear ();
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
            if (_hello_body_len < zmp_hello_min_body) {
                set_last_error (zmp_error_internal, "hello too short");
                errno = EPROTO;
                error (protocol_error);
                return false;
            }
            if (_hello_body_len > zmp_hello_max_body) {
                set_last_error (zmp_error_body_too_large, "hello too large");
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
        set_last_error (zmp_error_internal, "hello too short");
        errno = EPROTO;
        error (protocol_error);
        return false;
    }

    if (data_[0] != zmp_magic) {
        set_last_error (zmp_error_invalid_magic, NULL);
        errno = EPROTO;
        socket ()->event_handshake_failed_protocol (
          session ()->get_endpoint (),
          ZMQ_PROTOCOL_ERROR_ZMP_MALFORMED_COMMAND_HELLO);
        error (protocol_error);
        return false;
    }

    if (data_[1] != zmp_version) {
        set_last_error (zmp_error_version_mismatch, NULL);
        errno = EPROTO;
        socket ()->event_handshake_failed_protocol (
          session ()->get_endpoint (),
          ZMQ_PROTOCOL_ERROR_ZMP_MALFORMED_COMMAND_HELLO);
        error (protocol_error);
        return false;
    }

    const unsigned char flags = data_[2];
    if (data_[3] != 0 || flags != zmp_flag_control) {
        set_last_error (zmp_error_flags_invalid, NULL);
        errno = EPROTO;
        socket ()->event_handshake_failed_protocol (
          session ()->get_endpoint (),
          ZMQ_PROTOCOL_ERROR_ZMP_MALFORMED_COMMAND_HELLO);
        error (protocol_error);
        return false;
    }

    const uint32_t body_len = get_uint32 (data_ + 4);
    if (body_len + zmp_header_size != size_) {
        set_last_error (zmp_error_internal, "hello length mismatch");
        errno = EPROTO;
        error (protocol_error);
        return false;
    }

    const unsigned char *body = data_ + zmp_header_size;
    if (body[0] != zmp_control_hello) {
        set_last_error (zmp_error_internal, "missing hello");
        errno = EPROTO;
        error (protocol_error);
        return false;
    }

    const int peer_type = body[1];
    if (!is_socket_type_compatible (peer_type)) {
        set_last_error (zmp_error_socket_type_mismatch, NULL);
        errno = EPROTO;
        error (protocol_error);
        return false;
    }

    const unsigned char identity_len = body[2];
    if (body_len != static_cast<uint32_t> (zmp_hello_min_body + identity_len)) {
        set_last_error (zmp_error_internal, "hello identity mismatch");
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

bool zmq::asio_zmp_engine_t::process_handshake_input ()
{
    if (_decoder == NULL)
        return true;

    const bool input_in_decoder_buffer = _input_in_decoder_buffer;
    int rc = 0;
    size_t processed = 0;

    while (_insize > 0) {
        unsigned char *decode_buf = _inpos;
        size_t decode_size = _insize;

        if (!input_in_decoder_buffer) {
            _decoder->get_buffer (&decode_buf, &decode_size);
            decode_size = std::min (_insize, decode_size);
            memcpy (decode_buf, _inpos, decode_size);
            _decoder->resize_buffer (decode_size);
        }

        rc = _decoder->decode (decode_buf, decode_size, processed);
        _inpos += processed;
        _insize -= processed;

        if (rc == 0 || rc == -1)
            break;

        msg_t *msg = _decoder->msg ();
        const unsigned char msg_flags = msg->flags ();
        if (msg_flags & msg_t::command) {
            rc = process_command_message (msg);
        } else {
            set_last_error (zmp_error_internal, "data before ready");
            errno = EPROTO;
            rc = -1;
        }

        if (rc == -1)
            break;

        if (_ready_received)
            break;
    }

    if (rc == -1) {
        if (errno != EAGAIN)
            error (protocol_error);
        return false;
    }

    return true;
}

int zmq::asio_zmp_engine_t::process_ready_message (msg_t *msg_)
{
    if (_ready_received) {
        set_last_error (zmp_error_internal, "duplicate ready");
        errno = EPROTO;
        return -1;
    }

    const size_t size = msg_->size ();
    if (size < 1) {
        set_last_error (zmp_error_internal, "ready too short");
        errno = EPROTO;
        return -1;
    }

    properties_t properties;
    init_properties (properties);

    if (size > 1) {
        metadata_t::dict_t peer_props;
        const unsigned char *data =
          static_cast<const unsigned char *> (msg_->data ());
        if (zmp_metadata::parse (data + 1, size - 1, peer_props) == -1) {
            set_last_error (zmp_error_internal, "ready metadata invalid");
            return -1;
        }
        properties.insert (peer_props.begin (), peer_props.end ());
    }

    if (!properties.empty ()) {
        _metadata = new (std::nothrow) metadata_t (properties);
        alloc_assert (_metadata);
    }

    _ready_received = true;
    return 0;
}

int zmq::asio_zmp_engine_t::process_error_message (msg_t *msg_)
{
    const size_t size = msg_->size ();
    if (size < 3) {
        set_last_error (zmp_error_internal, "error frame too short");
        errno = EPROTO;
        return -1;
    }

    const unsigned char *data =
      static_cast<const unsigned char *> (msg_->data ());
    const uint8_t code = data[1];
    const size_t reason_len = data[2];
    if (size < 3 + reason_len) {
        set_last_error (zmp_error_internal, "error frame invalid");
        errno = EPROTO;
        return -1;
    }

    set_last_error (code, NULL);
    errno = EPROTO;
    return -1;
}

int zmq::asio_zmp_engine_t::produce_pong_message (msg_t *msg_)
{
    const size_t ctx_len = _heartbeat_ctx.size ();
    const size_t size = 2 + ctx_len;
    int rc = msg_->init_size (size);
    errno_assert (rc == 0);
    msg_->set_flags (msg_t::command);

    unsigned char *data = static_cast<unsigned char *> (msg_->data ());
    data[0] = zmp_control_heartbeat_ack;
    data[1] = static_cast<unsigned char> (ctx_len);
    if (ctx_len > 0)
        memcpy (data + 2, &_heartbeat_ctx[0], ctx_len);

    _heartbeat_ctx.clear ();
    _next_msg = static_cast<int (asio_engine_t::*) (msg_t *)> (
      &asio_zmp_engine_t::pull_msg_from_session);
    return 0;
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

    if (!_ready_received) {
        set_last_error (zmp_error_internal, "data before ready");
        errno = EPROTO;
        return -1;
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
    if (type == zmp_control_ready)
        return process_ready_message (msg_);
    if (type == zmp_control_error)
        return process_error_message (msg_);

    set_last_error (zmp_error_internal, "unknown control");
    errno = EPROTO;
    return -1;
}

int zmq::asio_zmp_engine_t::produce_ping_message (msg_t *msg_)
{
    const size_t ctx_len = 0;
    const size_t size = 4 + ctx_len;
    int rc = msg_->init_size (size);
    errno_assert (rc == 0);
    msg_->set_flags (msg_t::command);
    unsigned char *data = static_cast<unsigned char *> (msg_->data ());
    data[0] = zmp_control_heartbeat;
    put_uint16 (data + 1, _options.heartbeat_ttl);
    data[3] = static_cast<unsigned char> (ctx_len);

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
    if (msg_->size () < 1) {
        set_last_error (zmp_error_internal, "heartbeat too short");
        errno = EPROTO;
        return -1;
    }

    const unsigned char *data =
      static_cast<const unsigned char *> (msg_->data ());
    const uint8_t type = data[0];

    if (type == zmp_control_heartbeat) {
        if (msg_->size () == 1)
            return 0;
        if (msg_->size () < 4) {
            set_last_error (zmp_error_internal, "heartbeat too short");
            errno = EPROTO;
            return -1;
        }

        const uint16_t ttl_ds = get_uint16 (data + 1);
        const uint16_t effective_ttl_ds =
          zmp_effective_ttl_ds (_options.heartbeat_ttl, ttl_ds);
        const size_t ctx_len = data[3];
        if (ctx_len > 16) {
            set_last_error (zmp_error_internal, "heartbeat ctx too long");
            errno = EPROTO;
            return -1;
        }
        if (msg_->size () != 4 + ctx_len) {
            set_last_error (zmp_error_internal, "heartbeat length mismatch");
            errno = EPROTO;
            return -1;
        }

        if (!_has_ttl_timer && effective_ttl_ds > 0) {
            add_timer (static_cast<int> (effective_ttl_ds) * 100,
                       heartbeat_ttl_timer_id);
            _has_ttl_timer = true;
        }

        _heartbeat_ctx.assign (data + 4, data + 4 + ctx_len);
        _next_msg = static_cast<int (asio_engine_t::*) (msg_t *)> (
          &asio_zmp_engine_t::produce_pong_message);
        restart_output ();
        return 0;
    }

    if (type == zmp_control_heartbeat_ack) {
        if (msg_->size () < 2) {
            set_last_error (zmp_error_internal, "heartbeat ack too short");
            errno = EPROTO;
            return -1;
        }
        const size_t ctx_len = data[1];
        if (msg_->size () != 2 + ctx_len) {
            set_last_error (zmp_error_internal, "heartbeat ack invalid");
            errno = EPROTO;
            return -1;
        }
        return 0;
    }

    set_last_error (zmp_error_internal, "unknown control");
    errno = EPROTO;
    return -1;
}

bool zmq::asio_zmp_engine_t::build_gather_header (const msg_t &msg_,
                                                  unsigned char *buffer_,
                                                  size_t buffer_size_,
                                                  size_t &header_size_)
{
    if (buffer_size_ < zmp_header_size)
        return false;

    const size_t size = msg_.size ();
    const unsigned char msg_flags = msg_.flags ();

    unsigned char flags = 0;
    if (msg_flags != 0) {
        if (msg_flags & msg_t::more)
            flags |= zmp_flag_more;
        if (msg_flags & msg_t::command)
            flags |= zmp_flag_control;
        if (msg_flags & msg_t::routing_id)
            flags |= zmp_flag_identity;

        const unsigned char cmd_type = msg_flags & CMD_TYPE_MASK;
        if (cmd_type == msg_t::subscribe)
            flags |= zmp_flag_subscribe;
        else if (cmd_type == msg_t::cancel)
            flags |= zmp_flag_cancel;
    }

    buffer_[0] = zmp_magic;
    buffer_[1] = zmp_version;
    buffer_[2] = flags;
    buffer_[3] = 0;
    put_uint32 (buffer_ + 4, static_cast<uint32_t> (size));

    header_size_ = zmp_header_size;
    return true;
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
