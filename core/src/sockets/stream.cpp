/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"
#include "sockets/stream.hpp"
#include "core/pipe.hpp"
#include "protocol/wire.hpp"
#include "utils/err.hpp"
#include "utils/likely.hpp"
#include <climits>
#include <cstring>
#include <new>
#include <cstdlib>

namespace
{
bool env_flag_default_true (const char *name_)
{
    const char *env = std::getenv (name_);
    if (!env || !*env)
        return true;
    return *env != '0';
}

const bool stream_notify_queue_deque =
  env_flag_default_true ("ZLINK_ASIO_STREAM_NOTIFY_QUEUE_DEQUE");

int parse_positive_int_env (const char *name_, int default_value_)
{
    const char *env = std::getenv (name_);
    if (!env || !*env)
        return default_value_;

    char *end = NULL;
    const long value = std::strtol (env, &end, 10);
    if (!end || end == env || value <= 0 || value > INT_MAX)
        return default_value_;
    return static_cast<int> (value);
}

const int stream_batch_size_min =
  parse_positive_int_env ("ZLINK_ASIO_STREAM_BATCH_SIZE", 12288);

const int stream_packet_msg_pool_max = parse_positive_int_env (
  "ZLINK_STREAM_PACKET_MSG_POOL_MAX", 4096);

const int stream_packet_copy_threshold = parse_positive_int_env (
  "ZLINK_STREAM_PACKET_COPY_THRESHOLD", 128);
}

zlink::stream_t::stream_t (class ctx_t *parent_, uint32_t tid_, int sid_) :
    routing_socket_base_t (parent_, tid_, sid_),
    _prefetched (false),
    _routing_id_sent (false),
    _prefetched_routing_id_value (0),
    _current_out (NULL),
    _current_out_checked_writable (false),
    _more_out (false),
    _next_integral_routing_id (1)
{
    options.type = ZLINK_STREAM;
    options.backlog = 65536;
    if (options.sndbuf < 0)
        options.sndbuf = 262144;
    if (options.rcvbuf < 0)
        options.rcvbuf = 262144;
    const int stream_batch_size = stream_batch_size_min;
    if (options.in_batch_size < stream_batch_size)
        options.in_batch_size = stream_batch_size;
    if (options.out_batch_size < stream_batch_size)
        options.out_batch_size = stream_batch_size;

    _prefetched_msg.init ();
    _packet_chunk_msg.init ();
}

zlink::stream_t::~stream_t ()
{
    clear_packetized_state ();
    clear_packet_msg_pool ();
    _packet_chunk_msg.close ();
    _prefetched_msg.close ();
}

void zlink::stream_t::xattach_pipe (pipe_t *pipe_,
                                    bool subscribe_to_all_,
                                    bool locally_initiated_)
{
    LIBZLINK_UNUSED (subscribe_to_all_);

    zlink_assert (pipe_);

    identify_peer (pipe_, locally_initiated_);
    _fq.attach (pipe_);

    emit_connect_event (pipe_);

    if (options.stream_notify)
        queue_notify_event (pipe_->get_server_socket_routing_id ());
}

void zlink::stream_t::xpipe_terminated (pipe_t *pipe_)
{
    zlink_assert (pipe_);

    emit_disconnect_event (pipe_);

    const uint32_t server_routing_id = pipe_->get_server_socket_routing_id ();

    erase_out_pipe (pipe_);
    _fq.pipe_terminated (pipe_);
    if (pipe_ == _current_out) {
        _current_out = NULL;
        _current_out_checked_writable = false;
    }
    if (server_routing_id != 0
        && server_routing_id < static_cast<uint32_t> (_out_by_id.size ())) {
        _out_by_id[server_routing_id] = NULL;
    }
    destroy_packet_state (server_routing_id);

    if (options.stream_notify)
        queue_notify_event (server_routing_id);
}

void zlink::stream_t::xread_activated (pipe_t *pipe_)
{
    _fq.activated (pipe_);
}

