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
    size_t _recv_size;
    size_t _recv_limit;

    std::vector<unsigned char> _send_buffer_main;
    std::vector<unsigned char> _send_buffer_flush;
    size_t _send_buffer_flush_offset;
    size_t _send_buffer_limit;

    msg_t _tx_msg;
    bool _tx_msg_valid;
    unsigned char _tx_header[4];
    size_t _tx_total_size;

    session_base_t *_session;
    socket_base_t *_socket;

#if defined ZLINK_HAVE_ASIO_SSL
    std::unique_ptr<boost::asio::ssl::context> _ssl_context;
#endif

    ZLINK_NON_COPYABLE_NOR_MOVABLE (asio_stream_engine_t)
};
}

#endif  // ZLINK_IOTHREAD_POLLER_USE_ASIO

#endif
