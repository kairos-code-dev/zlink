/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"
#include "sockets/stream.hpp"
#include "core/pipe.hpp"
#include "core/ctx.hpp"
#include "core/io_thread.hpp"
#include "engine/i_engine.hpp"
#include "protocol/wire.hpp"
#include "utils/err.hpp"
#include "utils/likely.hpp"

#include <cstdlib>

namespace
{
const unsigned char stream_event_connect = 0x01;
const unsigned char stream_event_disconnect = 0x00;

bool stream_single_frame_recv_enabled ()
{
    const char *env = getenv ("ZLINK_STREAM_SINGLE_FRAME_RECV");
    return env && *env && *env != '0';
}

bool stream_direct_io_enabled ()
{
    const char *env = getenv ("ZLINK_STREAM_DIRECT_IO");
    //  STREAM direct-io is enabled by default.
    //  Export ZLINK_STREAM_DIRECT_IO=0 to force-disable.
    if (!env || !*env)
        return true;
    return *env != '0';
}

}

zlink::stream_t::stream_t (class ctx_t *parent_, uint32_t tid_, int sid_) :
    routing_socket_base_t (parent_, tid_, sid_),
    _single_frame_recv (false),
    _prefetched (false),
    _routing_id_sent (false),
    _prefetched_routing_id_value (0),
    _current_out (NULL),
    _more_out (false),
    _next_integral_routing_id (1),
    _direct_io_thread (NULL),
    _direct_io_tid (0),
    _direct_io_active (false),
    _direct_queue_lock (ATOMIC_FLAG_INIT),
    _direct_io_rr_counter (0),
    _direct_recv_head (0)
{
    options.type = ZLINK_STREAM;
    options.backlog = 65536;
    const int stream_batch_size = 128 * 1024;
    if (options.in_batch_size < stream_batch_size)
        options.in_batch_size = stream_batch_size;
    if (options.out_batch_size < stream_batch_size)
        options.out_batch_size = stream_batch_size;

    //  Enable direct IO by default, with env override.
    options.stream_direct_io = stream_direct_io_enabled () ? 1 : 0;

    //  Set up direct IO when enabled.
    if (options.stream_direct_io)
        setup_direct_io ();

    _dirty_send_pipes.reserve (256);
    _prefetched_msg.init ();
}

zlink::stream_t::~stream_t ()
{
    teardown_direct_io ();
    flush_dirty_send_pipes ();
    _prefetched_msg.close ();
    //  Close any queued direct msgs.
    for (size_t i = _direct_recv_head; i < _direct_recv_queue.size (); ++i)
        _direct_recv_queue[i].msg.close ();
    _direct_recv_queue.clear ();
    _direct_recv_head = 0;
}

int zlink::stream_t::push_msg_direct (msg_t *msg_,
                                       uint32_t routing_id_,
                                       void *engine_hint_)
{
    //  Spinlock: background IO thread may call concurrently with app thread.
    lock_direct_queue ();

    //  Update engine pointer for send-direction bypass.
    //  NULL engine_hint clears the entry (called from engine unplug).
    const size_t idx = static_cast<size_t> (routing_id_);
    if (idx >= _direct_send_engines.size ())
        _direct_send_engines.resize (idx + 1, NULL);
    _direct_send_engines[idx] = engine_hint_;

    //  NULL msg means engine is just updating/clearing the pointer.
    if (!msg_) {
        unlock_direct_queue ();
        return 0;
    }

    //  Set routing_id on msg before moving — it travels with the msg.
    msg_->set_routing_id (routing_id_);

    //  Append to direct recv queue (msg_t move, no data copy for lmsg).
    _direct_recv_queue.resize (_direct_recv_queue.size () + 1);
    direct_msg_t &entry = _direct_recv_queue.back ();
    entry.routing_id = routing_id_;
    entry.msg.init ();
    msg_->move (entry.msg);

    unlock_direct_queue ();
    return 0;
}