int zlink::stream_t::xsend (msg_t *msg_)
{
    const bool packet_mode = stream_packet_mode_enabled ();

    if (!_more_out) {
        zlink_assert (!_current_out);
        _current_out_checked_writable = false;

        // Fast path: single-frame send with routing id attached in msg_t.
        if (!(msg_->flags () & msg_t::more) && msg_->get_routing_id () != 0) {
            const uint32_t routing_id = msg_->get_routing_id ();
            const size_t routing_index = static_cast<size_t> (routing_id);
            if (routing_index >= _out_by_id.size ()) {
                errno = EHOSTUNREACH;
                return -1;
            }

            pipe_t *out = _out_by_id[routing_index];
            if (!out) {
                errno = EHOSTUNREACH;
                return -1;
            }
            if (!out->check_write ()) {
                errno = EAGAIN;
                return -1;
            }

            if (msg_->size () == 0) {
                out->terminate (false);
            } else {
                if (packet_mode && msg_->size ()
                                     > static_cast<size_t> (
                                       options.stream_packet_max_size)) {
                    errno = EMSGSIZE;
                    return -1;
                }
                const bool ok = out->write_no_hwm_check (msg_);

                if (unlikely (!ok)) {
                    const int close_rc = msg_->close ();
                    errno_assert (close_rc == 0);
                    errno = EAGAIN;
                    return -1;
                }
                out->flush ();
            }

            const int init_rc = msg_->init ();
            errno_assert (init_rc == 0);
            return 0;
        }

        // First frame is the target routing id in 4-byte wire format.
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
            _current_out_checked_writable = true;
        }

        // Match libzmq STREAM semantics: consume the first frame and
        // always expect one subsequent payload frame.
        _more_out = true;

        int rc = msg_->close ();
        errno_assert (rc == 0);
        rc = msg_->init ();
        errno_assert (rc == 0);
        return 0;
    }

    msg_->reset_flags (msg_t::more);
    _more_out = false;

    // Second frame is payload; if no route exists, drop silently.
    if (_current_out) {
        if (msg_->size () == 0) {
            _current_out->terminate (false);
            int rc = msg_->close ();
            errno_assert (rc == 0);
            rc = msg_->init ();
            errno_assert (rc == 0);
            _current_out = NULL;
            _current_out_checked_writable = false;
            return 0;
        }

        bool ok = false;
        if (packet_mode && msg_->size ()
                             > static_cast<size_t> (
                               options.stream_packet_max_size)) {
            const int rc = msg_->close ();
            errno_assert (rc == 0);
            const int init_rc = msg_->init ();
            errno_assert (init_rc == 0);
            _current_out = NULL;
            _current_out_checked_writable = false;
            errno = EMSGSIZE;
            return -1;
        }
        ok = _current_out_checked_writable
               ? _current_out->write_no_hwm_check (msg_)
               : _current_out->write (msg_);
        if (likely (ok))
            _current_out->flush ();
        _current_out = NULL;
        _current_out_checked_writable = false;
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
    if (_prefetched) {
        if (!_routing_id_sent) {
            init_routing_id_frame (
              msg_, _prefetched_routing_id_value, _prefetched_msg.metadata ());
            _routing_id_sent = true;
        } else {
            const int rc = msg_->move (_prefetched_msg);
            errno_assert (rc == 0);
            _prefetched = false;
        }
        return 0;
    }

    if (options.stream_notify && prefetch_notify_event ())
        return xrecv (msg_);

    if (stream_packet_mode_enabled ()) {
        int prefetch_rc = prefetch_packetized_message ();
        if (prefetch_rc < 0)
            return -1;
        if (prefetch_rc > 0)
            return xrecv (msg_);

        pipe_t *pipe = NULL;
        int rc = _fq.recvpipe (&_packet_chunk_msg, &pipe);
        if (rc != 0)
            return -1;

        zlink_assert (pipe != NULL);

        uint32_t routing_id_value = _packet_chunk_msg.get_routing_id ();
        if (routing_id_value == 0)
            routing_id_value = pipe->get_server_socket_routing_id ();

        rc = append_packetized_input (pipe, routing_id_value, &_packet_chunk_msg);
        if (rc != 0)
            return -1;

        if (_prefetched)
            return xrecv (msg_);

        prefetch_rc = prefetch_packetized_message ();
        if (prefetch_rc < 0)
            return -1;
        if (prefetch_rc > 0)
            return xrecv (msg_);

        errno = EAGAIN;
        return -1;
    }

    pipe_t *pipe = NULL;
    int rc = _fq.recvpipe (&_prefetched_msg, &pipe);
    if (rc != 0)
        return -1;

    zlink_assert (pipe != NULL);

    //  Match libzmq STREAM fast-path: keep payload prefetched and return
    //  routing-id frame directly to caller without extra msg move indirection.
    uint32_t routing_id_value = _prefetched_msg.get_routing_id ();
    if (routing_id_value == 0) {
        routing_id_value = pipe->get_server_socket_routing_id ();
        if (routing_id_value != 0) {
            const int set_rc = _prefetched_msg.set_routing_id (routing_id_value);
            errno_assert (set_rc == 0);
        }
    }

    init_routing_id_frame (msg_, routing_id_value, _prefetched_msg.metadata ());

    _prefetched_routing_id_value = routing_id_value;
    _prefetched = true;
    _routing_id_sent = true;

    return 0;
}

