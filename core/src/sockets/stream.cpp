/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"
#include "sockets/stream.hpp"
#include "core/pipe.hpp"
#include "protocol/wire.hpp"
#include "utils/err.hpp"
#include "utils/likely.hpp"
#include <climits>
#include <cstdlib>
#include <new>

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

int parse_non_negative_int_env (const char *name_, int default_value_)
{
    const char *env = std::getenv (name_);
    if (!env || !*env)
        return default_value_;

    char *end = NULL;
    const long value = std::strtol (env, &end, 10);
    if (!end || end == env || value < 0 || value > INT_MAX)
        return default_value_;
    return static_cast<int> (value);
}

const int stream_batch_size_min = 12288;

// Keep a small read headroom so framed application protocols (e.g. length
// prefix + payload) are less likely to split at the exact payload boundary.
// This is read-side only; write batch keeps its base size to preserve
// encoder zero-copy thresholds.
const int stream_batch_read_headroom = parse_non_negative_int_env (
  "ZLINK_ASIO_STREAM_BATCH_HEADROOM", 64);

int apply_headroom (int base_, int headroom_)
{
    if (headroom_ <= 0 || base_ > INT_MAX - headroom_)
        return base_;
    return base_ + headroom_;
}

const size_t stream_len32be_max_payload = 4 * 1024 * 1024;
const size_t stream_dispatch_reassembly_reserve = 16384;

uint32_t load_u32_be (const unsigned char *src_)
{
    return (static_cast<uint32_t> (src_[0]) << 24)
           | (static_cast<uint32_t> (src_[1]) << 16)
           | (static_cast<uint32_t> (src_[2]) << 8)
           | static_cast<uint32_t> (src_[3]);
}

bool is_stream_control_event (const unsigned char *payload_, size_t size_)
{
    return size_ == 0
           || (size_ == 1 && payload_
               && (payload_[0] == 0x00 || payload_[0] == 0x01));
}

uint32_t claim_next_routing_id (std::atomic<uint32_t> &next_)
{
    while (true) {
        const uint32_t candidate = next_.fetch_add (1, std::memory_order_relaxed);
        if (candidate != 0)
            return candidate;
    }
}

uint32_t resolve_dispatch_routing_id (const zlink::msg_t *msg_,
                                      zlink::pipe_t *pipe_)
{
    if (msg_) {
        const uint32_t msg_routing_id = msg_->get_routing_id ();
        if (msg_routing_id != 0)
            return msg_routing_id;
    }

    if (!pipe_)
        return 0;

    uint32_t routing_id = pipe_->get_server_socket_routing_id ();
    if (routing_id != 0)
        return routing_id;

    const zlink::blob_t &router_routing_id = pipe_->get_routing_id ();
    if (router_routing_id.size () == 4)
        return zlink::get_uint32 (router_routing_id.data ());

    zlink::pipe_t *peer = pipe_->get_peer ();
    if (peer) {
        routing_id = peer->get_server_socket_routing_id ();
        if (routing_id != 0)
            return routing_id;

        const zlink::blob_t &peer_routing_id = peer->get_routing_id ();
        if (peer_routing_id.size () == 4)
            return zlink::get_uint32 (peer_routing_id.data ());
    }
    return 0;
}

struct stream_dispatch_tls_t
{
    stream_dispatch_tls_t () : socket (NULL), pipe (NULL), routing_id (0) {}

    zlink::stream_t *socket;
    zlink::pipe_t *pipe;
    uint32_t routing_id;
};

thread_local stream_dispatch_tls_t g_stream_dispatch_tls;

class stream_dispatch_tls_scope_t
{
  public:
    stream_dispatch_tls_scope_t (zlink::stream_t *socket_,
                                 zlink::pipe_t *pipe_,
                                 uint32_t routing_id_) :
        _prev (g_stream_dispatch_tls)
    {
        g_stream_dispatch_tls.socket = socket_;
        g_stream_dispatch_tls.pipe = pipe_;
        g_stream_dispatch_tls.routing_id = routing_id_;
    }

    ~stream_dispatch_tls_scope_t () { g_stream_dispatch_tls = _prev; }

  private:
    stream_dispatch_tls_t _prev;
};

