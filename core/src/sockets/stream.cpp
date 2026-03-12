/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"
#include "sockets/stream.hpp"
#include "core/pipe.hpp"
#include "protocol/wire.hpp"
#include "utils/err.hpp"
#include "utils/likely.hpp"
#include <chrono>
#include <climits>
#include <cstdlib>
#include <thread>

namespace
{
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

// Keep a small read headroom so framed application protocols are less likely
// to split at the exact payload boundary.
const int stream_batch_read_headroom = parse_non_negative_int_env (
  "ZLINK_ASIO_STREAM_BATCH_HEADROOM", 64);

int apply_headroom (int base_, int headroom_)
{
    if (headroom_ <= 0 || base_ > INT_MAX - headroom_)
        return base_;
    return base_ + headroom_;
}

bool is_stream_control_event (const unsigned char *payload_, size_t size_)
{
    LIBZLINK_UNUSED (payload_);
    return size_ == 0;
}

uint32_t claim_next_routing_id (std::atomic<uint32_t> &next_)
{
    while (true) {
        const uint32_t candidate =
          next_.fetch_add (1, std::memory_order_relaxed);
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
    stream_dispatch_tls_t () : socket (NULL), pipe (NULL), routing_id (0)
    {
    }

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

zlink::pipe_t *resolve_connect_event_owner (zlink::pipe_t *pipe_)
{
    if (!pipe_)
        return NULL;

    zlink::pipe_t *owner = pipe_;
    zlink::pipe_t *peer = pipe_->get_peer ();
    if (peer && peer < owner)
        owner = peer;
    return owner;
}

}

zlink::stream_t::stream_t (class ctx_t *parent_, uint32_t tid_, int sid_) :
    routing_socket_base_t (parent_, tid_, sid_),
    _current_out (NULL),
    _more_out (false),
    _next_integral_routing_id (1),
    _dispatch_active (false),
    _dispatch_raw_callback (NULL)
{
    options.type = ZLINK_STREAM;
    options.backlog = 65536;
    if (options.sndbuf < 0)
        options.sndbuf = 262144;
    if (options.rcvbuf < 0)
        options.rcvbuf = 262144;

    const int stream_batch_size = stream_batch_size_min;
    const int stream_read_batch_size =
      apply_headroom (stream_batch_size, stream_batch_read_headroom);
    options.in_batch_size = stream_read_batch_size;
    options.out_batch_size = stream_batch_size;
}

zlink::stream_t::~stream_t ()
{
}

zlink::stream_t::route_shard_t &
zlink::stream_t::route_shard_for (uint32_t routing_id_)
{
    return _route_shards[routing_id_ % route_shard_count];
}

void zlink::stream_t::xattach_pipe (pipe_t *pipe_,
                                    bool subscribe_to_all_,
                                    bool locally_initiated_)
{
    LIBZLINK_UNUSED (subscribe_to_all_);
    zlink_assert (pipe_);

    identify_peer (pipe_, locally_initiated_);
    _fq.attach (pipe_);
    maybe_emit_connect_event (pipe_);
}

void zlink::stream_t::xpipe_terminated (pipe_t *pipe_)
{
    zlink_assert (pipe_);

    const uint32_t server_routing_id = pipe_->get_server_socket_routing_id ();

    std::lock_guard<std::recursive_mutex> api_lock (_api_mutex);
    if (pipe_ == _current_out)
        _current_out = NULL;

    if (server_routing_id != 0) {
        route_shard_t &shard = route_shard_for (server_routing_id);
        scoped_fast_lock_t shard_lock (shard.sync);
        route_shard_t::routes_t::iterator it = shard.routes.find (
          server_routing_id);
        if (it != shard.routes.end () && it->second == pipe_)
            shard.routes.erase (it);
    }
    erase_out_pipe (pipe_);
    _fq.pipe_terminated (pipe_);
}

void zlink::stream_t::xread_activated (pipe_t *pipe_)
{
    _fq.activated (pipe_);
}

int zlink::stream_t::xsend (msg_t *msg_)
{
    if (!_more_out && !(msg_->flags () & msg_t::more)
        && msg_->get_routing_id () != 0) {
        const uint32_t routing_id = msg_->get_routing_id ();
        route_shard_t &shard = route_shard_for (routing_id);
        scoped_fast_lock_t shard_lock (shard.sync);

        route_shard_t::routes_t::iterator it = shard.routes.find (routing_id);
        if (it == shard.routes.end () || !it->second) {
            errno = EHOSTUNREACH;
            return -1;
        }

        pipe_t *out = it->second;
        if (msg_->size () == 0) {
            out->terminate (false);
        } else {
            const bool ok = out->write (msg_);
            if (unlikely (!ok)) {
                errno = EAGAIN;
                return -1;
            }
            out->flush ();
        }

        const int init_rc = msg_->init ();
        errno_assert (init_rc == 0);
        return 0;
    }

    std::lock_guard<std::recursive_mutex> lk (_api_mutex);

    if (!_more_out) {
        zlink_assert (!_current_out);

        if (msg_->flags () & msg_t::more) {
            if (msg_->size () != 4) {
                errno = EINVAL;
                return -1;
            }

            const uint32_t routing_id =
              get_uint32 (static_cast<unsigned char *> (msg_->data ()));
            route_shard_t &shard = route_shard_for (routing_id);
            scoped_fast_lock_t shard_lock (shard.sync);
            route_shard_t::routes_t::iterator it = shard.routes.find (
              routing_id);
            if (it == shard.routes.end () || !it->second) {
                errno = EHOSTUNREACH;
                return -1;
            }

            _current_out = it->second;
            if (!_current_out->check_write ()) {
                _current_out = NULL;
                errno = EAGAIN;
                return -1;
            }
        }

        _more_out = true;

        int rc = msg_->close ();
        errno_assert (rc == 0);
        rc = msg_->init ();
        errno_assert (rc == 0);
        return 0;
    }

    msg_->reset_flags (msg_t::more);
    _more_out = false;

    if (_current_out) {
        if (msg_->size () == 0) {
            _current_out->terminate (false);
            int rc = msg_->close ();
            errno_assert (rc == 0);
            rc = msg_->init ();
            errno_assert (rc == 0);
            _current_out = NULL;
            return 0;
        }

        const bool ok = _current_out->write (msg_);
        if (likely (ok)) {
            _current_out->flush ();
        } else {
            _current_out = NULL;
            const int rc = msg_->close ();
            errno_assert (rc == 0);
            errno = EAGAIN;
            return -1;
        }
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
    LIBZLINK_UNUSED (msg_);
    errno = ENOTSUP;
    return -1;
}

bool zlink::stream_t::xhas_in ()
{
    return false;
}

bool zlink::stream_t::xhas_out ()
{
    return true;
}

int zlink::stream_t::xsetsockopt (int option_,
                                  const void *optval_,
                                  size_t optvallen_)
{
    if (option_ == ZLINK_CONNECT_ROUTING_ID || option_ == ZLINK_STREAM_NOTIFY) {
        LIBZLINK_UNUSED (optval_);
        LIBZLINK_UNUSED (optvallen_);
        errno = EOPNOTSUPP;
        return -1;
    }

    return routing_socket_base_t::xsetsockopt (option_, optval_, optvallen_);
}

int zlink::stream_t::stream_dispatch_start_raw (zlink_stream_on_raw_fn callback_)
{
    if (!callback_) {
        errno = EINVAL;
        return -1;
    }

    std::lock_guard<std::recursive_mutex> lk (_api_mutex);
    if (_dispatch_active.load (std::memory_order_acquire)) {
        errno = EBUSY;
        return -1;
    }

    _dispatch_raw_callback.store (callback_, std::memory_order_release);
    _dispatch_active.store (true, std::memory_order_release);
    return 0;
}

int zlink::stream_t::stream_dispatch_stop ()
{
    if (stream_dispatch_in_callback ()) {
        errno = EBUSY;
        return -1;
    }

    std::lock_guard<std::recursive_mutex> lk (_api_mutex);
    if (!_dispatch_active.load (std::memory_order_acquire)) {
        errno = EINVAL;
        return -1;
    }

    _dispatch_active.store (false, std::memory_order_release);
    _dispatch_raw_callback.store (NULL, std::memory_order_release);
    return 0;
}

bool zlink::stream_t::stream_dispatch_active () const
{
    return _dispatch_active.load (std::memory_order_acquire);
}

bool zlink::stream_t::stream_dispatch_in_callback () const
{
    return g_stream_dispatch_tls.socket == this;
}

int zlink::stream_t::stream_dispatch_send_from_io (
  const zlink_routing_id_t *rid_,
  const void *data_,
  size_t size_,
  int flags_)
{
    if (!rid_ || rid_->size != 4) {
        errno = EINVAL;
        return -1;
    }
    if (!data_ && size_ > 0) {
        errno = EINVAL;
        return -1;
    }

    const uint32_t routing_id =
      (static_cast<uint32_t> (rid_->data[0]) << 24)
      | (static_cast<uint32_t> (rid_->data[1]) << 16)
      | (static_cast<uint32_t> (rid_->data[2]) << 8)
      | static_cast<uint32_t> (rid_->data[3]);
    if (routing_id == 0) {
        errno = EINVAL;
        return -1;
    }

    if (size_ == 0) {
        route_shard_t &shard = route_shard_for (routing_id);
        scoped_fast_lock_t shard_lock (shard.sync);
        route_shard_t::routes_t::iterator it = shard.routes.find (routing_id);
        if (it == shard.routes.end () || !it->second) {
            errno = EHOSTUNREACH;
            return -1;
        }
        it->second->terminate (false);
        return 1;
    }

    msg_t out_msg;
    if (out_msg.init_buffer (data_, size_) != 0)
        return -1;

    const bool dontwait = (flags_ & ZLINK_DONTWAIT) != 0 || options.sndtimeo == 0;
    const int sndtimeo = options.sndtimeo;
    const std::chrono::steady_clock::time_point deadline =
      sndtimeo < 0
        ? std::chrono::steady_clock::time_point::max ()
        : std::chrono::steady_clock::now ()
            + std::chrono::milliseconds (sndtimeo);

    for (;;) {
        {
            route_shard_t &shard = route_shard_for (routing_id);
            scoped_fast_lock_t shard_lock (shard.sync);
            route_shard_t::routes_t::iterator it = shard.routes.find (
              routing_id);
            if (it == shard.routes.end () || !it->second) {
                const int rc = out_msg.close ();
                errno_assert (rc == 0);
                errno = EHOSTUNREACH;
                return -1;
            }

            if (it->second->write (&out_msg)) {
                it->second->flush ();
                const int init_rc = out_msg.init ();
                errno_assert (init_rc == 0);
                return 1;
            }
        }

        if (dontwait) {
            const int rc = out_msg.close ();
            errno_assert (rc == 0);
            errno = EAGAIN;
            return -1;
        }

        if (sndtimeo >= 0 && std::chrono::steady_clock::now () >= deadline) {
            const int rc = out_msg.close ();
            errno_assert (rc == 0);
            errno = EAGAIN;
            return -1;
        }

        std::this_thread::sleep_for (std::chrono::milliseconds (1));
    }
}

int zlink::stream_t::stream_dispatch_send_msg_from_io (
  const zlink_routing_id_t *rid_,
  msg_t *msg_,
  int flags_)
{
    if (!rid_ || rid_->size != 4 || !msg_) {
        errno = EINVAL;
        return -1;
    }

    const uint32_t routing_id =
      (static_cast<uint32_t> (rid_->data[0]) << 24)
      | (static_cast<uint32_t> (rid_->data[1]) << 16)
      | (static_cast<uint32_t> (rid_->data[2]) << 8)
      | static_cast<uint32_t> (rid_->data[3]);
    if (routing_id == 0) {
        errno = EINVAL;
        return -1;
    }

    const bool dontwait = (flags_ & ZLINK_DONTWAIT) != 0 || options.sndtimeo == 0;
    const int sndtimeo = options.sndtimeo;
    const std::chrono::steady_clock::time_point deadline =
      sndtimeo < 0
        ? std::chrono::steady_clock::time_point::max ()
        : std::chrono::steady_clock::now ()
            + std::chrono::milliseconds (sndtimeo);

    for (;;) {
        {
            route_shard_t &shard = route_shard_for (routing_id);
            scoped_fast_lock_t shard_lock (shard.sync);
            route_shard_t::routes_t::iterator it = shard.routes.find (
              routing_id);
            if (it == shard.routes.end () || !it->second) {
                errno = EHOSTUNREACH;
                return -1;
            }

            if (msg_->size () == 0) {
                it->second->terminate (false);
                const int init_rc = msg_->init ();
                errno_assert (init_rc == 0);
                return 1;
            }

            if (it->second->write (msg_)) {
                it->second->flush ();
                const int init_rc = msg_->init ();
                errno_assert (init_rc == 0);
                return 1;
            }
        }

        if (dontwait) {
            errno = EAGAIN;
            return -1;
        }

        if (sndtimeo >= 0 && std::chrono::steady_clock::now () >= deadline) {
            errno = EAGAIN;
            return -1;
        }

        std::this_thread::sleep_for (std::chrono::milliseconds (1));
    }
}

std::recursive_mutex *zlink::stream_t::api_sync_mutex ()
{
    return &_api_mutex;
}

void zlink::stream_t::stop_dispatch_from_callback ()
{
    std::lock_guard<std::recursive_mutex> lk (_api_mutex);
    _dispatch_active.store (false, std::memory_order_release);
    _dispatch_raw_callback.store (NULL, std::memory_order_release);
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

int zlink::stream_t::xstream_dispatch_msg (msg_t *msg_, pipe_t *pipe_)
{
    if (!_dispatch_active.load (std::memory_order_acquire))
        return 0;
    if (!msg_ || !pipe_)
        return 1;

    zlink_stream_on_raw_fn callback =
      _dispatch_raw_callback.load (std::memory_order_acquire);
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

    pipe_t *dispatch_out = pipe_->get_peer () ? pipe_->get_peer () : pipe_;
    {
        route_shard_t &shard = route_shard_for (routing_id_value);
        scoped_fast_lock_t shard_lock (shard.sync);
        if (shard.routes.find (routing_id_value) == shard.routes.end ())
            shard.routes[routing_id_value] = dispatch_out;
    }

    maybe_emit_connect_event (pipe_, routing_id_value);

    unsigned char rid_buf[4];
    put_uint32 (rid_buf, routing_id_value);
    zlink_routing_id_t rid;
    rid.size = 4;
    memcpy (rid.data, rid_buf, 4);

    const stream_dispatch_tls_scope_t tls_scope (this, pipe_, routing_id_value);

    msg_t callback_msg;
    const int init_rc = callback_msg.init ();
    errno_assert (init_rc == 0);
    const int move_rc = callback_msg.move (*msg_);
    errno_assert (move_rc == 0);
    const int src_init_rc = msg_->init ();
    errno_assert (src_init_rc == 0);

    const int cb_rc = callback (&rid,
                                reinterpret_cast<zlink_msg_t *> (&callback_msg));
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
    if (routing_id_value != 0) {
        maybe_emit_connect_event (pipe_, routing_id_value);
        return routing_id_value;
    }

    routing_id_value = claim_next_routing_id (_next_integral_routing_id);

    unsigned char routing_buf[4];
    put_uint32 (routing_buf, routing_id_value);
    blob_t routing_id;
    routing_id.set (routing_buf, sizeof routing_buf);
    target->set_router_socket_routing_id (routing_id);
    target->set_server_socket_routing_id (routing_id_value);
    if (pipe_ != target) {
        pipe_->set_router_socket_routing_id (routing_id);
        pipe_->set_server_socket_routing_id (routing_id_value);
    }

    maybe_emit_connect_event (pipe_, routing_id_value);
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
    pipe_t *peer = pipe_->get_peer ();
    if (peer) {
        peer->set_router_socket_routing_id (routing_id);
        peer->set_server_socket_routing_id (routing_id_value);
    }

    route_shard_t &shard = route_shard_for (routing_id_value);
    scoped_fast_lock_t shard_lock (shard.sync);
    shard.routes[routing_id_value] = pipe_;

    if (!has_out_pipe (routing_id))
        add_out_pipe (ZLINK_MOVE (routing_id), pipe_);
}

void zlink::stream_t::maybe_emit_connect_event (pipe_t *pipe_,
                                                uint32_t routing_id_value_)
{
    zlink_assert (pipe_);

    uint32_t resolved_routing_id = routing_id_value_;
    if (resolved_routing_id == 0)
        resolved_routing_id = pipe_->get_server_socket_routing_id ();
    if (resolved_routing_id == 0)
        return;

    pipe_t *event_owner = resolve_connect_event_owner (pipe_);
    if (!event_owner || !event_owner->mark_stream_connect_event_emitted ())
        return;

    unsigned char routing_id_data[4];
    put_uint32 (routing_id_data, resolved_routing_id);
    event_connection_ready (pipe_->get_endpoint_pair (), routing_id_data,
                            sizeof (routing_id_data));
}