bool zlink::stream_t::xhas_in ()
{
    if (_prefetched)
        return true;

    if (options.stream_notify && prefetch_notify_event ())
        return true;

    if (stream_packet_mode_enabled ()) {
        int prefetch_rc = prefetch_packetized_message ();
        if (prefetch_rc < 0)
            return false;
        if (prefetch_rc > 0)
            return true;

        pipe_t *pipe = NULL;
        int rc = _fq.recvpipe (&_packet_chunk_msg, &pipe);
        while (rc == 0) {
            zlink_assert (pipe != NULL);

            uint32_t routing_id_value = _packet_chunk_msg.get_routing_id ();
            if (routing_id_value == 0)
                routing_id_value = pipe->get_server_socket_routing_id ();

            rc = append_packetized_input (pipe, routing_id_value,
                                          &_packet_chunk_msg);
            if (rc != 0)
                return false;

            if (_prefetched)
                return true;

            prefetch_rc = prefetch_packetized_message ();
            if (prefetch_rc < 0)
                return false;
            if (prefetch_rc > 0)
                return true;

            pipe = NULL;
            rc = _fq.recvpipe (&_packet_chunk_msg, &pipe);
        }
        return false;
    }

    pipe_t *pipe = NULL;
    int rc = _fq.recvpipe (&_prefetched_msg, &pipe);
    if (rc != 0)
        return false;

    zlink_assert (pipe != NULL);

    uint32_t routing_id_value = _prefetched_msg.get_routing_id ();
    if (routing_id_value == 0) {
        routing_id_value = pipe->get_server_socket_routing_id ();
        if (routing_id_value != 0) {
            const int set_rc = _prefetched_msg.set_routing_id (routing_id_value);
            errno_assert (set_rc == 0);
        }
    }

    _prefetched_routing_id_value = routing_id_value;
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

    return routing_socket_base_t::xsetsockopt (option_, optval_, optvallen_);
}

void zlink::stream_t::identify_peer (pipe_t *pipe_, bool locally_initiated_)
{
    LIBZLINK_UNUSED (locally_initiated_);
    blob_t routing_id;

    unsigned char buf[4];
    put_uint32 (buf, _next_integral_routing_id++);
    if (_next_integral_routing_id == 0)
        _next_integral_routing_id = 1;
    routing_id.set (buf, sizeof buf);

    pipe_->set_router_socket_routing_id (routing_id);
    pipe_->set_server_socket_routing_id (get_uint32 (routing_id.data ()));

    const uint32_t routing_id_value = pipe_->get_server_socket_routing_id ();
    const size_t idx = static_cast<size_t> (routing_id_value);
    if (idx >= _out_by_id.size ())
        _out_by_id.resize (idx + 1, NULL);
    _out_by_id[idx] = pipe_;

    add_out_pipe (ZLINK_MOVE (routing_id), pipe_);
}

void zlink::stream_t::queue_notify_event (uint32_t routing_id_value_)
{
    if (stream_notify_queue_deque)
        _pending_notify_events_deque.push_back (routing_id_value_);
    else
        _pending_notify_events_vec.push_back (routing_id_value_);
}

bool zlink::stream_t::prefetch_notify_event ()
{
    uint32_t routing_id_value = 0;

    if (stream_notify_queue_deque) {
        if (_pending_notify_events_deque.empty ())
            return false;
        routing_id_value = _pending_notify_events_deque.front ();
        _pending_notify_events_deque.pop_front ();
    } else {
        if (_pending_notify_events_vec.empty ())
            return false;
        routing_id_value = _pending_notify_events_vec.front ();
        _pending_notify_events_vec.erase (_pending_notify_events_vec.begin ());
    }

    _prefetched_routing_id_value = routing_id_value;

    int rc = _prefetched_msg.close ();
    errno_assert (rc == 0);
    rc = _prefetched_msg.init_size (0);
    errno_assert (rc == 0);

    _prefetched = true;
    _routing_id_sent = false;

    return true;
}