void close_msg_batch (zlink::msg_t *batch_, size_t count_)
{
    if (!batch_)
        return;

    for (size_t i = 0; i < count_; ++i) {
        const int rc = batch_[i].close ();
        errno_assert (rc == 0);
    }
}

void free_msg_batch (zlink::msg_t *batch_, size_t count_)
{
    close_msg_batch (batch_, count_);
    std::free (batch_);
}

int append_msg_batch (zlink::msg_t **batch_,
                      size_t *count_,
                      size_t *capacity_,
                      zlink::msg_t *src_)
{
    zlink_assert (batch_);
    zlink_assert (count_);
    zlink_assert (capacity_);
    zlink_assert (src_);

    if (*count_ == *capacity_) {
        const size_t next_capacity = *capacity_ > 0 ? (*capacity_ * 2) : 4;
        zlink::msg_t *next_batch = static_cast<zlink::msg_t *> (
          std::malloc (next_capacity * sizeof (zlink::msg_t)));
        if (!next_batch) {
            errno = ENOMEM;
            return -1;
        }

        for (size_t i = 0; i < *count_; ++i) {
            int init_rc = next_batch[i].init ();
            errno_assert (init_rc == 0);

            const int move_rc = next_batch[i].move ((*batch_)[i]);
            errno_assert (move_rc == 0);

            const int close_rc = (*batch_)[i].close ();
            errno_assert (close_rc == 0);
        }

        std::free (*batch_);
        *batch_ = next_batch;
        *capacity_ = next_capacity;
    }

    int init_rc = (*batch_)[*count_].init ();
    errno_assert (init_rc == 0);

    const int move_rc = (*batch_)[*count_].move (*src_);
    errno_assert (move_rc == 0);
    ++(*count_);
    return 0;
}

}

zlink::stream_t::stream_t (class ctx_t *parent_, uint32_t tid_, int sid_) :
    routing_socket_base_t (parent_, tid_, sid_),
    _prefetched (false),
    _routing_id_sent (false),
    _prefetched_routing_id_value (0),
    _current_out (NULL),
    _current_out_checked_writable (false),
    _more_out (false),
    _next_integral_routing_id (1),
    _dispatch_active (false),
    _dispatch_len32be (false),
    _dispatch_callback (NULL)
{
    options.type = ZLINK_STREAM;
    options.backlog = 65536;
    if (options.sndbuf < 0)
        options.sndbuf = 262144;
    if (options.rcvbuf < 0)
        options.rcvbuf = 262144;
    const int stream_batch_size = stream_batch_size_min;
    const int stream_in_batch_base = stream_batch_size;
    const int stream_read_batch_size =
      apply_headroom (stream_in_batch_base, stream_batch_read_headroom);
    if (options.in_batch_size < stream_read_batch_size)
        options.in_batch_size = stream_read_batch_size;
    if (options.out_batch_size < stream_batch_size)
        options.out_batch_size = stream_batch_size;

    _prefetched_msg.init ();
}

zlink::stream_t::~stream_t ()
{
    clear_reassembly ();
    _prefetched_msg.close ();
}

zlink::stream_t::stream_reassembly_state_t::stream_reassembly_state_t () :
    header (),
    header_written (0),
    payload_size (0),
    payload_written (0),
    packet (),
    packet_active (false)
{
    header[0] = 0;
    header[1] = 0;
    header[2] = 0;
    header[3] = 0;
}

zlink::stream_t::stream_reassembly_state_t::~stream_reassembly_state_t ()
{
    if (packet_active) {
        const int rc = packet.close ();
        errno_assert (rc == 0);
    }
}

void zlink::stream_t::stream_reassembly_state_t::reset_frame ()
{
    if (packet_active) {
        const int rc = packet.close ();
        errno_assert (rc == 0);
        packet_active = false;
    }
    header_written = 0;
    payload_size = 0;
    payload_written = 0;
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

    if (server_routing_id != 0) {
        std::lock_guard<std::mutex> lk (_dispatch_reassembly_mu);
        stream_reassembly_map_t::iterator it =
          _dispatch_reassembly.find (server_routing_id);
        if (it != _dispatch_reassembly.end ()) {
            _dispatch_reassembly.erase (it);
        }
    }

    if (options.stream_notify)
        queue_notify_event (server_routing_id);
}

