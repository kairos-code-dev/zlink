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
                         void *engine_hint_,
                         uint32_t producer_index_) ZLINK_OVERRIDE;

  private:
    struct stream_event_t
    {
        uint32_t routing_id_value;
        unsigned char code;
    };

    void identify_peer (pipe_t *pipe_, bool locally_initiated_);
    void queue_event (uint32_t routing_id_value_, unsigned char code_);
    bool prefetch_event ();

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

    //  Worker IO threads: background workers with their own io_contexts.
    //  Connections are distributed round-robin across all workers.
    //  Count configurable via ZLINK_STREAM_IO_WORKERS (default: 2).
    struct io_worker_t
    {
        zlink::io_thread_t *thread;
        uint32_t tid;
    };
    std::vector<io_worker_t> _io_workers;

    //  Spinlock protecting fallback direct recv queue and send-engine map.
    //  SPSC recv path is lock-free; this lock is used for compatibility
    //  fallback and send map lifetime safety.
    std::atomic_flag _direct_queue_lock;
    void lock_direct_queue ();
    void unlock_direct_queue ();

    //  Round-robin counter for distributing connections between
    //  the primary and worker io_threads.
    uint32_t _direct_io_rr_counter;

    //  Direct IO pipe bypass message wrapper.
    struct direct_msg_t
    {
        uint32_t routing_id;
        msg_t msg;
    };

    //  Per-worker SPSC recv rings (producer=worker, consumer=app thread).
    //  Enabled by default, disabled with ZLINK_STREAM_SPSC_RECV=0.
    struct worker_recv_ring_t
    {
        worker_recv_ring_t ();
        std::vector<direct_msg_t> slots;
        size_t mask;
        std::atomic<size_t> head;
        std::atomic<size_t> tail;
    };
    static const size_t worker_recv_ring_capacity = 8192;
    static const size_t recv_batch_rr_limit = 32;
    std::vector<worker_recv_ring_t *> _worker_recv_rings;
    bool init_worker_recv_rings (size_t worker_count_);
    void clear_worker_recv_rings ();
    bool push_to_worker_ring (size_t producer_index_,
                              zlink::msg_t *msg_,
                              uint32_t routing_id_);
    bool pop_from_worker_ring (size_t worker_index_, zlink::msg_t *msg_);
    bool pop_from_batch_cache (zlink::msg_t *msg_);
    void clear_batch_cache ();
    std::atomic<size_t> _recv_live;
    size_t _recv_rr_idx;
    std::vector<direct_msg_t> _recv_batch_cache;
    size_t _recv_batch_head;
    size_t _recv_batch_size;
    bool _spsc_recv_enabled;

    //  Legacy fallback queue used when SPSC is disabled or unavailable.
    std::vector<direct_msg_t> _direct_recv_queue;
    size_t _direct_recv_head;

    //  Engine pointers indexed by routing_id for send-direction bypass.
    //  Populated by push_msg_direct, cleared by xpipe_terminated.
    std::vector<void *> _direct_send_engines;

    ZLINK_NON_COPYABLE_NOR_MOVABLE (stream_t)
};
}

#endif