bool zlink::stream_t::stream_packet_mode_enabled () const
{
    return options.stream_packet_mode == ZLINK_STREAM_PACKET_MODE_LEN32BE;
}

zlink::msg_t *zlink::stream_t::acquire_packet_msg ()
{
    if (!_packet_msg_pool.empty ()) {
        msg_t *msg = _packet_msg_pool.back ();
        _packet_msg_pool.pop_back ();
        return msg;
    }

    msg_t *msg = new (std::nothrow) msg_t;
    if (!msg) {
        errno = ENOMEM;
        return NULL;
    }

    const int rc = msg->init ();
    errno_assert (rc == 0);
    return msg;
}

void zlink::stream_t::release_packet_msg (msg_t *msg_)
{
    if (!msg_)
        return;

    const int rc = msg_->close ();
    errno_assert (rc == 0);
    const int init_rc = msg_->init ();
    errno_assert (init_rc == 0);

    if (_packet_msg_pool.size ()
        >= static_cast<size_t> (stream_packet_msg_pool_max)) {
        delete msg_;
    } else {
        _packet_msg_pool.push_back (msg_);
    }
}

void zlink::stream_t::recycle_packet_msg (msg_t *msg_)
{
    if (!msg_)
        return;

    if (_packet_msg_pool.size ()
        >= static_cast<size_t> (stream_packet_msg_pool_max)) {
        delete msg_;
    } else {
        _packet_msg_pool.push_back (msg_);
    }
}

void zlink::stream_t::clear_packet_msg_pool ()
{
    while (!_packet_msg_pool.empty ()) {
        msg_t *msg = _packet_msg_pool.back ();
        _packet_msg_pool.pop_back ();
        delete msg;
    }
}

int zlink::stream_t::queue_or_prefetch_packet (uint32_t routing_id_value_,
                                               msg_t *payload_msg_)
{
    if (!payload_msg_)
        return 0;

    // Fast-path: if no buffered packet is pending for delivery, move this packet
    // directly into the prefetched slot and avoid ready-queue push/pop.
    if (!_prefetched && _packet_ready.empty ()) {
        int rc = _prefetched_msg.move (*payload_msg_);
        if (rc != 0) {
            release_packet_msg (payload_msg_);
            return -1;
        }

        rc = _prefetched_msg.set_routing_id (routing_id_value_);
        errno_assert (rc == 0);

        recycle_packet_msg (payload_msg_);
        _prefetched_routing_id_value = routing_id_value_;
        _prefetched = true;
        _routing_id_sent = false;
        return 1;
    }

    _packet_ready.push_back (ready_packet_t (routing_id_value_, payload_msg_));
    return 0;
}

zlink::stream_t::packet_rx_state_t *
zlink::stream_t::get_or_create_packet_state (uint32_t routing_id_value_)
{
    const size_t idx = static_cast<size_t> (routing_id_value_);
    if (idx >= _packet_rx_states.size ())
        _packet_rx_states.resize (idx + 1, NULL);

    packet_rx_state_t *state = _packet_rx_states[idx];
    if (state)
        return state;

    state = new (std::nothrow) packet_rx_state_t;
    if (!state) {
        errno = ENOMEM;
        return NULL;
    }
    _packet_rx_states[idx] = state;

    return state;
}

void zlink::stream_t::destroy_packet_state (uint32_t routing_id_value_)
{
    const size_t idx = static_cast<size_t> (routing_id_value_);
    if (idx < _packet_rx_states.size ()) {
        packet_rx_state_t *state = _packet_rx_states[idx];
        if (state) {
            if (state->payload_msg) {
                release_packet_msg (state->payload_msg);
                state->payload_msg = NULL;
            }
            delete state;
            _packet_rx_states[idx] = NULL;
        }
    }

    for (packet_ready_deque_t::iterator q = _packet_ready.begin ();
         q != _packet_ready.end ();) {
        if (q->routing_id_value == routing_id_value_) {
            if (q->payload_msg) {
                release_packet_msg (q->payload_msg);
                q->payload_msg = NULL;
            }
            q = _packet_ready.erase (q);
        } else {
            ++q;
        }
    }
}

