/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_STREAM_HPP_INCLUDED__
#define __ZLINK_STREAM_HPP_INCLUDED__

#include <deque>
#include <mutex>
#include <string>
#include <vector>
#include <atomic>

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
    int stream_dispatch_start (zlink_stream_on_packets_fn callback_,
                               int flags_) ZLINK_OVERRIDE;
    int stream_dispatch_stop () ZLINK_OVERRIDE;
    bool stream_dispatch_len32be_enabled () const ZLINK_OVERRIDE;
    bool stream_dispatch_active () const ZLINK_OVERRIDE;
    int stream_dispatch_send_from_io (const zlink_routing_id_t *rid_,
                                      const void *data_,
                                      size_t size_,
                                      int flags_,
                                      bool len32be_) ZLINK_OVERRIDE;
    int stream_dispatch_send_msg_from_io (const zlink_routing_id_t *rid_,
                                          zlink::msg_t *msg_,
                                          int flags_,
                                          bool len32be_) ZLINK_OVERRIDE;

  private:
    typedef std::vector<uint32_t> pending_notify_vec_t;
    typedef std::deque<uint32_t> pending_notify_deque_t;

    void identify_peer (pipe_t *pipe_, bool locally_initiated_);
    uint32_t ensure_dispatch_routing_id (pipe_t *pipe_);
    void queue_notify_event (uint32_t routing_id_value_);
    bool prefetch_notify_event ();
    int deliver_prefetched (msg_t *msg_);
    void init_routing_id_frame (msg_t *msg_,
                                uint32_t routing_id_value_,
                                metadata_t *metadata_);
    void emit_connect_event (pipe_t *pipe_);
    void emit_disconnect_event (pipe_t *pipe_);
    int xstream_dispatch_msg (zlink::msg_t *msg_, zlink::pipe_t *pipe_)
      ZLINK_OVERRIDE;
    int dispatch_len32be (zlink::msg_t *msg_, zlink::pipe_t *pipe_);
    uint32_t resolve_dispatch_routing_id_fast (const zlink::msg_t *msg_,
                                               zlink::pipe_t *pipe_);
    void stop_dispatch_from_callback ();

    fq_t _fq;

    bool _prefetched;
    bool _routing_id_sent;
    uint32_t _prefetched_routing_id_value;
    msg_t _prefetched_msg;

    zlink::pipe_t *_current_out;
    bool _more_out;

    std::atomic<uint32_t> _next_integral_routing_id;
    typedef std::vector<zlink::pipe_t *> out_pipe_vec_t;

    out_pipe_vec_t _out_by_id;

    pending_notify_vec_t _pending_notify_events_vec;
    pending_notify_deque_t _pending_notify_events_deque;

    std::atomic<bool> _dispatch_active;
    std::atomic<bool> _dispatch_len32be;
    std::atomic<zlink_stream_on_packets_fn> _dispatch_callback;
    std::atomic<uint32_t> _dispatch_reassembly_epoch;
    mutable std::mutex _dispatch_control_mu;

    ZLINK_NON_COPYABLE_NOR_MOVABLE (stream_t)
};
}

#endif
