/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"
#define ZMQ_TYPE_UNSAFE

#include "utils/macros.hpp"
#include "core/poller.hpp"
#include "utils/random.hpp"

#if !defined ZMQ_HAVE_POLLER
#if defined ZMQ_POLL_BASED_ON_POLL && !defined ZMQ_HAVE_WINDOWS
#include <poll.h>
#endif
#include "utils/polling_util.hpp"
#endif

#include <cstdio>

#if !defined ZMQ_HAVE_WINDOWS
#include <unistd.h>
#ifdef ZMQ_HAVE_VXWORKS
#include <strings.h>
#endif
#endif

// XSI vector I/O
#if defined ZMQ_HAVE_UIO
#include <sys/uio.h>
#else
struct iovec
{
    void *iov_base;
    size_t iov_len;
};
#endif

#include <string.h>
#include <stdlib.h>
#include <new>
#include <climits>

#include "sockets/proxy.hpp"
#include "sockets/socket_base.hpp"
#include "sockets/thread_safe_socket.hpp"
#include "utils/stdint.hpp"
#include "utils/config.hpp"
#include "utils/likely.hpp"
#include "utils/clock.hpp"
#include "core/ctx.hpp"
#include "utils/err.hpp"
#include "core/msg.hpp"
#include "utils/fd.hpp"
#include "protocol/metadata.hpp"
#include "core/socket_poller.hpp"
#include "core/timers.hpp"
#include "utils/ip.hpp"
#include "core/address.hpp"

#ifdef ZMQ_HAVE_PPOLL
#include "utils/polling_util.hpp"
#include <sys/select.h>
#endif

//  Compile time check whether msg_t fits into zmq_msg_t.
typedef char
  check_msg_t_size[sizeof (zmq::msg_t) == sizeof (zmq_msg_t) ? 1 : -1];

//  Forward declarations for internal draft API functions
int zmq_msg_init_buffer (zmq_msg_t *msg_, const void *buf_, size_t size_);
int zmq_ctx_set_ext (void *ctx_, int option_, const void *optval_, size_t optvallen_);
int zmq_poller_wait_all (void *poller_, zmq_poller_event_t *events_, int n_events_, long timeout_);

void zmq_version (int *major_, int *minor_, int *patch_)
{
    *major_ = ZMQ_VERSION_MAJOR;
    *minor_ = ZMQ_VERSION_MINOR;
    *patch_ = ZMQ_VERSION_PATCH;
}

const char *zmq_strerror (int errnum_)
{
    return zmq::errno_to_string (errnum_);
}

int zmq_errno (void)
{
    return errno;
}

//  New context API

void *zmq_ctx_new (void)
{
    if (!zmq::initialize_network ()) {
        return NULL;
    }

    zmq::ctx_t *ctx = new (std::nothrow) zmq::ctx_t;
    if (ctx) {
        if (!ctx->valid ()) {
            delete ctx;
            return NULL;
        }
    }
    return ctx;
}

int zmq_ctx_term (void *ctx_)
{
    if (!ctx_ || !(static_cast<zmq::ctx_t *> (ctx_))->check_tag ()) {
        errno = EFAULT;
        return -1;
    }

    const int rc = (static_cast<zmq::ctx_t *> (ctx_))->terminate ();
    const int en = errno;

    if (!rc || en != EINTR) {
        zmq::shutdown_network ();
    }

    errno = en;
    return rc;
}