void zlink::stream_t::clear_packetized_state ()
{
    while (!_packet_ready.empty ()) {
        ready_packet_t ready = _packet_ready.front ();
        _packet_ready.pop_front ();
        if (ready.payload_msg) {
            release_packet_msg (ready.payload_msg);
            ready.payload_msg = NULL;
        }
    }

    for (size_t i = 0; i < _packet_rx_states.size (); ++i) {
        packet_rx_state_t *state = _packet_rx_states[i];
        if (!state)
            continue;
        if (state->payload_msg) {
            release_packet_msg (state->payload_msg);
            state->payload_msg = NULL;
        }
        delete state;
    }
    _packet_rx_states.clear ();
}

int zlink::stream_t::append_packetized_input (pipe_t *pipe_,
                                              uint32_t routing_id_value_,
                                              msg_t *chunk_)
{
    zlink_assert (pipe_ != NULL);
    zlink_assert (chunk_ != NULL);

    packet_rx_state_t *state = get_or_create_packet_state (routing_id_value_);
    if (!state) {
        int rc = chunk_->close ();
        errno_assert (rc == 0);
        rc = chunk_->init ();
        errno_assert (rc == 0);
        return -1;
    }

    const unsigned char *chunk_begin =
      static_cast<unsigned char *> (chunk_->data ());
    const unsigned char *chunk_data = chunk_begin;
    size_t remaining = chunk_->size ();

    while (remaining > 0) {
        if (state->header_filled < 4) {
            const size_t need = 4 - state->header_filled;
            const size_t n = need < remaining ? need : remaining;
            memcpy (state->header + state->header_filled, chunk_data, n);
            state->header_filled += n;
            chunk_data += n;
            remaining -= n;

            if (state->header_filled < 4)
                continue;

            const uint32_t payload_size = get_uint32 (state->header);
            if (payload_size
                > static_cast<uint32_t> (options.stream_packet_max_size)
                || static_cast<size_t> (payload_size) + 4
                     > static_cast<size_t> (
                       options.stream_packet_buffer_max)) {
                destroy_packet_state (routing_id_value_);
                pipe_->terminate (false);
                int rc = chunk_->close ();
                errno_assert (rc == 0);
                rc = chunk_->init ();
                errno_assert (rc == 0);
                return 0;
            }

            state->payload_size = payload_size;
            state->payload_written = 0;

            if (state->payload_size == 0) {
                msg_t *empty_msg = acquire_packet_msg ();
                if (!empty_msg) {
                    int rc = chunk_->close ();
                    errno_assert (rc == 0);
                    rc = chunk_->init ();
                    errno_assert (rc == 0);
                    return -1;
                }

                const int rc = empty_msg->init_size (0);
                if (rc != 0) {
                    const int saved_errno = errno;
                    release_packet_msg (empty_msg);
                    int close_rc = chunk_->close ();
                    errno_assert (close_rc == 0);
                    close_rc = chunk_->init ();
                    errno_assert (close_rc == 0);
                    errno = saved_errno;
                    return -1;
                }

                const int queue_rc =
                  queue_or_prefetch_packet (routing_id_value_, empty_msg);
                if (queue_rc < 0) {
                    int rc = chunk_->close ();
                    errno_assert (rc == 0);
                    rc = chunk_->init ();
                    errno_assert (rc == 0);
                    return -1;
                }
                state->header_filled = 0;
                state->payload_size = 0;
                state->payload_written = 0;
                continue;
            }
        }

        const size_t need = static_cast<size_t> (state->payload_size)
                            - state->payload_written;

        // Fast path: full payload is contiguous in this chunk.
        if (!state->payload_msg && state->payload_written == 0
            && need <= remaining) {
            msg_t *ready_msg = acquire_packet_msg ();
            if (!ready_msg) {
                int rc = chunk_->close ();
                errno_assert (rc == 0);
                rc = chunk_->init ();
                errno_assert (rc == 0);
                return -1;
            }

            int rc = 0;
            if (need <= static_cast<size_t> (stream_packet_copy_threshold)) {
                rc = ready_msg->init_size (need);
                if (rc == 0 && need > 0)
                    memcpy (ready_msg->data (), chunk_data, need);
            } else {
                const size_t payload_offset =
                  static_cast<size_t> (chunk_data - chunk_begin);
                rc = ready_msg->init_view (*chunk_, payload_offset, need);
            }

            if (rc != 0) {
                const int saved_errno = errno;
                release_packet_msg (ready_msg);
                int close_rc = chunk_->close ();
                errno_assert (close_rc == 0);
                close_rc = chunk_->init ();
                errno_assert (close_rc == 0);
                errno = saved_errno;
                return -1;
            }

            const int queue_rc =
              queue_or_prefetch_packet (routing_id_value_, ready_msg);
            if (queue_rc < 0) {
                int close_rc = chunk_->close ();
                errno_assert (close_rc == 0);
                close_rc = chunk_->init ();
                errno_assert (close_rc == 0);
                return -1;
            }
            chunk_data += need;
            remaining -= need;
            state->header_filled = 0;
            state->payload_size = 0;
            state->payload_written = 0;
            continue;
        }

        // Slow path: payload spans multiple chunks, copy into an assembly msg.
        if (!state->payload_msg) {
            state->payload_msg = acquire_packet_msg ();
            if (!state->payload_msg) {
                int rc = chunk_->close ();
                errno_assert (rc == 0);
                rc = chunk_->init ();
                errno_assert (rc == 0);
                return -1;
            }

            const int rc = state->payload_msg->init_size (state->payload_size);
            if (rc != 0) {
                const int saved_errno = errno;
                release_packet_msg (state->payload_msg);
                state->payload_msg = NULL;
                int close_rc = chunk_->close ();
                errno_assert (close_rc == 0);
                close_rc = chunk_->init ();
                errno_assert (close_rc == 0);
                errno = saved_errno;
                return -1;
            }
        }

        zlink_assert (state->payload_msg != NULL);
        const size_t n = need < remaining ? need : remaining;
        if (n > 0) {
            memcpy (static_cast<unsigned char *> (state->payload_msg->data ())
                      + state->payload_written,
                    chunk_data, n);
            state->payload_written += n;
            chunk_data += n;
            remaining -= n;
        }

        if (state->payload_written
            == static_cast<size_t> (state->payload_size)) {
            msg_t *ready_msg = state->payload_msg;
            state->payload_msg = NULL;
            const int queue_rc =
              queue_or_prefetch_packet (routing_id_value_, ready_msg);
            if (queue_rc < 0) {
                int close_rc = chunk_->close ();
                errno_assert (close_rc == 0);
                close_rc = chunk_->init ();
                errno_assert (close_rc == 0);
                return -1;
            }
            state->header_filled = 0;
            state->payload_size = 0;
            state->payload_written = 0;
        }
    }

    int rc = chunk_->close ();
    errno_assert (rc == 0);
    rc = chunk_->init ();
    errno_assert (rc == 0);
    return 0;
}