void zlink::stream_t::flush_dirty_send_pipes ()
{
    for (size_t i = 0; i < _dirty_send_pipes.size (); ++i) {
        if (_dirty_send_pipes[i])
            _dirty_send_pipes[i]->flush ();
    }
    _dirty_send_pipes.clear ();
}

void zlink::stream_t::xattach_pipe (pipe_t *pipe_,
                                    bool subscribe_to_all_,
                                    bool locally_initiated_)
{
    LIBZLINK_UNUSED (subscribe_to_all_);

    zlink_assert (pipe_);

    identify_peer (pipe_, locally_initiated_);
    _fq.attach (pipe_);

    queue_event (pipe_->get_server_socket_routing_id (), stream_event_connect);
}

void zlink::stream_t::xpipe_terminated (pipe_t *pipe_)
{
    const uint32_t server_routing_id = pipe_->get_server_socket_routing_id ();

    erase_out_pipe (pipe_);
    _fq.pipe_terminated (pipe_);
    if (pipe_ == _current_out)
        _current_out = NULL;
    if (server_routing_id != 0
        && server_routing_id < static_cast<uint32_t> (_out_by_id.size ())) {
        _out_by_id[server_routing_id] = NULL;
    }

    //  Remove terminated pipe from deferred flush list.
    for (size_t i = 0; i < _dirty_send_pipes.size (); ++i) {
        if (_dirty_send_pipes[i] == pipe_)
            _dirty_send_pipes[i] = NULL;
    }

    //  Clear direct send engine pointer for this connection.
    if (server_routing_id != 0
        && server_routing_id < static_cast<uint32_t> (_direct_send_engines.size ()))
        _direct_send_engines[server_routing_id] = NULL;

    queue_event (server_routing_id, stream_event_disconnect);
}

void zlink::stream_t::xread_activated (pipe_t *pipe_)
{
    _fq.activated (pipe_);
}

