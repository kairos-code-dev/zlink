/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZMQ_ASIO_ZMP_ENGINE_HPP_INCLUDED__
#define __ZMQ_ASIO_ZMP_ENGINE_HPP_INCLUDED__

#include "asio_engine.hpp"
#if defined ZMQ_HAVE_ASIO_SSL
#include <boost/asio/ssl.hpp>
#endif

namespace zmq
{
class asio_zmp_engine_t ZMQ_FINAL : public asio_engine_t
{
  public:
    asio_zmp_engine_t (fd_t fd_,
                       const options_t &options_,
                       const endpoint_uri_pair_t &endpoint_uri_pair_);
    asio_zmp_engine_t (fd_t fd_,
                       const options_t &options_,
                       const endpoint_uri_pair_t &endpoint_uri_pair_,
                       std::unique_ptr<i_asio_transport> transport_);
#if defined ZMQ_HAVE_ASIO_SSL
    asio_zmp_engine_t (fd_t fd_,
                       const options_t &options_,
                       const endpoint_uri_pair_t &endpoint_uri_pair_,
                       std::unique_ptr<i_asio_transport> transport_,
                       std::unique_ptr<boost::asio::ssl::context> ssl_context_);
#endif
    ~asio_zmp_engine_t () ZMQ_OVERRIDE;

  protected:
    bool handshake () ZMQ_OVERRIDE;
    void plug_internal () ZMQ_OVERRIDE;
    int decode_and_push (msg_t *msg_) ZMQ_OVERRIDE;
    int process_command_message (msg_t *msg_) ZMQ_OVERRIDE;
    int produce_ping_message (msg_t *msg_) ZMQ_OVERRIDE;
    int process_heartbeat_message (msg_t *msg_) ZMQ_OVERRIDE;
    bool build_gather_header (const msg_t &msg_,
                              unsigned char *buffer_,
                              size_t buffer_size_,
                              size_t &header_size_) ZMQ_OVERRIDE;

  private:
    void init_zmp_engine ();
    bool receive_hello ();
    bool parse_hello (const unsigned char *data_, size_t size_);
    bool is_socket_type_compatible (int peer_type_) const;
    int push_one_then_decode (msg_t *msg_);

    bool _hello_sent;
    bool _hello_received;
    size_t _hello_header_bytes;
    size_t _hello_body_bytes;
    uint32_t _hello_body_len;
    unsigned char _hello_recv[272];
    unsigned char _hello_send[272];
    size_t _hello_send_size;
    unsigned char _peer_routing_id[256];
    size_t _peer_routing_id_size;

    bool _subscription_required;
    int _heartbeat_timeout;

#if defined ZMQ_HAVE_ASIO_SSL
    std::unique_ptr<boost::asio::ssl::context> _ssl_context;
#endif

    ZMQ_NON_COPYABLE_NOR_MOVABLE (asio_zmp_engine_t)
};
}

#endif