int zlink::stream_t::prefetch_packetized_message ()
{
    while (!_packet_ready.empty ()) {
        ready_packet_t ready = _packet_ready.front ();
        _packet_ready.pop_front ();
        if (!ready.payload_msg)
            continue;

        int rc = _prefetched_msg.move (*ready.payload_msg);
        if (rc != 0) {
            release_packet_msg (ready.payload_msg);
            return -1;
        }

        rc = _prefetched_msg.set_routing_id (ready.routing_id_value);
        errno_assert (rc == 0);

        recycle_packet_msg (ready.payload_msg);
        ready.payload_msg = NULL;

        _prefetched_routing_id_value = ready.routing_id_value;
        _prefetched = true;
        _routing_id_sent = false;
        return 1;
    }

    return 0;
}

void zlink::stream_t::init_routing_id_frame (msg_t *msg_,
                                             uint32_t routing_id_value_,
                                             metadata_t *metadata_)
{
    int rc = msg_->close ();
    errno_assert (rc == 0);
    rc = msg_->init_size (4);
    errno_assert (rc == 0);

    put_uint32 (static_cast<unsigned char *> (msg_->data ()),
                routing_id_value_);
    if (metadata_)
        msg_->set_metadata (metadata_);
    msg_->set_flags (msg_t::more);
}

void zlink::stream_t::emit_connect_event (pipe_t *pipe_)
{
    zlink_assert (pipe_);
    const blob_t &routing_id = pipe_->get_routing_id ();
    const unsigned char *routing_id_data =
      routing_id.size () ? routing_id.data () : NULL;
    event_connection_ready (pipe_->get_endpoint_pair (), routing_id_data,
                            routing_id.size ());
}

void zlink::stream_t::emit_disconnect_event (pipe_t *pipe_)
{
    zlink_assert (pipe_);
    const blob_t &routing_id = pipe_->get_routing_id ();
    const unsigned char *routing_id_data =
      routing_id.size () ? routing_id.data () : NULL;
    event_disconnected (pipe_->get_endpoint_pair (), ZLINK_DISCONNECT_UNKNOWN,
                        routing_id_data, routing_id.size ());
}