void zlink::stream_t::xread_activated (pipe_t *pipe_)
{
    _fq.activated (pipe_);
}

int zlink::stream_t::xsend (msg_t *msg_)
{
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

        const bool ok = _current_out_checked_writable
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
    if (_prefetched)
        return deliver_prefetched (msg_);

    if (options.stream_notify && prefetch_notify_event ())
        return deliver_prefetched (msg_);

    pipe_t *pipe = NULL;
    int rc = _fq.recvpipe (&_prefetched_msg, &pipe);
    if (rc != 0)
        return -1;

    zlink_assert (pipe != NULL);

    // Match libzmq STREAM fast-path: keep payload prefetched and return
    // routing-id frame directly to caller without extra msg move indirection.
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

int zlink::stream_t::stream_dispatch_start (zlink_stream_on_packets_fn callback_,
                                            int flags_)
{
    if (!callback_) {
        errno = EINVAL;
        return -1;
    }

    std::lock_guard<std::mutex> lk (_dispatch_control_mu);
    if (_dispatch_active.load (std::memory_order_acquire)) {
        errno = EBUSY;
        return -1;
    }

    clear_reassembly ();
    {
        std::lock_guard<std::mutex> reassembly_lk (_dispatch_reassembly_mu);
        try {
            _dispatch_reassembly.reserve (stream_dispatch_reassembly_reserve);
        } catch (const std::bad_alloc &) {
            errno = ENOMEM;
            return -1;
        }
    }

    _dispatch_callback.store (callback_, std::memory_order_release);
    _dispatch_len32be.store ((flags_ & ZLINK_STREAM_DISPATCH_LEN32BE) != 0,
                             std::memory_order_release);
    _dispatch_active.store (true, std::memory_order_release);
    return 0;
}

int zlink::stream_t::stream_dispatch_stop ()
{
    std::lock_guard<std::mutex> lk (_dispatch_control_mu);
    if (!_dispatch_active.load (std::memory_order_acquire)) {
        errno = EINVAL;
        return -1;
    }

    _dispatch_active.store (false, std::memory_order_release);
    _dispatch_len32be.store (false, std::memory_order_release);
    _dispatch_callback.store (NULL, std::memory_order_release);
    clear_reassembly ();
    return 0;
}

bool zlink::stream_t::stream_dispatch_len32be_enabled () const
{
    return _dispatch_active.load (std::memory_order_acquire)
           && _dispatch_len32be.load (std::memory_order_acquire);
}

bool zlink::stream_t::stream_dispatch_active () const
{
    return _dispatch_active.load (std::memory_order_acquire);
}

int zlink::stream_t::stream_dispatch_send_from_io (
  const zlink_routing_id_t *rid_,
  const void *data_,
  size_t size_,
  int flags_,
  bool len32be_)
{
    LIBZLINK_UNUSED (flags_);

    if (!rid_ || rid_->size != 4) {
        errno = EINVAL;
        return -1;
    }

    if (!data_ && size_ > 0) {
        errno = EINVAL;
        return -1;
    }

    if (!stream_dispatch_active ())
        return 0;

    const uint32_t routing_id =
      (static_cast<uint32_t> (rid_->data[0]) << 24)
      | (static_cast<uint32_t> (rid_->data[1]) << 16)
      | (static_cast<uint32_t> (rid_->data[2]) << 8)
      | static_cast<uint32_t> (rid_->data[3]);

    if (routing_id == 0) {
        errno = EINVAL;
        return -1;
    }

    if (g_stream_dispatch_tls.socket != this || !g_stream_dispatch_tls.pipe
        || g_stream_dispatch_tls.routing_id != routing_id)
        return 0;

    pipe_t *out = g_stream_dispatch_tls.pipe->get_peer ();
    if (!out) {
        errno = EHOSTUNREACH;
        return -1;
    }

    if (!len32be_ && size_ == 0) {
        out->terminate (false);
        return 1;
    }

    if (len32be_ && size_ > static_cast<size_t> (0xFFFFFFFFu)) {
        errno = EMSGSIZE;
        return -1;
    }

    const size_t wire_size = len32be_ ? (size_ + 4) : size_;

    msg_t out_msg;
    if (out_msg.init_size (wire_size) != 0)
        return -1;

    unsigned char *dst = static_cast<unsigned char *> (out_msg.data ());
    if (len32be_) {
        put_uint32 (dst, static_cast<uint32_t> (size_));
        if (size_ > 0)
            memcpy (dst + 4, data_, size_);
    } else if (size_ > 0) {
        memcpy (dst, data_, size_);
    }

    if (!out->write (&out_msg)) {
        const int close_rc = out_msg.close ();
        errno_assert (close_rc == 0);
        errno = EAGAIN;
        return -1;
    }

    out->flush ();
    const int init_rc = out_msg.init ();
    errno_assert (init_rc == 0);
    return 1;
}

void zlink::stream_t::clear_reassembly ()
{
    std::lock_guard<std::mutex> lk (_dispatch_reassembly_mu);
    _dispatch_reassembly.clear ();
}

void zlink::stream_t::stop_dispatch_from_callback ()
{
    std::lock_guard<std::mutex> lk (_dispatch_control_mu);
    _dispatch_active.store (false, std::memory_order_release);
    _dispatch_len32be.store (false, std::memory_order_release);
    _dispatch_callback.store (NULL, std::memory_order_release);
    clear_reassembly ();
}

zlink::stream_t::stream_reassembly_state_ptr_t
zlink::stream_t::find_reassembly_state (uint32_t routing_id_value_) const
{
    std::lock_guard<std::mutex> lk (_dispatch_reassembly_mu);
    stream_reassembly_map_t::const_iterator it =
      _dispatch_reassembly.find (routing_id_value_);
    if (it == _dispatch_reassembly.end ())
        return stream_reassembly_state_ptr_t ();
    return it->second;
}

zlink::stream_t::stream_reassembly_state_ptr_t
zlink::stream_t::get_or_create_reassembly_state (uint32_t routing_id_value_)
{
    {
        std::lock_guard<std::mutex> lk (_dispatch_reassembly_mu);
        stream_reassembly_map_t::iterator it =
          _dispatch_reassembly.find (routing_id_value_);
        if (it != _dispatch_reassembly.end ())
            return it->second;
    }

    stream_reassembly_state_t *raw_state =
      new (std::nothrow) stream_reassembly_state_t ();
    if (!raw_state) {
        errno = ENOMEM;
        return stream_reassembly_state_ptr_t ();
    }

    stream_reassembly_state_ptr_t candidate_state (raw_state);
    std::lock_guard<std::mutex> lk (_dispatch_reassembly_mu);
    stream_reassembly_map_t::iterator it =
      _dispatch_reassembly.find (routing_id_value_);
    if (it != _dispatch_reassembly.end ())
        return it->second;

    _dispatch_reassembly[routing_id_value_] = candidate_state;
    return candidate_state;
}

uint32_t zlink::stream_t::resolve_dispatch_routing_id_fast (const msg_t *msg_,
                                                            pipe_t *pipe_)
{
    if (pipe_) {
        const uint32_t pipe_routing_id = pipe_->get_server_socket_routing_id ();
        if (pipe_routing_id != 0)
            return pipe_routing_id;
    }

    if (msg_) {
        const uint32_t msg_routing_id = msg_->get_routing_id ();
        if (msg_routing_id != 0)
            return msg_routing_id;
    }

    return resolve_dispatch_routing_id (msg_, pipe_);
}

int zlink::stream_t::dispatch_len32be (msg_t *msg_, pipe_t *pipe_)
{
    uint32_t routing_id_value = resolve_dispatch_routing_id_fast (msg_, pipe_);
    if (routing_id_value == 0)
        routing_id_value = ensure_dispatch_routing_id (pipe_);
    if (routing_id_value == 0)
        return 1;

    zlink_stream_on_packets_fn callback =
      _dispatch_callback.load (std::memory_order_acquire);
    if (!callback)
        return 1;

    const unsigned char *payload =
      static_cast<const unsigned char *> (msg_->data ());
    const size_t payload_size = msg_->size ();

    unsigned char rid_buf[4];
    put_uint32 (rid_buf, routing_id_value);
    zlink_routing_id_t rid;
    rid.size = 4;
    memcpy (rid.data, rid_buf, 4);

    if (is_stream_control_event (payload, payload_size)) {
        const stream_reassembly_state_ptr_t state =
          find_reassembly_state (routing_id_value);
        if (state) {
            std::lock_guard<std::mutex> state_lk (state->mutex);
            state->reset_frame ();
        }
        return 1;
    }

    const stream_reassembly_state_ptr_t state =
      get_or_create_reassembly_state (routing_id_value);
    if (!state)
        return -1;

    msg_t single_packet;
    bool single_packet_ready = false;
    msg_t *batch = NULL;
    size_t batch_count = 0;
    size_t batch_capacity = 0;
    size_t consumed_total = 0;

    while (consumed_total < payload_size) {
        msg_t packet;
        int init_rc = packet.init ();
        errno_assert (init_rc == 0);

        size_t consumed = 0;
        bool packet_ready = false;

        {
            std::lock_guard<std::mutex> state_lk (state->mutex);
            const unsigned char *src = payload + consumed_total;
            const size_t src_size = payload_size - consumed_total;

            while (consumed < src_size) {
                if (state->header_written < 4) {
                    const size_t need = 4 - state->header_written;
                    const size_t available = src_size - consumed;
                    const size_t copied = need < available ? need : available;
                    memcpy (state->header + state->header_written, src + consumed,
                            copied);
                    state->header_written += copied;
                    consumed += copied;

                    if (state->header_written < 4)
                        break;

                    state->payload_size = load_u32_be (state->header);
                    state->payload_written = 0;
                    if (state->payload_size > stream_len32be_max_payload) {
                        state->reset_frame ();
                        consumed = src_size;
                        break;
                    }

                    if (state->packet.init_size (state->payload_size) != 0) {
                        state->reset_frame ();
                        const int close_rc = packet.close ();
                        errno_assert (close_rc == 0);
                        if (single_packet_ready) {
                            const int single_close_rc = single_packet.close ();
                            errno_assert (single_close_rc == 0);
                        }
                        free_msg_batch (batch, batch_count);
                        errno = ENOMEM;
                        return -1;
                    }
                    state->packet_active = true;

                    if (state->payload_size == 0) {
                        const int move_rc = packet.move (state->packet);
                        errno_assert (move_rc == 0);
                        state->packet_active = false;
                        state->header_written = 0;
                        state->payload_size = 0;
                        state->payload_written = 0;
                        packet_ready = true;
                        break;
                    }
                }

                if (!state->packet_active)
                    break;

                const size_t remaining =
                  state->payload_size - state->payload_written;
                const size_t available = src_size - consumed;
                const size_t copied = remaining < available ? remaining : available;
                if (copied == 0)
                    break;

                memcpy (static_cast<unsigned char *> (state->packet.data ())
                          + state->payload_written,
                        src + consumed, copied);
                state->payload_written += copied;
                consumed += copied;

                if (state->payload_written == state->payload_size) {
                    const int move_rc = packet.move (state->packet);
                    errno_assert (move_rc == 0);
                    state->packet_active = false;
                    state->header_written = 0;
                    state->payload_size = 0;
                    state->payload_written = 0;
                    packet_ready = true;
                    break;
                }
            }
        }

        if (consumed == 0 && !packet_ready) {
            const int close_rc = packet.close ();
            errno_assert (close_rc == 0);
            break;
        }

        consumed_total += consumed;

        if (!packet_ready) {
            const int close_rc = packet.close ();
            errno_assert (close_rc == 0);
            continue;
        }

        if (!single_packet_ready && batch_count == 0) {
            const int single_init_rc = single_packet.init ();
            errno_assert (single_init_rc == 0);
            const int move_rc = single_packet.move (packet);
            errno_assert (move_rc == 0);
            single_packet_ready = true;
        } else {
            if (batch_count == 0 && single_packet_ready) {
                if (append_msg_batch (&batch, &batch_count, &batch_capacity,
                                      &single_packet)
                    != 0) {
                    const int close_rc = packet.close ();
                    errno_assert (close_rc == 0);
                    const int single_close_rc = single_packet.close ();
                    errno_assert (single_close_rc == 0);
                    single_packet_ready = false;
                    free_msg_batch (batch, batch_count);
                    return -1;
                }
                const int single_close_rc = single_packet.close ();
                errno_assert (single_close_rc == 0);
                single_packet_ready = false;
            }

            if (append_msg_batch (&batch, &batch_count, &batch_capacity, &packet)
                != 0) {
                const int close_rc = packet.close ();
                errno_assert (close_rc == 0);
                free_msg_batch (batch, batch_count);
                return -1;
            }
        }

        const int close_rc = packet.close ();
        errno_assert (close_rc == 0);
    }

    if (!single_packet_ready && batch_count == 0) {
        std::free (batch);
        return 1;
    }

    const stream_dispatch_tls_scope_t tls_scope (this, pipe_, routing_id_value);
    int cb_rc = 0;
    if (batch_count == 0) {
        cb_rc = callback (&rid, reinterpret_cast<zlink_msg_t *> (&single_packet),
                          1);
        const int close_rc = single_packet.close ();
        errno_assert (close_rc == 0);
    } else {
        cb_rc = callback (&rid, reinterpret_cast<zlink_msg_t *> (batch),
                          batch_count);
        free_msg_batch (batch, batch_count);
    }
    if (cb_rc != 0)
        stop_dispatch_from_callback ();

    return 1;
}

int zlink::stream_t::xstream_dispatch_msg (msg_t *msg_, pipe_t *pipe_)
{
    if (!_dispatch_active.load (std::memory_order_acquire))
        return 0;
    if (!msg_ || !pipe_)
        return 1;

    if (_dispatch_len32be.load (std::memory_order_acquire))
        return dispatch_len32be (msg_, pipe_);

    zlink_stream_on_packets_fn callback =
      _dispatch_callback.load (std::memory_order_acquire);
    if (!callback)
        return 1;

    const unsigned char *payload =
      static_cast<const unsigned char *> (msg_->data ());
    const size_t payload_size = msg_->size ();
    if (is_stream_control_event (payload, payload_size))
        return 1;

    uint32_t routing_id_value = resolve_dispatch_routing_id_fast (msg_, pipe_);
    if (routing_id_value == 0)
        routing_id_value = ensure_dispatch_routing_id (pipe_);
    if (routing_id_value == 0)
        return 1;

    unsigned char rid_buf[4];
    put_uint32 (rid_buf, routing_id_value);
    zlink_routing_id_t rid;
    rid.size = 4;
    memcpy (rid.data, rid_buf, 4);

    const stream_dispatch_tls_scope_t tls_scope (this, pipe_,
                                                 routing_id_value);
    const int cb_rc =
      callback (&rid, reinterpret_cast<zlink_msg_t *> (msg_), 1);
    if (cb_rc != 0)
        stop_dispatch_from_callback ();

    return 1;
}

uint32_t zlink::stream_t::ensure_dispatch_routing_id (pipe_t *pipe_)
{
    if (!pipe_)
        return 0;

    pipe_t *target = pipe_->get_peer () ? pipe_->get_peer () : pipe_;
    uint32_t routing_id_value = target->get_server_socket_routing_id ();
    if (routing_id_value != 0)
        return routing_id_value;

    routing_id_value = claim_next_routing_id (_next_integral_routing_id);

    unsigned char routing_buf[4];
    put_uint32 (routing_buf, routing_id_value);
    blob_t routing_id;
    routing_id.set (routing_buf, sizeof routing_buf);
    target->set_router_socket_routing_id (routing_id);
    target->set_server_socket_routing_id (routing_id_value);

    return routing_id_value;
}

void zlink::stream_t::identify_peer (pipe_t *pipe_, bool locally_initiated_)
{
    LIBZLINK_UNUSED (locally_initiated_);
    blob_t routing_id;

    uint32_t routing_id_value = pipe_->get_server_socket_routing_id ();
    if (routing_id_value == 0)
        routing_id_value = claim_next_routing_id (_next_integral_routing_id);

    unsigned char buf[4];
    put_uint32 (buf, routing_id_value);
    routing_id.set (buf, sizeof buf);

    pipe_->set_router_socket_routing_id (routing_id);
    pipe_->set_server_socket_routing_id (routing_id_value);

    const size_t idx = static_cast<size_t> (routing_id_value);
    if (idx >= _out_by_id.size ())
        _out_by_id.resize (idx + 1, NULL);
    _out_by_id[idx] = pipe_;

    if (!has_out_pipe (routing_id))
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

int zlink::stream_t::deliver_prefetched (msg_t *msg_)
{
    zlink_assert (_prefetched);

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
