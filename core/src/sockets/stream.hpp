/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_STREAM_HPP_INCLUDED__
#define __ZLINK_STREAM_HPP_INCLUDED__

#include <deque>
#include <vector>

#include "sockets/socket_base.hpp"
#include "sockets/fq.hpp"
#include "utils/stdint.hpp"

namespace zlink
{
class ctx_t;
class pipe_t;

class stream_t ZLINK_FINAL : public routing_socket_base_t
{
  public:
    stream_t (zlink::ctx_t *parent_, uint32_t tid_, int sid_);
    ~stream_t () ZLINK_OVERRIDE;

    void xattach_pipe (zlink::pipe_t *pipe_,
                       bool subscribe_to_all_,
                       bool locally_initiated_) ZLINK_FINAL;
    int xsend (zlink::msg_t *msg_) ZLINK_OVERRIDE;
    int xrecv (zlink::msg_t *msg_) ZLINK_OVERRIDE;
    bool xhas_in () ZLINK_OVERRIDE;
    bool xhas_out () ZLINK_OVERRIDE;
    void xread_activated (zlink::pipe_t *pipe_) ZLINK_FINAL;
    void xpipe_terminated (zlink::pipe_t *pipe_) ZLINK_FINAL;
    int xsetsockopt (int option_, const void *optval_, size_t optvallen_)
      ZLINK_FINAL;

  private:
    typedef std::vector<uint32_t> pending_notify_vec_t;
    typedef std::deque<uint32_t> pending_notify_deque_t;

    struct packet_rx_state_t
    {
        packet_rx_state_t () :
            header_filled (0),
            payload_size (0),
            payload_written (0),
            payload_msg (NULL)
        {
        }

        unsigned char header[4];
        size_t header_filled;
        uint32_t payload_size;
        size_t payload_written;
        msg_t *payload_msg;
    };

    struct ready_packet_t
    {
        ready_packet_t () : routing_id_value (0), payload_msg (NULL)
        {
        }

        ready_packet_t (uint32_t routing_id_value_, msg_t *payload_msg_) :
            routing_id_value (routing_id_value_),
            payload_msg (payload_msg_)
        {
        }

        uint32_t routing_id_value;
        msg_t *payload_msg;
    };

    typedef std::vector<packet_rx_state_t *> packet_rx_vec_t;
    typedef std::deque<ready_packet_t> packet_ready_deque_t;

    void identify_peer (pipe_t *pipe_, bool locally_initiated_);
    void queue_notify_event (uint32_t routing_id_value_);
    bool prefetch_notify_event ();
    bool stream_packet_mode_enabled () const;
    msg_t *acquire_packet_msg ();
    void release_packet_msg (msg_t *msg_);
    void recycle_packet_msg (msg_t *msg_);
    void clear_packet_msg_pool ();
    int queue_or_prefetch_packet (uint32_t routing_id_value_, msg_t *payload_msg_);
    packet_rx_state_t *get_or_create_packet_state (uint32_t routing_id_value_);
    void destroy_packet_state (uint32_t routing_id_value_);
    void clear_packetized_state ();
    int append_packetized_input (pipe_t *pipe_,
                                 uint32_t routing_id_value_,
                                 msg_t *chunk_);
    int prefetch_packetized_message ();
    void init_routing_id_frame (msg_t *msg_,
                                uint32_t routing_id_value_,
                                metadata_t *metadata_);
    void emit_connect_event (pipe_t *pipe_);
    void emit_disconnect_event (pipe_t *pipe_);

    fq_t _fq;

    bool _prefetched;
    bool _routing_id_sent;
    uint32_t _prefetched_routing_id_value;
    msg_t _prefetched_msg;
    msg_t _packet_chunk_msg;

    zlink::pipe_t *_current_out;
    bool _current_out_checked_writable;
    bool _more_out;

    uint32_t _next_integral_routing_id;
    typedef std::vector<zlink::pipe_t *> out_pipe_vec_t;

    out_pipe_vec_t _out_by_id;

    pending_notify_vec_t _pending_notify_events_vec;
    pending_notify_deque_t _pending_notify_events_deque;
    packet_rx_vec_t _packet_rx_states;
    packet_ready_deque_t _packet_ready;
    std::vector<msg_t *> _packet_msg_pool;

    ZLINK_NON_COPYABLE_NOR_MOVABLE (stream_t)
};
}

#endif
