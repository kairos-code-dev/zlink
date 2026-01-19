/* SPDX-License-Identifier: MPL-2.0 */

#include "precompiled.hpp"
#if defined ZMQ_IOTHREAD_POLLER_USE_ASIO

#include "asio_zmtp_engine.hpp"
#if defined ZMQ_HAVE_ASIO_SSL
#include <boost/asio/ssl.hpp>
#endif
#include "../io_thread.hpp"
#include "../session_base.hpp"
#include "../v2_decoder.hpp"
#include "../v3_1_encoder.hpp"
#include "../null_mechanism.hpp"
#include "../config.hpp"
#include "../err.hpp"
#include "../ip.hpp"
#include "../likely.hpp"
#include "../wire.hpp"

#ifndef ZMQ_HAVE_WINDOWS
#include <unistd.h>
#endif

// Debug logging for ASIO ZMTP engine - set to 1 to enable
#define ASIO_ZMTP_ENGINE_DEBUG 0

#if ASIO_ZMTP_ENGINE_DEBUG
#include <cstdio>
#define ZMTP_ENGINE_DBG(fmt, ...)                                              \
    fprintf (stderr, "[ASIO_ZMTP_ENGINE] " fmt "\n", ##__VA_ARGS__)
#else
#define ZMTP_ENGINE_DBG(fmt, ...)
#endif

zmq::asio_zmtp_engine_t::asio_zmtp_engine_t (
  fd_t fd_,
  const options_t &options_,
  const endpoint_uri_pair_t &endpoint_uri_pair_) :
    asio_engine_t (fd_, options_, endpoint_uri_pair_),
    _greeting_size (v2_greeting_size),
    _greeting_bytes_read (0),
    _subscription_required (false),
    _heartbeat_timeout (0)
{
    init_zmtp_engine ();
}

zmq::asio_zmtp_engine_t::asio_zmtp_engine_t (
  fd_t fd_,
  const options_t &options_,
  const endpoint_uri_pair_t &endpoint_uri_pair_,
  std::unique_ptr<i_asio_transport> transport_) :
    asio_engine_t (fd_, options_, endpoint_uri_pair_, std::move (transport_)),
    _greeting_size (v2_greeting_size),
    _greeting_bytes_read (0),
    _subscription_required (false),
    _heartbeat_timeout (0)
{
    init_zmtp_engine ();
}

#if defined ZMQ_HAVE_ASIO_SSL
zmq::asio_zmtp_engine_t::asio_zmtp_engine_t (
  fd_t fd_,
  const options_t &options_,
  const endpoint_uri_pair_t &endpoint_uri_pair_,
  std::unique_ptr<i_asio_transport> transport_,
  std::unique_ptr<boost::asio::ssl::context> ssl_context_) :
    asio_engine_t (fd_, options_, endpoint_uri_pair_, std::move (transport_)),
    _greeting_size (v2_greeting_size),
    _greeting_bytes_read (0),
    _subscription_required (false),
    _heartbeat_timeout (0),
    _ssl_context (std::move (ssl_context_))
{
    init_zmtp_engine ();
}
#endif

zmq::asio_zmtp_engine_t::~asio_zmtp_engine_t ()
{
    ZMTP_ENGINE_DBG ("Destructor called");

    const int rc = _routing_id_msg.close ();
    errno_assert (rc == 0);
}

void zmq::asio_zmtp_engine_t::init_zmtp_engine ()
{
    ZMTP_ENGINE_DBG ("Constructor initializing fd=%d%s", _fd,
                     _transport ? " (custom transport)" : "");

    _next_msg = static_cast<int (asio_engine_t::*) (msg_t *)> (
      &asio_zmtp_engine_t::routing_id_msg);
    _process_msg = static_cast<int (asio_engine_t::*) (msg_t *)> (
      &asio_zmtp_engine_t::process_routing_id_msg);

    int rc = _pong_msg.init ();
    errno_assert (rc == 0);

    rc = _routing_id_msg.init ();
    errno_assert (rc == 0);

    if (_options.heartbeat_interval > 0) {
        _heartbeat_timeout = _options.heartbeat_timeout;
        if (_heartbeat_timeout == -1)
            _heartbeat_timeout = _options.heartbeat_interval;
    }

    memset (_greeting_recv, 0, sizeof (_greeting_recv));
    memset (_greeting_send, 0, sizeof (_greeting_send));
}

void zmq::asio_zmtp_engine_t::plug_internal ()
{
    ZMTP_ENGINE_DBG ("plug_internal called");

    set_handshake_timer ();

    //  Prepare the initial greeting to send
    _outpos = _greeting_send;
    _outpos[_outsize++] = UCHAR_MAX;
    put_uint64 (&_outpos[_outsize], _options.routing_id_size + 1);
    _outsize += 8;
    _outpos[_outsize++] = 0x7f;

    //  Start async I/O
    start_async_read ();
    start_async_write ();
}

const size_t revision_pos = 10;
const size_t minor_pos = 11;

bool zmq::asio_zmtp_engine_t::handshake ()
{
    ZMTP_ENGINE_DBG ("handshake: greeting_bytes_read=%u, greeting_size=%zu",
                     _greeting_bytes_read, _greeting_size);

    zmq_assert (_greeting_bytes_read < _greeting_size);
    const int rc = receive_greeting ();
    if (rc == -1)
        return false;
    if (rc != 0) {
        socket ()->event_handshake_failed_protocol (
          session ()->get_endpoint (), ZMQ_PROTOCOL_ERROR_ZMTP_UNSPECIFIED);
        error (protocol_error);
        return false;
    }

    if (_greeting_recv[revision_pos] != ASIO_ZMTP_3_x
        || _greeting_recv[minor_pos] != 1) {
        socket ()->event_handshake_failed_protocol (
          session ()->get_endpoint (), ZMQ_PROTOCOL_ERROR_ZMTP_UNSPECIFIED);
        error (protocol_error);
        return false;
    }

    if (!handshake_v3_1 ())
        return false;

    if (_outsize == 0)
        start_async_write ();

    return true;
}

int zmq::asio_zmtp_engine_t::receive_greeting ()
{
    bool unversioned = false;

    ZMTP_ENGINE_DBG ("receive_greeting: inpos=%p, insize=%zu",
                     static_cast<void *> (_inpos), _insize);

    //  Copy data from internal read buffer to greeting buffer
    while (_greeting_bytes_read < _greeting_size && _insize > 0) {
        _greeting_recv[_greeting_bytes_read] = *_inpos;
        ++_greeting_bytes_read;
        ++_inpos;
        --_insize;

        if (_greeting_recv[0] != 0xff) {
            unversioned = true;
            break;
        }

        if (_greeting_bytes_read < signature_size)
            continue;

        if (!(_greeting_recv[9] & 0x01)) {
            unversioned = true;
            break;
        }

        receive_greeting_versioned ();
    }

    if (_greeting_bytes_read < _greeting_size && !unversioned) {
        //  Need more data
        errno = EAGAIN;
        return -1;
    }

    return unversioned ? 1 : 0;
}

void zmq::asio_zmtp_engine_t::receive_greeting_versioned ()
{
    //  After receiving signature (10 bytes), need to send version byte (3)
    if (_outpos + _outsize == _greeting_send + signature_size) {
        _outpos[_outsize++] = 3;
    }

    //  After receiving more than signature, send rest of greeting
    if (_greeting_bytes_read > signature_size) {
        if (_outpos + _outsize == _greeting_send + signature_size + 1) {
            if (_greeting_recv[revision_pos] == ASIO_ZMTP_1_0
                || _greeting_recv[revision_pos] == ASIO_ZMTP_2_0)
                _outpos[_outsize++] = _options.type;
            else {
                _outpos[_outsize++] = 1;
                memset (_outpos + _outsize, 0, 20);

                memcpy (_outpos + _outsize, "NULL", 4);
                _outsize += 20;
                memset (_outpos + _outsize, 0, 32);
                _outsize += 32;
                _greeting_size = v3_greeting_size;
            }
        }
    }
}

bool zmq::asio_zmtp_engine_t::handshake_v3_x ()
{

    ZMTP_ENGINE_DBG ("handshake_v3_x");

    if (_options.mechanism != ZMQ_NULL
        || memcmp (_greeting_recv + 12,
                   "NULL\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0", 20)
             != 0) {
        socket ()->event_handshake_failed_protocol (
          session ()->get_endpoint (),
          ZMQ_PROTOCOL_ERROR_ZMTP_MECHANISM_MISMATCH);
        error (protocol_error);
        return false;
    }
    _mechanism = new (std::nothrow)
      null_mechanism_t (session (), _peer_address, _options);
    alloc_assert (_mechanism);
    _next_msg = &asio_zmtp_engine_t::next_handshake_command;
    _process_msg = &asio_zmtp_engine_t::process_handshake_command;

    return true;
}

bool zmq::asio_zmtp_engine_t::handshake_v3_1 ()
{
    ZMTP_ENGINE_DBG ("handshake_v3_1");

    _encoder = new (std::nothrow) v3_1_encoder_t (_options.out_batch_size);
    alloc_assert (_encoder);

    _decoder = new (std::nothrow)
      v2_decoder_t (_options.in_batch_size, _options.maxmsgsize, true);
    alloc_assert (_decoder);

    return zmq::asio_zmtp_engine_t::handshake_v3_x ();
}

int zmq::asio_zmtp_engine_t::routing_id_msg (msg_t *msg_)
{
    const int rc = msg_->init_size (_options.routing_id_size);
    errno_assert (rc == 0);
    if (_options.routing_id_size > 0)
        memcpy (msg_->data (), _options.routing_id, _options.routing_id_size);
    _next_msg = &asio_zmtp_engine_t::pull_msg_from_session;
    return 0;
}

int zmq::asio_zmtp_engine_t::process_routing_id_msg (msg_t *msg_)
{
    if (_options.recv_routing_id) {
        msg_->set_flags (msg_t::routing_id);
        const int rc = session ()->push_msg (msg_);
        errno_assert (rc == 0);
    } else {
        int rc = msg_->close ();
        errno_assert (rc == 0);
        rc = msg_->init ();
        errno_assert (rc == 0);
    }

    if (_subscription_required) {
        msg_t subscription;

        int rc = subscription.init_size (1);
        errno_assert (rc == 0);
        *static_cast<unsigned char *> (subscription.data ()) = 1;
        rc = session ()->push_msg (&subscription);
        errno_assert (rc == 0);
    }

    _process_msg = &asio_zmtp_engine_t::push_msg_to_session;

    return 0;
}

int zmq::asio_zmtp_engine_t::produce_ping_message (msg_t *msg_)
{
    const size_t ping_ttl_len = msg_t::ping_cmd_name_size + 2;
    zmq_assert (_mechanism != NULL);

    int rc = msg_->init_size (ping_ttl_len);
    errno_assert (rc == 0);
    msg_->set_flags (msg_t::command);
    memcpy (msg_->data (), "\4PING", msg_t::ping_cmd_name_size);

    uint16_t ttl_val = htons (_options.heartbeat_ttl);
    memcpy (static_cast<uint8_t *> (msg_->data ()) + msg_t::ping_cmd_name_size,
            &ttl_val, sizeof (ttl_val));

    rc = _mechanism->encode (msg_);
    _next_msg = &asio_zmtp_engine_t::pull_and_encode;
    if (!_has_timeout_timer && _heartbeat_timeout > 0) {
        add_timer (_heartbeat_timeout, heartbeat_timeout_timer_id);
        _has_timeout_timer = true;
    }
    return rc;
}

int zmq::asio_zmtp_engine_t::produce_pong_message (msg_t *msg_)
{
    zmq_assert (_mechanism != NULL);

    int rc = msg_->move (_pong_msg);
    errno_assert (rc == 0);

    rc = _mechanism->encode (msg_);
    _next_msg = &asio_zmtp_engine_t::pull_and_encode;
    return rc;
}

int zmq::asio_zmtp_engine_t::process_heartbeat_message (msg_t *msg_)
{
    if (msg_->is_ping ()) {
        const size_t ping_ttl_len = msg_t::ping_cmd_name_size + 2;
        const size_t ping_max_ctx_len = 16;
        uint16_t remote_heartbeat_ttl;

        memcpy (&remote_heartbeat_ttl,
                static_cast<uint8_t *> (msg_->data ())
                  + msg_t::ping_cmd_name_size,
                ping_ttl_len - msg_t::ping_cmd_name_size);
        remote_heartbeat_ttl = ntohs (remote_heartbeat_ttl);
        remote_heartbeat_ttl *= 100;

        if (!_has_ttl_timer && remote_heartbeat_ttl > 0) {
            add_timer (remote_heartbeat_ttl, heartbeat_ttl_timer_id);
            _has_ttl_timer = true;
        }

        const size_t context_len =
          std::min (msg_->size () - ping_ttl_len, ping_max_ctx_len);
        const int rc =
          _pong_msg.init_size (msg_t::ping_cmd_name_size + context_len);
        errno_assert (rc == 0);
        _pong_msg.set_flags (msg_t::command);
        memcpy (_pong_msg.data (), "\4PONG", msg_t::ping_cmd_name_size);
        if (context_len > 0)
            memcpy (static_cast<uint8_t *> (_pong_msg.data ())
                      + msg_t::ping_cmd_name_size,
                    static_cast<uint8_t *> (msg_->data ()) + ping_ttl_len,
                    context_len);

        _next_msg = static_cast<int (asio_engine_t::*) (msg_t *)> (
          &asio_zmtp_engine_t::produce_pong_message);
        restart_output ();
    }

    return 0;
}

int zmq::asio_zmtp_engine_t::process_command_message (msg_t *msg_)
{
    const uint8_t cmd_name_size =
      *(static_cast<const uint8_t *> (msg_->data ()));
    const size_t ping_name_size = msg_t::ping_cmd_name_size - 1;
    const size_t sub_name_size = msg_t::sub_cmd_name_size - 1;
    const size_t cancel_name_size = msg_t::cancel_cmd_name_size - 1;
    if (unlikely (msg_->size () < cmd_name_size + sizeof (cmd_name_size)))
        return -1;

    const uint8_t *const cmd_name =
      static_cast<const uint8_t *> (msg_->data ()) + 1;
    if (cmd_name_size == ping_name_size
        && memcmp (cmd_name, "PING", cmd_name_size) == 0)
        msg_->set_flags (zmq::msg_t::ping);
    if (cmd_name_size == ping_name_size
        && memcmp (cmd_name, "PONG", cmd_name_size) == 0)
        msg_->set_flags (zmq::msg_t::pong);
    if (cmd_name_size == sub_name_size
        && memcmp (cmd_name, "SUBSCRIBE", cmd_name_size) == 0)
        msg_->set_flags (zmq::msg_t::subscribe);
    if (cmd_name_size == cancel_name_size
        && memcmp (cmd_name, "CANCEL", cmd_name_size) == 0)
        msg_->set_flags (zmq::msg_t::cancel);

    if (msg_->is_ping () || msg_->is_pong ())
        return process_heartbeat_message (msg_);

    return 0;
}

#endif  // ZMQ_IOTHREAD_POLLER_USE_ASIO