int zlink::stream_t::xsend (msg_t *msg_)
{
    if (!_more_out) {
        zlink_assert (!_current_out);

        // Fast path: single-frame send with routing id attached in msg_t.
        // This bypasses the ROUTING_ID + payload multipart requirement while
        // keeping backward-compatible multipart behavior.
        if (!(msg_->flags () & msg_t::more) && msg_->get_routing_id () != 0) {
            const uint32_t routing_id = msg_->get_routing_id ();
            const size_t routing_index = static_cast<size_t> (routing_id);

            //  Direct IO send bypass: write directly to engine send buffer,
            //  skipping pipe write/flush/ypipe/CAS/mailbox entirely.
            //  Spinlock: engine pointer may be cleared by background thread.
            lock_direct_queue ();
            if (routing_index < _direct_send_engines.size ()
                && _direct_send_engines[routing_index] != NULL) {
                i_engine *engine = static_cast<i_engine *> (
                  _direct_send_engines[routing_index]);
                const int send_rc = engine->queue_direct_send (msg_);
                unlock_direct_queue ();
                if (send_rc >= 0) {
                    //  send_rc==0: sync write done, restart deferred read.
                    //  send_rc==1: async posted to worker thread, restart
                    //              handled in the posted lambda.
                    if (send_rc == 0)
                        engine->restart_deferred_read ();
                    const int init_rc = msg_->init ();
                    errno_assert (init_rc == 0);
                    return 0;
                }
                //  Fall through to pipe path on failure.
            } else {
                unlock_direct_queue ();
            }

            if (routing_index >= _out_by_id.size ()) {
                errno = EHOSTUNREACH;
                return -1;
            }

            pipe_t *out = _out_by_id[routing_index];
            if (!out) {
                errno = EHOSTUNREACH;
                return -1;
            }

            const bool ok = out->write (msg_);
            if (unlikely (!ok)) {
                const int close_rc = msg_->close ();
                errno_assert (close_rc == 0);
                errno = EAGAIN;
                return -1;
            }

            //  Deferred flush: accumulate dirty pipes and batch-flush
            //  when recv drains (EAGAIN) or threshold is reached.
            //  This reduces per-message mailbox signaling overhead.
            if (options.stream_single_frame_recv) {
                _dirty_send_pipes.push_back (out);
                if (_dirty_send_pipes.size () >= flush_batch_threshold)
                    flush_dirty_send_pipes ();
            } else {
                out->flush ();
            }

            const int init_rc = msg_->init ();
            errno_assert (init_rc == 0);
            return 0;
        }

        if (msg_->flags () & msg_t::more) {
            if (msg_->size () != 4) {
                errno = EINVAL;
                return -1;
            }

            const uint32_t routing_id =
              get_uint32 (static_cast<unsigned char *> (msg_->data ()));
            const size_t routing_index = static_cast<size_t> (routing_id);
            if (routing_index >= _out_by_id.size ()
                || !_out_by_id[routing_index]) {
                errno = EHOSTUNREACH;
                return -1;
            }

            _current_out = _out_by_id[routing_index];
            if (!_current_out->check_write ()) {
                _current_out = NULL;
                errno = EAGAIN;
                return -1;
            }
            _more_out = true;
        }

        int rc = msg_->close ();
        errno_assert (rc == 0);
        rc = msg_->init ();
        errno_assert (rc == 0);
        return 0;
    }

    msg_->reset_flags (msg_t::more);
    _more_out = false;

    if (_current_out) {
        if (msg_->size () == 1) {
            const unsigned char *data =
              static_cast<unsigned char *> (msg_->data ());
            if (data[0] == stream_event_disconnect) {
                _current_out->terminate (false);
                int rc = msg_->close ();
                errno_assert (rc == 0);
                rc = msg_->init ();
                errno_assert (rc == 0);
                _current_out = NULL;
                return 0;
            }
        }

        const uint32_t routing_id = _current_out->get_server_socket_routing_id ();
        if (routing_id != 0) {
            const size_t routing_index = static_cast<size_t> (routing_id);
            lock_direct_queue ();
            if (routing_index < _direct_send_engines.size ()
                && _direct_send_engines[routing_index] != NULL) {
                i_engine *engine = static_cast<i_engine *> (
                  _direct_send_engines[routing_index]);
                const int send_rc = engine->queue_direct_send (msg_);
                unlock_direct_queue ();
                if (send_rc >= 0) {
                    if (send_rc == 0)
                        engine->restart_deferred_read ();
                    const int rc = msg_->init ();
                    errno_assert (rc == 0);
                    _current_out = NULL;
                    return 0;
                }
                //  Fall back to pipe path on failure.
            } else {
                unlock_direct_queue ();
            }
        }

        if (unlikely (routing_id == 0 || msg_->set_routing_id (routing_id) != 0)) {
            _current_out = NULL;
            return -1;
        }

        const bool ok = _current_out->write (msg_);
        if (likely (ok))
            _current_out->flush ();
        _current_out = NULL;
    } else {
        const int rc = msg_->close ();
        errno_assert (rc == 0);
    }

    const int rc = msg_->init ();
    errno_assert (rc == 0);

    return 0;
}

