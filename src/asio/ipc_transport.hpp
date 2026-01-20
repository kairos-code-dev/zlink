/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZMQ_ASIO_IPC_TRANSPORT_HPP_INCLUDED__
#define __ZMQ_ASIO_IPC_TRANSPORT_HPP_INCLUDED__

#include "i_asio_transport.hpp"

#if defined ZMQ_IOTHREAD_POLLER_USE_ASIO && defined ZMQ_HAVE_IPC

#include <boost/asio.hpp>
#include <boost/asio/local/stream_protocol.hpp>

#include <memory>

namespace zmq
{
class ipc_transport_t ZMQ_FINAL : public i_asio_transport
{
  public:
    ipc_transport_t ();
    ~ipc_transport_t ();

    bool open (boost::asio::io_context &io_context, fd_t fd) ZMQ_OVERRIDE;
    bool is_open () const ZMQ_OVERRIDE;
    void close () ZMQ_OVERRIDE;

    void async_read_some (unsigned char *buffer,
                          std::size_t buffer_size,
                          completion_handler_t handler) ZMQ_OVERRIDE;

    std::size_t read_some (std::uint8_t *buffer,
                           std::size_t len) ZMQ_OVERRIDE;

    void async_write_some (const unsigned char *buffer,
                           std::size_t buffer_size,
                           completion_handler_t handler) ZMQ_OVERRIDE;

    void async_writev (const unsigned char *header,
                       std::size_t header_size,
                       const unsigned char *body,
                       std::size_t body_size,
                       completion_handler_t handler) ZMQ_OVERRIDE;

    std::size_t write_some (const std::uint8_t *data,
                            std::size_t len) ZMQ_OVERRIDE;

    bool supports_speculative_write () const ZMQ_OVERRIDE;
    bool supports_gather_write () const ZMQ_OVERRIDE { return true; }

    const char *name () const ZMQ_OVERRIDE { return "ipc_transport"; }

  private:
    std::unique_ptr<boost::asio::local::stream_protocol::socket> _socket;
};

}  // namespace zmq

#endif  // ZMQ_IOTHREAD_POLLER_USE_ASIO && ZMQ_HAVE_IPC

#endif  // __ZMQ_ASIO_IPC_TRANSPORT_HPP_INCLUDED__