int zmq_ctx_shutdown (void *ctx_)
{
    if (!ctx_ || !(static_cast<zmq::ctx_t *> (ctx_))->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    return (static_cast<zmq::ctx_t *> (ctx_))->shutdown ();
}

int zmq_ctx_set (void *ctx_, int option_, int optval_)
{
    return zmq_ctx_set_ext (ctx_, option_, &optval_, sizeof (int));
}

int zmq_ctx_set_ext (void *ctx_,
                     int option_,
                     const void *optval_,
                     size_t optvallen_)
{
    if (!ctx_ || !(static_cast<zmq::ctx_t *> (ctx_))->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    return (static_cast<zmq::ctx_t *> (ctx_))
      ->set (option_, optval_, optvallen_);
}

int zmq_ctx_get (void *ctx_, int option_)
{
    if (!ctx_ || !(static_cast<zmq::ctx_t *> (ctx_))->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    return (static_cast<zmq::ctx_t *> (ctx_))->get (option_);
}

// Sockets

struct socket_handle_t
{
    zmq::socket_base_t *socket;
    zmq::thread_safe_socket_t *threadsafe;
};

static inline socket_handle_t as_socket_handle (void *s_)
{
    socket_handle_t handle;
    handle.socket = NULL;
    handle.threadsafe = NULL;

    if (!s_) {
        errno = ENOTSOCK;
        return handle;
    }

    zmq::socket_base_t *s = static_cast<zmq::socket_base_t *> (s_);
    if (!s->check_tag ()) {
        errno = ENOTSOCK;
        return handle;
    }

    handle.socket = s;
    handle.threadsafe = s->get_threadsafe_proxy ();
    return handle;
}

void *zmq_socket (void *ctx_, int type_)
{
    if (!ctx_ || !(static_cast<zmq::ctx_t *> (ctx_))->check_tag ()) {
        errno = EFAULT;
        return NULL;
    }
    zmq::ctx_t *ctx = static_cast<zmq::ctx_t *> (ctx_);
    zmq::socket_base_t *s = ctx->create_socket (type_);
    return static_cast<void *> (s);
}

void *zmq_socket_threadsafe (void *ctx_, int type_)
{
    if (!ctx_ || !(static_cast<zmq::ctx_t *> (ctx_))->check_tag ()) {
        errno = EFAULT;
        return NULL;
    }

    zmq::ctx_t *ctx = static_cast<zmq::ctx_t *> (ctx_);
    zmq::socket_base_t *s = ctx->create_socket (type_);
    if (!s)
        return NULL;

    zmq::thread_safe_socket_t *ts =
      new (std::nothrow) zmq::thread_safe_socket_t (ctx, s);
    if (!ts) {
        s->close ();
        errno = ENOMEM;
        return NULL;
    }
    s->set_threadsafe_proxy (ts);
    return static_cast<void *> (s);
}

int zmq_close (void *s_)
{
    socket_handle_t handle = as_socket_handle (s_);
    if (!handle.socket)
        return -1;
    if (handle.threadsafe) {
        const int rc = handle.threadsafe->close ();
        if (rc == -1)
            return -1;
        return 0;
    }
    handle.socket->close ();
    return 0;
}

int zmq_setsockopt (void *s_,
                    int option_,
                    const void *optval_,
                    size_t optvallen_)
{
    socket_handle_t handle = as_socket_handle (s_);
    if (!handle.socket)
        return -1;
    if (handle.threadsafe)
        return handle.threadsafe->setsockopt (option_, optval_, optvallen_);
    return handle.socket->setsockopt (option_, optval_, optvallen_);
}

int zmq_getsockopt (void *s_, int option_, void *optval_, size_t *optvallen_)
{
    socket_handle_t handle = as_socket_handle (s_);
    if (!handle.socket)
        return -1;
    if (handle.threadsafe)
        return handle.threadsafe->getsockopt (option_, optval_, optvallen_);
    return handle.socket->getsockopt (option_, optval_, optvallen_);
}

int zmq_is_threadsafe (void *s_)
{
    socket_handle_t handle = as_socket_handle (s_);
    if (!handle.socket)
        return -1;
    if (handle.threadsafe)
        return 1;
    return handle.socket->is_thread_safe () ? 1 : 0;
}

int zmq_socket_monitor (void *s_, const char *addr_, int events_)
{
    socket_handle_t handle = as_socket_handle (s_);
    if (!handle.socket)
        return -1;
    if (handle.threadsafe)
        return handle.threadsafe->monitor (addr_, events_, 3, ZMQ_PAIR);
    return handle.socket->monitor (addr_, events_, 3, ZMQ_PAIR);
}

void *zmq_socket_monitor_open (void *s_, int events_)
{
    socket_handle_t handle = as_socket_handle (s_);
    if (!handle.socket)
        return NULL;

    char endpoint[128];
    const uint32_t rand_id = zmq::generate_random ();
    snprintf (endpoint, sizeof endpoint, "inproc://monitor-%p-%u",
              static_cast<void *> (s_), rand_id);

    const int monitor_rc =
      handle.threadsafe
        ? handle.threadsafe->monitor (endpoint, events_, 3, ZMQ_PAIR)
        : handle.socket->monitor (endpoint, events_, 3, ZMQ_PAIR);
    if (monitor_rc != 0)
        return NULL;

    void *monitor_socket = zmq_socket (handle.socket->get_ctx (), ZMQ_PAIR);
    if (!monitor_socket) {
        if (handle.threadsafe)
            handle.threadsafe->monitor (NULL, 0, 3, ZMQ_PAIR);
        else
            handle.socket->monitor (NULL, 0, 3, ZMQ_PAIR);
        return NULL;
    }

    if (zmq_connect (monitor_socket, endpoint) != 0) {
        zmq_close (monitor_socket);
        if (handle.threadsafe)
            handle.threadsafe->monitor (NULL, 0, 3, ZMQ_PAIR);
        else
            handle.socket->monitor (NULL, 0, 3, ZMQ_PAIR);
        return NULL;
    }

    return monitor_socket;
}

int zmq_monitor_recv (void *monitor_socket_,
                      zmq_monitor_event_t *event_,
                      int flags_)
{
    if (!monitor_socket_ || !event_) {
        errno = EINVAL;
        return -1;
    }

    zmq_msg_t msg;
    zmq_msg_init (&msg);
    int rc = zmq_msg_recv (&msg, monitor_socket_, flags_);
    if (rc == -1) {
        zmq_msg_close (&msg);
        return -1;
    }

    memset (event_, 0, sizeof (*event_));

    if (zmq_msg_size (&msg) < sizeof (uint64_t)) {
        zmq_msg_close (&msg);
        errno = EPROTO;
        return -1;
    }

    memcpy (&event_->event, zmq_msg_data (&msg), sizeof (uint64_t));
    zmq_msg_close (&msg);

    const int follow_flags = flags_ & ~ZMQ_DONTWAIT;

    zmq_msg_init (&msg);
    rc = zmq_msg_recv (&msg, monitor_socket_, follow_flags);
    if (rc == -1) {
        zmq_msg_close (&msg);
        return -1;
    }
    if (zmq_msg_size (&msg) < sizeof (uint64_t)) {
        zmq_msg_close (&msg);
        errno = EPROTO;
        return -1;
    }

    uint64_t value_count = 0;
    memcpy (&value_count, zmq_msg_data (&msg), sizeof (uint64_t));
    zmq_msg_close (&msg);

    for (uint64_t i = 0; i < value_count; ++i) {
        zmq_msg_init (&msg);
        rc = zmq_msg_recv (&msg, monitor_socket_, follow_flags);
        if (rc == -1) {
            zmq_msg_close (&msg);
            return -1;
        }
        if (i == 0 && zmq_msg_size (&msg) >= sizeof (uint64_t)) {
            memcpy (&event_->value, zmq_msg_data (&msg), sizeof (uint64_t));
        }
        zmq_msg_close (&msg);
    }

    zmq_msg_init (&msg);
    rc = zmq_msg_recv (&msg, monitor_socket_, follow_flags);
    if (rc == -1) {
        zmq_msg_close (&msg);
        return -1;
    }
    const size_t routing_id_size = zmq_msg_size (&msg);
    const size_t copy_size =
      routing_id_size > sizeof (event_->routing_id.data)
        ? sizeof (event_->routing_id.data)
        : routing_id_size;
    event_->routing_id.size = static_cast<uint8_t> (copy_size);
    if (copy_size > 0) {
        memcpy (event_->routing_id.data, zmq_msg_data (&msg), copy_size);
    }
    zmq_msg_close (&msg);

    zmq_msg_init (&msg);
    rc = zmq_msg_recv (&msg, monitor_socket_, follow_flags);
    if (rc == -1) {
        zmq_msg_close (&msg);
        return -1;
    }
    const size_t local_size = zmq_msg_size (&msg);
    const size_t local_copy =
      local_size >= sizeof (event_->local_addr)
        ? sizeof (event_->local_addr) - 1
        : local_size;
    if (local_copy > 0)
        memcpy (event_->local_addr, zmq_msg_data (&msg), local_copy);
    event_->local_addr[local_copy] = '\0';
    zmq_msg_close (&msg);

    zmq_msg_init (&msg);
    rc = zmq_msg_recv (&msg, monitor_socket_, follow_flags);
    if (rc == -1) {
        zmq_msg_close (&msg);
        return -1;
    }
    const size_t remote_size = zmq_msg_size (&msg);
    const size_t remote_copy =
      remote_size >= sizeof (event_->remote_addr)
        ? sizeof (event_->remote_addr) - 1
        : remote_size;
    if (remote_copy > 0)
        memcpy (event_->remote_addr, zmq_msg_data (&msg), remote_copy);
    event_->remote_addr[remote_copy] = '\0';
    zmq_msg_close (&msg);

    return 0;
}

int zmq_socket_stats (void *socket_, zmq_socket_stats_t *stats_)
{
    socket_handle_t handle = as_socket_handle (socket_);
    if (!handle.socket)
        return -1;
    if (handle.threadsafe)
        return handle.threadsafe->socket_stats (stats_);
    return handle.socket->socket_stats (stats_);
}

int zmq_socket_peer_info (void *socket_,
                          const zmq_routing_id_t *routing_id_,
                          zmq_peer_info_t *info_)
{
    socket_handle_t handle = as_socket_handle (socket_);
    if (!handle.socket)
        return -1;
    if (handle.threadsafe)
        return handle.threadsafe->socket_peer_info (routing_id_, info_);
    return handle.socket->socket_peer_info (routing_id_, info_);
}

int zmq_socket_peer_routing_id (void *socket_,
                                int index_,
                                zmq_routing_id_t *out_)
{
    socket_handle_t handle = as_socket_handle (socket_);
    if (!handle.socket)
        return -1;
    if (handle.threadsafe)
        return handle.threadsafe->socket_peer_routing_id (index_, out_);
    return handle.socket->socket_peer_routing_id (index_, out_);
}

int zmq_socket_peer_count (void *socket_)
{
    socket_handle_t handle = as_socket_handle (socket_);
    if (!handle.socket)
        return -1;
    if (handle.threadsafe)
        return handle.threadsafe->socket_peer_count ();
    return handle.socket->socket_peer_count ();
}

int zmq_socket_peers (void *socket_, zmq_peer_info_t *peers_, size_t *count_)
{
    socket_handle_t handle = as_socket_handle (socket_);
    if (!handle.socket)
        return -1;
    if (handle.threadsafe)
        return handle.threadsafe->socket_peers (peers_, count_);
    return handle.socket->socket_peers (peers_, count_);
}

int zmq_bind (void *s_, const char *addr_)
{
    socket_handle_t handle = as_socket_handle (s_);
    if (!handle.socket)
        return -1;
    if (handle.threadsafe)
        return handle.threadsafe->bind (addr_);
    return handle.socket->bind (addr_);
}

int zmq_connect (void *s_, const char *addr_)
{
    socket_handle_t handle = as_socket_handle (s_);
    if (!handle.socket)
        return -1;
    if (handle.threadsafe)
        return handle.threadsafe->connect (addr_);
    return handle.socket->connect (addr_);
}

int zmq_unbind (void *s_, const char *addr_)
{
    socket_handle_t handle = as_socket_handle (s_);
    if (!handle.socket)
        return -1;
    if (handle.threadsafe)
        return handle.threadsafe->term_endpoint (addr_);
    return handle.socket->term_endpoint (addr_);
}

int zmq_disconnect (void *s_, const char *addr_)
{
    socket_handle_t handle = as_socket_handle (s_);
    if (!handle.socket)
        return -1;
    if (handle.threadsafe)
        return handle.threadsafe->term_endpoint (addr_);
    return handle.socket->term_endpoint (addr_);
}

// Sending functions.

static inline int
s_sendmsg (socket_handle_t handle_, zmq_msg_t *msg_, int flags_)
{
    size_t sz = zmq_msg_size (msg_);
    int rc = 0;
    if (unlikely (handle_.threadsafe != NULL))
        rc = handle_.threadsafe->send (reinterpret_cast<zmq::msg_t *> (msg_),
                                       flags_);
    else
        rc = handle_.socket->send (reinterpret_cast<zmq::msg_t *> (msg_),
                                   flags_);
    if (unlikely (rc < 0))
        return -1;

    size_t max_msgsz = INT_MAX;
    return static_cast<int> (sz < max_msgsz ? sz : max_msgsz);
}

int zmq_send (void *s_, const void *buf_, size_t len_, int flags_)
{
    socket_handle_t handle = as_socket_handle (s_);
    if (!handle.socket)
        return -1;
    zmq_msg_t msg;
    int rc = zmq_msg_init_buffer (&msg, buf_, len_);
    if (unlikely (rc < 0))
        return -1;

    rc = s_sendmsg (handle, &msg, flags_);
    if (unlikely (rc < 0)) {
        const int err = errno;
        zmq_msg_close (&msg);
        errno = err;
        return -1;
    }
    return rc;
}

int zmq_send_const (void *s_, const void *buf_, size_t len_, int flags_)
{
    socket_handle_t handle = as_socket_handle (s_);
    if (!handle.socket)
        return -1;
    zmq_msg_t msg;
    int rc =
      zmq_msg_init_data (&msg, const_cast<void *> (buf_), len_, NULL, NULL);
    if (rc != 0)
        return -1;

    rc = s_sendmsg (handle, &msg, flags_);
    if (unlikely (rc < 0)) {
        const int err = errno;
        zmq_msg_close (&msg);
        errno = err;
        return -1;
    }
    return rc;
}

// Receiving functions.

static int s_recvmsg (socket_handle_t handle_, zmq_msg_t *msg_, int flags_)
{
    int rc = 0;
    if (unlikely (handle_.threadsafe != NULL))
        rc = handle_.threadsafe->recv (reinterpret_cast<zmq::msg_t *> (msg_),
                                       flags_);
    else
        rc = handle_.socket->recv (reinterpret_cast<zmq::msg_t *> (msg_),
                                   flags_);
    if (unlikely (rc < 0))
        return -1;

    const size_t sz = zmq_msg_size (msg_);
    return static_cast<int> (sz < INT_MAX ? sz : INT_MAX);
}

int zmq_recv (void *s_, void *buf_, size_t len_, int flags_)
{
    socket_handle_t handle = as_socket_handle (s_);
    if (!handle.socket)
        return -1;
    zmq_msg_t msg;
    int rc = zmq_msg_init (&msg);
    errno_assert (rc == 0);

    const int nbytes = s_recvmsg (handle, &msg, flags_);
    if (unlikely (nbytes < 0)) {
        const int err = errno;
        zmq_msg_close (&msg);
        errno = err;
        return -1;
    }

    const size_t to_copy = size_t (nbytes) < len_ ? size_t (nbytes) : len_;
    if (to_copy) {
        memcpy (buf_, zmq_msg_data (&msg), to_copy);
    }
    zmq_msg_close (&msg);

    return nbytes;
}

// Message manipulators.

int zmq_msg_init (zmq_msg_t *msg_)
{
    return (reinterpret_cast<zmq::msg_t *> (msg_))->init ();
}

int zmq_msg_init_size (zmq_msg_t *msg_, size_t size_)
{
    return (reinterpret_cast<zmq::msg_t *> (msg_))->init_size (size_);
}

int zmq_msg_init_buffer (zmq_msg_t *msg_, const void *buf_, size_t size_)
{
    return (reinterpret_cast<zmq::msg_t *> (msg_))->init_buffer (buf_, size_);
}

int zmq_msg_init_data (
  zmq_msg_t *msg_, void *data_, size_t size_, zmq_free_fn *ffn_, void *hint_)
{
    return (reinterpret_cast<zmq::msg_t *> (msg_))
      ->init_data (data_, size_, ffn_, hint_);
}

int zmq_msg_send (zmq_msg_t *msg_, void *s_, int flags_)
{
    socket_handle_t handle = as_socket_handle (s_);
    if (!handle.socket)
        return -1;
    return s_sendmsg (handle, msg_, flags_);
}

int zmq_msg_recv (zmq_msg_t *msg_, void *s_, int flags_)
{
    socket_handle_t handle = as_socket_handle (s_);
    if (!handle.socket)
        return -1;
    return s_recvmsg (handle, msg_, flags_);
}

int zmq_msg_close (zmq_msg_t *msg_)
{
    return (reinterpret_cast<zmq::msg_t *> (msg_))->close ();
}

int zmq_msg_move (zmq_msg_t *dest_, zmq_msg_t *src_)
{
    return (reinterpret_cast<zmq::msg_t *> (dest_))
      ->move (*reinterpret_cast<zmq::msg_t *> (src_));
}

int zmq_msg_copy (zmq_msg_t *dest_, zmq_msg_t *src_)
{
    return (reinterpret_cast<zmq::msg_t *> (dest_))
      ->copy (*reinterpret_cast<zmq::msg_t *> (src_));
}

void *zmq_msg_data (zmq_msg_t *msg_)
{
    return (reinterpret_cast<zmq::msg_t *> (msg_))->data ();
}

size_t zmq_msg_size (const zmq_msg_t *msg_)
{
    return ((zmq::msg_t *) msg_)->size ();
}

int zmq_msg_more (const zmq_msg_t *msg_)
{
    return (((zmq::msg_t *) msg_)->flags () & zmq::msg_t::more) ? 1 : 0;
}

int zmq_msg_get (const zmq_msg_t *msg_, int property_)
{
    switch (property_) {
        case ZMQ_MORE:
            return (((zmq::msg_t *) msg_)->flags () & zmq::msg_t::more) ? 1 : 0;
        case ZMQ_SHARED:
            return (((zmq::msg_t *) msg_)->is_cmsg ())
                       || (((zmq::msg_t *) msg_)->flags () & zmq::msg_t::shared)
                     ? 1
                     : 0;
        default:
            errno = EINVAL;
            return -1;
    }
}

int zmq_msg_set (zmq_msg_t *, int, int)
{
    errno = EINVAL;
    return -1;
}

const char *zmq_msg_gets (const zmq_msg_t *msg_, const char *property_)
{
    const zmq::metadata_t *metadata =
      reinterpret_cast<const zmq::msg_t *> (msg_)->metadata ();
    const char *value = NULL;
    if (metadata)
        value = metadata->get (std::string (property_));
    if (value)
        return value;

    errno = EINVAL;
    return NULL;
}

// Polling.

int zmq_poll (zmq_pollitem_t *items_, int nitems_, long timeout_)
{
    if (unlikely (nitems_ < 0)) {
        errno = EINVAL;
        return -1;
    }
    if (unlikely (nitems_ == 0)) {
        if (timeout_ == 0)
            return 0;
#if defined ZMQ_HAVE_WINDOWS
        Sleep (timeout_ > 0 ? timeout_ : INFINITE);
        return 0;
#else
        return usleep (timeout_ * 1000);
#endif
    }
    if (!items_) {
        errno = EFAULT;
        return -1;
    }

    zmq::fast_vector_t<pollfd, ZMQ_POLLITEMS_DFLT> pollfds (nitems_);
    bool socket_fd_unavailable = false;

    for (int i = 0; i != nitems_; i++) {
        if (items_[i].socket) {
            size_t zmq_fd_size = sizeof (zmq::fd_t);
            if (zmq_getsockopt (items_[i].socket, ZMQ_FD, &pollfds[i].fd,
                                &zmq_fd_size)
                == -1) {
                if (errno == EINVAL) {
                    socket_fd_unavailable = true;
                    pollfds[i].fd = -1;
                    pollfds[i].events = 0;
                    continue;
                }
                return -1;
            }
            pollfds[i].events = items_[i].events ? POLLIN : 0;
        }
        else {
            pollfds[i].fd = items_[i].fd;
            pollfds[i].events =
              (items_[i].events & ZMQ_POLLIN ? POLLIN : 0)
              | (items_[i].events & ZMQ_POLLOUT ? POLLOUT : 0)
              | (items_[i].events & ZMQ_POLLPRI ? POLLPRI : 0);
        }
    }

    zmq::clock_t clock;
    uint64_t now = 0;
    uint64_t end = 0;
    bool first_pass = true;
    int nevents = 0;

    while (true) {
        const zmq::timeout_t timeout =
          zmq::compute_timeout (first_pass, timeout_, now, end);

        int poll_timeout = timeout;
        if (socket_fd_unavailable) {
            static const int max_poll_timeout_ms = 10;
            if (timeout_ == 0) {
                poll_timeout = 0;
            } else if (timeout_ < 0) {
                poll_timeout = max_poll_timeout_ms;
            } else {
                poll_timeout = timeout > max_poll_timeout_ms
                                 ? max_poll_timeout_ms
                                 : timeout;
            }
        }

        {
            const int rc = poll (&pollfds[0], nitems_, poll_timeout);
            if (rc == -1 && errno == EINTR) {
                return -1;
            }
            errno_assert (rc >= 0);
        }

        for (int i = 0; i != nitems_; i++) {
            items_[i].revents = 0;
            if (items_[i].socket) {
                size_t zmq_events_size = sizeof (uint32_t);
                uint32_t zmq_events;
                if (zmq_getsockopt (items_[i].socket, ZMQ_EVENTS, &zmq_events,
                                    &zmq_events_size)
                    == -1) {
                    return -1;
                }
                if ((items_[i].events & ZMQ_POLLOUT)
                    && (zmq_events & ZMQ_POLLOUT))
                    items_[i].revents |= ZMQ_POLLOUT;
                if ((items_[i].events & ZMQ_POLLIN)
                    && (zmq_events & ZMQ_POLLIN))
                    items_[i].revents |= ZMQ_POLLIN;
            }
            else {
                if (pollfds[i].revents & POLLIN)
                    items_[i].revents |= ZMQ_POLLIN;
                if (pollfds[i].revents & POLLOUT)
                    items_[i].revents |= ZMQ_POLLOUT;
                if (pollfds[i].revents & POLLPRI)
                    items_[i].revents |= ZMQ_POLLPRI;
                if (pollfds[i].revents & ~(POLLIN | POLLOUT | POLLPRI))
                    items_[i].revents |= ZMQ_POLLERR;
            }

            if (items_[i].revents)
                nevents++;
        }

        if (timeout_ == 0 || nevents)
            break;

        if (timeout_ < 0) {
            first_pass = false;
            continue;
        }

        if (first_pass) {
            now = clock.now_ms ();
            end = now + timeout_;
            if (now == end)
                break;
            first_pass = false;
            continue;
        }

        now = clock.now_ms ();
        if (now >= end)
            break;
    }

    return nevents;
}

int zmq_proxy (void *frontend_, void *backend_, void *capture_)
{
    if (!frontend_ || !backend_) {
        errno = EFAULT;
        return -1;
    }

    socket_handle_t frontend = as_socket_handle (frontend_);
    if (!frontend.socket)
        return -1;
    socket_handle_t backend = as_socket_handle (backend_);
    if (!backend.socket)
        return -1;

    zmq::socket_base_t *capture_socket = NULL;
    if (capture_) {
        socket_handle_t capture = as_socket_handle (capture_);
        if (!capture.socket)
            return -1;
        capture_socket = capture.socket;
    }

    return zmq::proxy (frontend.socket, backend.socket, capture_socket);
}

int zmq_proxy_steerable (void *frontend_,
                         void *backend_,
                         void *capture_,
                         void *control_)
{
    if (!frontend_ || !backend_) {
        errno = EFAULT;
        return -1;
    }

    socket_handle_t frontend = as_socket_handle (frontend_);
    if (!frontend.socket)
        return -1;
    socket_handle_t backend = as_socket_handle (backend_);
    if (!backend.socket)
        return -1;

    zmq::socket_base_t *capture_socket = NULL;
    if (capture_) {
        socket_handle_t capture = as_socket_handle (capture_);
        if (!capture.socket)
            return -1;
        capture_socket = capture.socket;
    }

    zmq::socket_base_t *control_socket = NULL;
    if (control_) {
        socket_handle_t control = as_socket_handle (control_);
        if (!control.socket)
            return -1;
        control_socket = control.socket;
    }

    return zmq::proxy_steerable (frontend.socket, backend.socket,
                                 capture_socket, control_socket);
}

int zmq_has (const char *capability_)
{
#if defined(ZMQ_HAVE_IPC)
    if (strcmp (capability_, zmq::protocol_name::ipc) == 0)
        return true;
#endif
#if defined(ZMQ_HAVE_TLS)
    if (strcmp (capability_, "tls") == 0)
        return true;
#endif
#if defined(ZMQ_HAVE_WS)
    if (strcmp (capability_, "ws") == 0)
        return true;
#endif
#if defined(ZMQ_HAVE_WSS)
    if (strcmp (capability_, "wss") == 0)
        return true;
#endif
    return false;
}