int zlink::stream_t::xrecv (msg_t *msg_)
{
    //  Direct IO pipe bypass: drain direct queue first.
    //  Messages already have routing_id set by push_msg_direct.
    //  Spinlock: background IO thread may push concurrently.
    lock_direct_queue ();
    if (_direct_recv_head < _direct_recv_queue.size ()) {
        direct_msg_t &entry = _direct_recv_queue[_direct_recv_head];
        entry.msg.move (*msg_);
        entry.msg.close ();
        ++_direct_recv_head;
        //  Reset queue when fully drained to avoid unbounded growth.
        if (_direct_recv_head == _direct_recv_queue.size ()) {
            _direct_recv_queue.clear ();
            _direct_recv_head = 0;
        }
        unlock_direct_queue ();
        _routing_id_sent = false;
        return 0;
    }
    unlock_direct_queue ();

    //  In Direct IO mode, re-activate all FQ pipes so that data pushed
    //  by the io_context pump (in process_commands) becomes readable.
    //  This is a no-op when all pipes are already active.
    if (_direct_io_active)
        _fq.force_activate_all ();

    if (options.stream_single_frame_recv) {
        //  Hot path: return already-prefetched message.
        if (_prefetched) {
            const int rc = msg_->move (_prefetched_msg);
            errno_assert (rc == 0);
            _prefetched = false;
            _routing_id_sent = false;
            return 0;
        }

        //  Fast path: no pending events — read directly into user's msg,
        //  skipping the _prefetched_msg intermediary and recursive call.
        //  Uses drain mode to keep reading from the same pipe for
        //  better cache locality with many concurrent connections.
        if (likely (_pending_events.empty ())) {
            pipe_t *pipe = NULL;
            int rc = _fq.recvpipe_drain (msg_, &pipe);
            if (rc != 0) {
                flush_dirty_send_pipes ();
                return -1;
            }

            zlink_assert (pipe != NULL);

            uint32_t routing_id_value = msg_->get_routing_id ();
            if (routing_id_value == 0)
                routing_id_value = pipe->get_server_socket_routing_id ();

            if (routing_id_value != 0 && msg_->get_routing_id () == 0) {
                rc = msg_->set_routing_id (routing_id_value);
                if (unlikely (rc != 0))
                    return -1;
            }
            _routing_id_sent = false;
            return 0;
        }

        //  Slow path: process pending event, then return via prefetch.
        if (prefetch_event ()) {
            if (_prefetched_routing_id_value != 0) {
                const int rc = _prefetched_msg.set_routing_id (
                  _prefetched_routing_id_value);
                if (unlikely (rc != 0))
                    return -1;
            }
            return xrecv (msg_);
        }

        flush_dirty_send_pipes ();
        errno = EAGAIN;
        return -1;
    }

    if (_prefetched) {
        if (!_routing_id_sent) {
            int rc = msg_->close ();
            errno_assert (rc == 0);
            rc = msg_->init_size (4);
            errno_assert (rc == 0);
            put_uint32 (static_cast<unsigned char *> (msg_->data ()),
                        _prefetched_routing_id_value);
            metadata_t *metadata = _prefetched_msg.metadata ();
            if (metadata)
                msg_->set_metadata (metadata);
            msg_->set_flags (msg_t::more);
            _routing_id_sent = true;
        } else {
            const int rc = msg_->move (_prefetched_msg);
            errno_assert (rc == 0);
            _prefetched = false;
        }
        return 0;
    }

    if (prefetch_event ()) {
        return xrecv (msg_);
    }

    pipe_t *pipe = NULL;
    int rc = _fq.recvpipe (&_prefetched_msg, &pipe);
    if (rc != 0)
        return -1;

    zlink_assert (pipe != NULL);

    _prefetched_routing_id_value = _prefetched_msg.get_routing_id ();
    if (_prefetched_routing_id_value == 0)
        _prefetched_routing_id_value = pipe->get_server_socket_routing_id ();

    rc = msg_->close ();
    errno_assert (rc == 0);
    rc = msg_->init_size (4);
    errno_assert (rc == 0);

    metadata_t *metadata = _prefetched_msg.metadata ();
    if (metadata)
        msg_->set_metadata (metadata);

    put_uint32 (static_cast<unsigned char *> (msg_->data ()),
                _prefetched_routing_id_value);
    msg_->set_flags (msg_t::more);

    _prefetched = true;
    _routing_id_sent = true;

    return 0;
}

