/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZMQ_ASIO_ZMTP_ENGINE_HPP_INCLUDED__
#define __ZMQ_ASIO_ZMTP_ENGINE_HPP_INCLUDED__

#include "../poller.hpp"
#if defined ZMQ_IOTHREAD_POLLER_USE_ASIO

#include "asio_engine.hpp"

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

//  Phase 1-C: ZMTP Protocol Engine using True Proactor Mode
//
//  This engine implements the ZMTP 3.x handshake and message framing
//  using Boost.Asio's async_read/async_write operations.

class asio_zmtp_engine_t ZMQ_FINAL : public asio_engine_t
{
  public:
    asio_zmtp_engine_t (fd_t fd_,
                        const options_t &options_,
                        const endpoint_uri_pair_t &endpoint_uri_pair_);
    ~asio_zmtp_engine_t ();

  protected:
    //  Detects the protocol used by the peer.
    bool handshake () ZMQ_OVERRIDE;

    void plug_internal () ZMQ_OVERRIDE;

    int process_command_message (msg_t *msg_) ZMQ_OVERRIDE;
    int produce_ping_message (msg_t *msg_) ZMQ_OVERRIDE;
    int process_heartbeat_message (msg_t *msg_) ZMQ_OVERRIDE;
    int produce_pong_message (msg_t *msg_) ZMQ_OVERRIDE;

  private:
    //  Receive the greeting from the peer.
    int receive_greeting ();
    void receive_greeting_versioned ();

    typedef bool (asio_zmtp_engine_t::*handshake_fun_t) ();
    static handshake_fun_t select_handshake_fun (bool unversioned,
                                                 unsigned char revision,
                                                 unsigned char minor);

    bool handshake_v1_0_unversioned ();
    bool handshake_v1_0 ();
    bool handshake_v2_0 ();
    bool handshake_v3_x (bool downgrade_sub);
    bool handshake_v3_0 ();
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

    ZMQ_NON_COPYABLE_NOR_MOVABLE (asio_zmtp_engine_t)
};
}  // namespace zmq

#endif  // ZMQ_IOTHREAD_POLLER_USE_ASIO

#endif  // __ZMQ_ASIO_ZMTP_ENGINE_HPP_INCLUDED__
