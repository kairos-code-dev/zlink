/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZMQ_ASIO_ZMTP_ENGINE_HPP_INCLUDED__
#define __ZMQ_ASIO_ZMTP_ENGINE_HPP_INCLUDED__

#include "../poller.hpp"
#if defined ZMQ_IOTHREAD_POLLER_USE_ASIO

#include "asio_engine.hpp"

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

//  Protocol revisions
enum
{
    ASIO_ZMTP_1_0 = 0,
    ASIO_ZMTP_2_0 = 1,
    ASIO_ZMTP_3_x = 3
};

class io_thread_t;
class session_base_t;
class mechanism_t;

//  ZMTP Protocol Engine using True Proactor Mode
//
//  This engine implements the ZMTP 3.x handshake and message framing
//  using Boost.Asio's async_read/async_write operations.

class asio_zmtp_engine_t ZMQ_FINAL : public asio_engine_t
{
  public:
    asio_zmtp_engine_t (fd_t fd_,
                        const options_t &options_,
                        const endpoint_uri_pair_t &endpoint_uri_pair_);
    asio_zmtp_engine_t (fd_t fd_,
                        const options_t &options_,
                        const endpoint_uri_pair_t &endpoint_uri_pair_,
                        std::unique_ptr<i_asio_transport> transport_);
#if defined ZMQ_HAVE_ASIO_SSL
    asio_zmtp_engine_t (fd_t fd_,
                        const options_t &options_,
                        const endpoint_uri_pair_t &endpoint_uri_pair_,
                        std::unique_ptr<i_asio_transport> transport_,
                        std::unique_ptr<boost::asio::ssl::context> ssl_context_);
#endif
    ~asio_zmtp_engine_t ();

  protected:
    //  Detects the protocol used by the peer.
    bool handshake () ZMQ_OVERRIDE;

    void plug_internal () ZMQ_OVERRIDE;

    int process_command_message (msg_t *msg_) ZMQ_OVERRIDE;
    int produce_ping_message (msg_t *msg_) ZMQ_OVERRIDE;
    int process_heartbeat_message (msg_t *msg_) ZMQ_OVERRIDE;
    int produce_pong_message (msg_t *msg_) ZMQ_OVERRIDE;
    bool build_gather_header (const msg_t &msg_,
                              unsigned char *buffer_,
                              size_t buffer_size_,
                              size_t &header_size_) ZMQ_OVERRIDE;

  private:
    //  Receive the greeting from the peer.
    int receive_greeting ();
    void receive_greeting_versioned ();

    void init_zmtp_engine ();

    bool handshake_v3_x ();
    bool handshake_v3_1 ();

    int routing_id_msg (msg_t *msg_);
    int process_routing_id_msg (msg_t *msg_);

    msg_t _routing_id_msg;

    //  Need to store PING payload for PONG
    msg_t _pong_msg;

    static const size_t signature_size = 10;

    //  Size of ZMTP/1.0 and ZMTP/2.0 greeting message
    static const size_t v2_greeting_size = 12;

    //  Size of ZMTP/3.0 greeting message
    static const size_t v3_greeting_size = 64;

    //  Expected greeting size.
    size_t _greeting_size;

    //  Greeting received from, and sent to peer
    unsigned char _greeting_recv[v3_greeting_size];
    unsigned char _greeting_send[v3_greeting_size];

    //  Size of greeting received so far
    unsigned int _greeting_bytes_read;

    //  Indicates whether the engine is to inject a phantom
    //  subscription message into the incoming stream.
    //  Needed to support old peers.
    bool _subscription_required;

    int _heartbeat_timeout;

#if defined ZMQ_HAVE_ASIO_SSL
    std::unique_ptr<boost::asio::ssl::context> _ssl_context;
#endif

    ZMQ_NON_COPYABLE_NOR_MOVABLE (asio_zmtp_engine_t)
};
}  // namespace zmq

#endif  // ZMQ_IOTHREAD_POLLER_USE_ASIO

#endif  // __ZMQ_ASIO_ZMTP_ENGINE_HPP_INCLUDED__
