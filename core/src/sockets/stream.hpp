/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_STREAM_HPP_INCLUDED__
#define __ZLINK_STREAM_HPP_INCLUDED__

#include <atomic>
#include <deque>
#include <vector>

#include "sockets/socket_base.hpp"
#include "sockets/fq.hpp"
#include "utils/stdint.hpp"

namespace zlink
{
class ctx_t;
class pipe_t;
class io_thread_t;

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

    //  Direct IO pipe bypass: engine pushes msgs here instead of pipe.
    int push_msg_direct (zlink::msg_t *msg_,
                         uint32_t routing_id_,
                         void *engine_hint_) ZLINK_OVERRIDE;

  private:
    struct stream_event_t
    {
        uint32_t routing_id_value;
        unsigned char code;
    };

    void identify_peer (pipe_t *pipe_, bool locally_initiated_);
    void queue_event (uint32_t routing_id_value_, unsigned char code_);
    bool prefetch_event ();
    int try_direct_send (zlink::msg_t *msg_, uint32_t routing_id_);
    bool pop_direct_msg (zlink::msg_t *msg_);
    bool has_direct_msg ();
    size_t direct_recv_shard (uint32_t routing_id_) const;

    fq_t _fq;

    //  Cached single-frame recv mode (from socket option or env var).
    bool _single_frame_recv;

    bool _prefetched;
    bool _routing_id_sent;
    uint32_t _prefetched_routing_id_value;
    msg_t _prefetched_msg;

    zlink::pipe_t *_current_out;
    bool _more_out;

    uint32_t _next_integral_routing_id;
    typedef std::vector<zlink::pipe_t *> out_pipe_vec_t;

    out_pipe_vec_t _out_by_id;

    std::deque<stream_event_t> _pending_events;

    //  Deferred send flush: accumulate pipes that need flushing and
    //  batch-flush them when recv drains or threshold is reached.
    //  This reduces per-message mailbox signaling overhead.
    static const size_t flush_batch_threshold = 1;
    std::vector<zlink::pipe_t *> _dirty_send_pipes;
    void flush_dirty_send_pipes ();

    //  Direct IO: socket-owned io_thread driven by app thread.
    //  When active, all ASIO operations (accept, read, write) run in
    //  the application thread, eliminating cross-thread signaling.
    zlink::io_thread_t *_direct_io_thread;
    uint32_t _direct_io_tid;
    bool _direct_io_active;
    void setup_direct_io ();
    void teardown_direct_io ();

    //  Helper IO threads: background workers with their own io_contexts.
    //  Connections are distributed round-robin across all helpers.
    //  Count configurable via ZLINK_STREAM_HELPER_THREADS (default: 2).
    struct helper_thread_t
    {
        zlink::io_thread_t *thread;
        uint32_t tid;
    };
    std::vector<helper_thread_t> _direct_io_helpers;

    //  Spinlock protecting _direct_recv_queue and _direct_send_engines
    //  against concurrent access from the background IO thread.
    std::atomic_flag _direct_queue_lock;
    void lock_direct_queue ();
    void unlock_direct_queue ();

    //  Round-robin counter for distributing connections between
    //  the primary and helper io_threads.
    uint32_t _direct_io_rr_counter;

    //  Direct IO pipe bypass: msgs delivered here by engine, read by xrecv.
    //  Eliminates pipe write/read/flush/CAS/mailbox overhead entirely.
    struct direct_msg_t
    {
        uint32_t routing_id;
        msg_t msg;
    };
    std::vector<direct_msg_t> _direct_recv_queue;
    size_t _direct_recv_head;

    //  Engine pointers indexed by routing_id for send-direction bypass.
    //  Populated by push_msg_direct, cleared by xpipe_terminated.
    std::vector<void *> _direct_send_engines;

    ZLINK_NON_COPYABLE_NOR_MOVABLE (stream_t)
};
}

#endif
