/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"
#if defined ZMQ_IOTHREAD_POLLER_USE_ASIO

#include "engine/asio/asio_raw_engine.hpp"
#include "protocol/raw_encoder.hpp"
#include "protocol/raw_decoder.hpp"
#include "utils/err.hpp"
#include "protocol/wire.hpp"
#include "sockets/socket_base.hpp"

zmq::asio_raw_engine_t::asio_raw_engine_t (
  fd_t fd_,
  const options_t &options_,
  const endpoint_uri_pair_t &endpoint_uri_pair_) :
    asio_engine_t (fd_, options_, endpoint_uri_pair_)
{
    init_raw_engine ();
}

zmq::asio_raw_engine_t::asio_raw_engine_t (
  fd_t fd_,
  const options_t &options_,
  const endpoint_uri_pair_t &endpoint_uri_pair_,
  std::unique_ptr<i_asio_transport> transport_) :
    asio_engine_t (fd_, options_, endpoint_uri_pair_, std::move (transport_))
{
    init_raw_engine ();
}

#if defined ZMQ_HAVE_ASIO_SSL
zmq::asio_raw_engine_t::asio_raw_engine_t (
  fd_t fd_,
  const options_t &options_,
  const endpoint_uri_pair_t &endpoint_uri_pair_,
  std::unique_ptr<i_asio_transport> transport_,
  std::unique_ptr<boost::asio::ssl::context> ssl_context_) :
    asio_engine_t (fd_, options_, endpoint_uri_pair_, std::move (transport_)),
    _ssl_context (std::move (ssl_context_))
{
    init_raw_engine ();
}
#endif

zmq::asio_raw_engine_t::~asio_raw_engine_t ()
{
}

void zmq::asio_raw_engine_t::init_raw_engine ()
{
    _next_msg = static_cast<int (asio_engine_t::*) (msg_t *)> (
      &asio_raw_engine_t::pull_msg_from_session);
    _process_msg = static_cast<int (asio_engine_t::*) (msg_t *)> (
      &asio_raw_engine_t::decode_and_push);
}

void zmq::asio_raw_engine_t::plug_internal ()
{
    if (_encoder == NULL) {
        _encoder = new (std::nothrow) raw_encoder_t (_options.out_batch_size);
        alloc_assert (_encoder);
    }

    if (_decoder == NULL) {
        _decoder = new (std::nothrow)
          raw_decoder_t (_options.in_batch_size, _options.maxmsgsize);
        alloc_assert (_decoder);
        _input_in_decoder_buffer = false;
    }

    properties_t properties;
    if (init_properties (properties)) {
        zmq_assert (_metadata == NULL);
        _metadata = new (std::nothrow) metadata_t (properties);
        alloc_assert (_metadata);
    }

    complete_handshake ();
    socket ()->event_handshake_succeeded (_endpoint_uri_pair, 0);

    start_async_read ();
    start_async_write ();
}

bool zmq::asio_raw_engine_t::build_gather_header (const msg_t &msg_,
                                                  unsigned char *buffer_,
                                                  size_t buffer_size_,
                                                  size_t &header_size_)
{
    if (buffer_size_ < 4)
        return false;

    put_uint32 (buffer_, static_cast<uint32_t> (msg_.size ()));
    header_size_ = 4;
    return true;
}

#endif  // ZMQ_IOTHREAD_POLLER_USE_ASIO