bool zlink::stream_t::xhas_in ()
{
    //  Direct IO pipe bypass: check direct queue first.
    if (_direct_recv_head < _direct_recv_queue.size ())
        return true;

    //  In Direct IO mode, re-activate all FQ pipes so that data pushed
    //  by the io_context pump (in process_commands) becomes readable.
    if (_direct_io_active)
        _fq.force_activate_all ();

    if (options.stream_single_frame_recv) {
        if (_prefetched)
            return true;

        //  Fast path: no events, prefetch using drain mode.
        if (likely (_pending_events.empty ())) {
            pipe_t *pipe = NULL;
            int rc = _fq.recvpipe_drain (&_prefetched_msg, &pipe);
            if (rc != 0) {
                flush_dirty_send_pipes ();
                return false;
            }

            zlink_assert (pipe != NULL);

            uint32_t routing_id_value = _prefetched_msg.get_routing_id ();
            if (routing_id_value == 0)
                routing_id_value = pipe->get_server_socket_routing_id ();

            if (routing_id_value != 0
                && _prefetched_msg.get_routing_id () == 0) {
                rc = _prefetched_msg.set_routing_id (routing_id_value);
                if (unlikely (rc != 0))
                    return false;
            }

            _prefetched = true;
            _routing_id_sent = false;
            return true;
        }

        //  Slow path: pending event.
        if (prefetch_event ()) {
            if (_prefetched_routing_id_value != 0) {
                const int rc = _prefetched_msg.set_routing_id (
                  _prefetched_routing_id_value);
                if (unlikely (rc != 0))
                    return false;
            }
            return true;
        }

        flush_dirty_send_pipes ();
        return false;
    }

    if (_prefetched)
        return true;

    if (prefetch_event ())
        return true;

    pipe_t *pipe = NULL;
    int rc = _fq.recvpipe (&_prefetched_msg, &pipe);
    if (rc != 0)
        return false;

    zlink_assert (pipe != NULL);

    _prefetched_routing_id_value = _prefetched_msg.get_routing_id ();
    if (_prefetched_routing_id_value == 0)
        _prefetched_routing_id_value = pipe->get_server_socket_routing_id ();

    _prefetched = true;
    _routing_id_sent = false;

    return true;
}

bool zlink::stream_t::xhas_out ()
{
    return true;
}

int zlink::stream_t::xsetsockopt (int option_,
                                  const void *optval_,
                                  size_t optvallen_)
{
    if (option_ == ZLINK_CONNECT_ROUTING_ID) {
        LIBZLINK_UNUSED (optval_);
        LIBZLINK_UNUSED (optvallen_);
        errno = EOPNOTSUPP;
        return -1;
    }

    const int rc =
      routing_socket_base_t::xsetsockopt (option_, optval_, optvallen_);

    if (rc == 0 && option_ == ZLINK_STREAM_SINGLE_FRAME_RECV)
        _single_frame_recv = (options.stream_single_frame_recv != 0);

    if (rc == 0 && option_ == ZLINK_STREAM_DIRECT_IO
        && options.stream_direct_io != 0)
        setup_direct_io ();

    return rc;
}

void zlink::stream_t::identify_peer (pipe_t *pipe_, bool locally_initiated_)
{
    LIBZLINK_UNUSED (locally_initiated_);
    blob_t routing_id;

    if (routing_id.size () == 0) {
        unsigned char buf[4];
        put_uint32 (buf, _next_integral_routing_id++);
        if (_next_integral_routing_id == 0)
            _next_integral_routing_id = 1;
        routing_id.set (buf, sizeof buf);
    }

    pipe_->set_router_socket_routing_id (routing_id);
    pipe_->set_server_socket_routing_id (get_uint32 (routing_id.data ()));
    const uint32_t routing_id_value = pipe_->get_server_socket_routing_id ();
    const size_t idx = static_cast<size_t> (routing_id_value);
    if (idx >= _out_by_id.size ())
        _out_by_id.resize (idx + 1, NULL);
    _out_by_id[idx] = pipe_;
    add_out_pipe (ZLINK_MOVE (routing_id), pipe_);
}

void zlink::stream_t::queue_event (uint32_t routing_id_value_,
                                   unsigned char code_)
{
    stream_event_t ev;
    ev.routing_id_value = routing_id_value_;
    ev.code = code_;
    _pending_events.push_back (ZLINK_MOVE (ev));
}

