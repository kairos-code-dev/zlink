/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZMQ_ASIO_ZMP_ENGINE_HPP_INCLUDED__
#define __ZMQ_ASIO_ZMP_ENGINE_HPP_INCLUDED__

#include "engine/asio/asio_engine.hpp"
#include <string>
#include <vector>
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
    void error (error_reason_t reason_) ZMQ_OVERRIDE;
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
    bool process_handshake_input ();
    int process_ready_message (msg_t *msg_);
    int process_error_message (msg_t *msg_);
    int produce_pong_message (msg_t *msg_);
    int push_one_then_decode (msg_t *msg_);
    void set_last_error (uint8_t code_, const char *reason_);
    void send_error_frame (uint8_t code_, const char *reason_);

    bool _hello_sent;
    bool _hello_received;
    bool _ready_sent;
    bool _ready_received;
    size_t _hello_header_bytes;
    size_t _hello_body_bytes;
    uint32_t _hello_body_len;
    unsigned char _hello_recv[272];
    unsigned char _hello_send[272];
    size_t _hello_send_size;
    std::vector<unsigned char> _ready_send;
    unsigned char _peer_routing_id[256];
    size_t _peer_routing_id_size;

    bool _subscription_required;
    int _heartbeat_timeout;
    std::vector<unsigned char> _heartbeat_ctx;
    uint8_t _last_error_code;
    std::string _last_error_reason;

#if defined ZMQ_HAVE_ASIO_SSL
    std::unique_ptr<boost::asio::ssl::context> _ssl_context;
#endif

    ZMQ_NON_COPYABLE_NOR_MOVABLE (asio_zmp_engine_t)
};
}

#endif