bool zlink::stream_t::prefetch_event ()
{
    if (_pending_events.empty ())
        return false;

    stream_event_t ev = ZLINK_MOVE (_pending_events.front ());
    _pending_events.pop_front ();

    _prefetched_routing_id_value = ev.routing_id_value;

    int rc = _prefetched_msg.init_size (1);
    errno_assert (rc == 0);
    *static_cast<unsigned char *> (_prefetched_msg.data ()) = ev.code;

    _prefetched = true;
    _routing_id_sent = false;

    return true;
}

void zlink::stream_t::lock_direct_queue ()
{
    while (_direct_queue_lock.test_and_set (std::memory_order_acquire))
        ;
}

void zlink::stream_t::unlock_direct_queue ()
{
    _direct_queue_lock.clear (std::memory_order_release);
}

void zlink::stream_t::setup_direct_io ()
{
    if (_direct_io_active)
        return;

    //  Allocate a mailbox slot from the context for the direct io_thread.
    //  This gives the io_thread a unique tid so that commands routed to
    //  objects on this io_thread arrive at its mailbox.
    _direct_io_thread = new (std::nothrow) io_thread_t (get_ctx (), 0);
    alloc_assert (_direct_io_thread);

    _direct_io_tid =
      get_ctx ()->allocate_tid_slot (_direct_io_thread->get_mailbox ());
    if (_direct_io_tid == 0) {
        delete _direct_io_thread;
        _direct_io_thread = NULL;
        return;
    }

    //  Patch the io_thread's tid to match the allocated slot.
    _direct_io_thread->set_tid (_direct_io_tid);

    //  Do NOT call _direct_io_thread->start() — no background thread.
    //  The application thread will drive the io_context via process_commands.

    //  Store the pointer in options so that listener/create_engine can use it.
    options.direct_io_thread_ptr = _direct_io_thread;

    //  Create worker io_threads with their own io_contexts (opt-out
    //  via env).  Each runs on a background worker thread and handles
    //  both read AND write for its assigned connections — true per-thread
    //  independence, similar to cppserver's architecture.
    //  Count: ZLINK_STREAM_IO_WORKERS env var (default 2).
    {
        const char *no_worker = std::getenv ("ZLINK_STREAM_NO_WORKER");
        if (no_worker && *no_worker && *no_worker != '0') {
            _direct_io_active = true;
            return;
        }
    }

    int worker_count = 2;
    {
        const char *env = std::getenv ("ZLINK_STREAM_IO_WORKERS");
        if (env && *env) {
            const int val = std::atoi (env);
            if (val > 0 && val <= 16)
                worker_count = val;
        }
    }

    for (int i = 0; i < worker_count; ++i) {
        io_thread_t *ht =
          new (std::nothrow) io_thread_t (get_ctx (), 0);
        if (!ht)
            break;
        const uint32_t tid =
          get_ctx ()->allocate_tid_slot (ht->get_mailbox ());
        if (tid == 0) {
            delete ht;
            break;
        }
        ht->set_tid (tid);
        ht->start ();
        io_worker_t entry;
        entry.thread = ht;
        entry.tid = tid;
        _io_workers.push_back (entry);
        options.io_workers.push_back (
          static_cast<void *> (ht));
    }

    _direct_io_active = true;
}

void zlink::stream_t::teardown_direct_io ()
{
    if (!_direct_io_active)
        return;

    _direct_io_active = false;

    //  Stop the worker io_threads first (they run background workers).
    for (size_t i = _io_workers.size (); i > 0; --i) {
        io_worker_t &h = _io_workers[i - 1];
        if (h.thread) {
            h.thread->stop ();
            if (h.tid != 0)
                get_ctx ()->release_tid_slot (h.tid);
            delete h.thread;
        }
    }
    _io_workers.clear ();
    options.io_workers.clear ();

    if (_direct_io_thread) {
        //  Drain any remaining ASIO handlers.
        _direct_io_thread->get_io_context ().poll ();

        if (_direct_io_tid != 0) {
            get_ctx ()->release_tid_slot (_direct_io_tid);
            _direct_io_tid = 0;
        }
        delete _direct_io_thread;
        _direct_io_thread = NULL;
    }
}
