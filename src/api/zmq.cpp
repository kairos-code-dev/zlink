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
#include "discovery/registry.hpp"
#include "discovery/discovery.hpp"
#include "discovery/gateway.hpp"
#include "discovery/provider.hpp"
#include "spot/spot_node.hpp"
#include "spot/spot.hpp"
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

int zmq_socket_stats_ex (void *socket_, zmq_socket_stats_ex_t *stats_)
{
    socket_handle_t handle = as_socket_handle (socket_);
    if (!handle.socket)
        return -1;
    if (handle.threadsafe)
        return handle.threadsafe->socket_stats_ex (stats_);
    return handle.socket->socket_stats_ex (stats_);
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

uint64_t zmq_request (void *socket_,
                      const zmq_routing_id_t *routing_id_,
                      zmq_msg_t *parts_,
                      size_t part_count_,
                      zmq_request_cb_fn callback_,
                      int timeout_ms_)
{
    socket_handle_t handle = as_socket_handle (socket_);
    if (!handle.socket)
        return 0;
    if (!handle.threadsafe) {
        errno = ENOTSUP;
        return 0;
    }
    return handle.threadsafe->request (routing_id_, parts_, part_count_,
                                       callback_, timeout_ms_);
}

uint64_t zmq_group_request (void *socket_,
                            const zmq_routing_id_t *routing_id_,
                            uint64_t group_id_,
                            zmq_msg_t *parts_,
                            size_t part_count_,
                            zmq_request_cb_fn callback_,
                            int timeout_ms_)
{
    socket_handle_t handle = as_socket_handle (socket_);
    if (!handle.socket)
        return 0;
    if (!handle.threadsafe) {
        errno = ENOTSUP;
        return 0;
    }
    return handle.threadsafe->group_request (routing_id_, group_id_, parts_,
                                             part_count_, callback_,
                                             timeout_ms_);
}

int zmq_on_request (void *socket_, zmq_server_cb_fn handler_)
{
    socket_handle_t handle = as_socket_handle (socket_);
    if (!handle.socket)
        return -1;
    if (!handle.threadsafe) {
        errno = ENOTSUP;
        return -1;
    }
    return handle.threadsafe->on_request (handler_);
}

int zmq_reply (void *socket_,
               const zmq_routing_id_t *routing_id_,
               uint64_t request_id_,
               zmq_msg_t *parts_,
               size_t part_count_)
{
    socket_handle_t handle = as_socket_handle (socket_);
    if (!handle.socket)
        return -1;
    if (!handle.threadsafe) {
        errno = ENOTSUP;
        return -1;
    }
    return handle.threadsafe->reply (routing_id_, request_id_, parts_,
                                     part_count_);
}

int zmq_reply_simple (void *socket_, zmq_msg_t *parts_, size_t part_count_)
{
    socket_handle_t handle = as_socket_handle (socket_);
    if (!handle.socket)
        return -1;
    if (!handle.threadsafe) {
        errno = ENOTSUP;
        return -1;
    }
    return handle.threadsafe->reply_simple (parts_, part_count_);
}

void zmq_msgv_close (zmq_msg_t *parts_, size_t part_count_)
{
    if (!parts_)
        return;
    for (size_t i = 0; i < part_count_; ++i)
        zmq_msg_close (&parts_[i]);
    free (parts_);
}

uint64_t zmq_request_send (void *socket_,
                           const zmq_routing_id_t *routing_id_,
                           zmq_msg_t *parts_,
                           size_t part_count_)
{
    socket_handle_t handle = as_socket_handle (socket_);
    if (!handle.socket)
        return 0;
    if (!handle.threadsafe) {
        errno = ENOTSUP;
        return 0;
    }
    return handle.threadsafe->request_send (routing_id_, parts_, part_count_);
}

int zmq_request_recv (void *socket_,
                      zmq_completion_t *completion_,
                      int timeout_ms_)
{
    socket_handle_t handle = as_socket_handle (socket_);
    if (!handle.socket)
        return -1;
    if (!handle.threadsafe) {
        errno = ENOTSUP;
        return -1;
    }
    return handle.threadsafe->request_recv (completion_, timeout_ms_);
}

int zmq_pending_requests (void *socket_)
{
    socket_handle_t handle = as_socket_handle (socket_);
    if (!handle.socket)
        return -1;
    if (!handle.threadsafe) {
        errno = ENOTSUP;
        return -1;
    }
    return handle.threadsafe->pending_requests ();
}

int zmq_cancel_all_requests (void *socket_)
{
    socket_handle_t handle = as_socket_handle (socket_);
    if (!handle.socket)
        return -1;
    if (!handle.threadsafe) {
        errno = ENOTSUP;
        return -1;
    }
    return handle.threadsafe->cancel_all_requests ();
}

// Service Discovery API

void *zmq_registry_new (void *ctx_)
{
    if (!ctx_ || !(static_cast<zmq::ctx_t *> (ctx_))->check_tag ()) {
        errno = EFAULT;
        return NULL;
    }
    zmq::registry_t *registry =
      new (std::nothrow) zmq::registry_t (static_cast<zmq::ctx_t *> (ctx_));
    if (!registry) {
        errno = ENOMEM;
        return NULL;
    }
    return static_cast<void *> (registry);
}

int zmq_registry_set_endpoints (void *registry_,
                                const char *pub_endpoint_,
                                const char *router_endpoint_)
{
    if (!registry_)
        return -1;
    zmq::registry_t *registry = static_cast<zmq::registry_t *> (registry_);
    if (!registry->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    return registry->set_endpoints (pub_endpoint_, router_endpoint_);
}

int zmq_registry_set_id (void *registry_, uint32_t registry_id_)
{
    if (!registry_)
        return -1;
    zmq::registry_t *registry = static_cast<zmq::registry_t *> (registry_);
    if (!registry->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    return registry->set_id (registry_id_);
}

int zmq_registry_add_peer (void *registry_, const char *peer_pub_endpoint_)
{
    if (!registry_)
        return -1;
    zmq::registry_t *registry = static_cast<zmq::registry_t *> (registry_);
    if (!registry->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    return registry->add_peer (peer_pub_endpoint_);
}

int zmq_registry_set_heartbeat (void *registry_,
                                uint32_t interval_ms_,
                                uint32_t timeout_ms_)
{
    if (!registry_)
        return -1;
    zmq::registry_t *registry = static_cast<zmq::registry_t *> (registry_);
    if (!registry->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    return registry->set_heartbeat (interval_ms_, timeout_ms_);
}

int zmq_registry_set_broadcast_interval (void *registry_,
                                         uint32_t interval_ms_)
{
    if (!registry_)
        return -1;
    zmq::registry_t *registry = static_cast<zmq::registry_t *> (registry_);
    if (!registry->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    return registry->set_broadcast_interval (interval_ms_);
}

int zmq_registry_start (void *registry_)
{
    if (!registry_)
        return -1;
    zmq::registry_t *registry = static_cast<zmq::registry_t *> (registry_);
    if (!registry->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    return registry->start ();
}

int zmq_registry_destroy (void **registry_p_)
{
    if (!registry_p_ || !*registry_p_) {
        errno = EFAULT;
        return -1;
    }
    zmq::registry_t *registry = static_cast<zmq::registry_t *> (*registry_p_);
    *registry_p_ = NULL;
    if (!registry->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    registry->destroy ();
    delete registry;
    return 0;
}

void *zmq_discovery_new (void *ctx_)
{
    if (!ctx_ || !(static_cast<zmq::ctx_t *> (ctx_))->check_tag ()) {
        errno = EFAULT;
        return NULL;
    }
    zmq::discovery_t *discovery =
      new (std::nothrow) zmq::discovery_t (static_cast<zmq::ctx_t *> (ctx_));
    if (!discovery) {
        errno = ENOMEM;
        return NULL;
    }
    return static_cast<void *> (discovery);
}

int zmq_discovery_connect_registry (void *discovery_,
                                    const char *registry_pub_endpoint_)
{
    if (!discovery_)
        return -1;
    zmq::discovery_t *discovery = static_cast<zmq::discovery_t *> (discovery_);
    if (!discovery->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    return discovery->connect_registry (registry_pub_endpoint_);
}

int zmq_discovery_subscribe (void *discovery_, const char *service_name_)
{
    if (!discovery_)
        return -1;
    zmq::discovery_t *discovery = static_cast<zmq::discovery_t *> (discovery_);
    if (!discovery->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    return discovery->subscribe (service_name_);
}

int zmq_discovery_unsubscribe (void *discovery_, const char *service_name_)
{
    if (!discovery_)
        return -1;
    zmq::discovery_t *discovery = static_cast<zmq::discovery_t *> (discovery_);
    if (!discovery->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    return discovery->unsubscribe (service_name_);
}

int zmq_discovery_get_providers (void *discovery_,
                                 const char *service_name_,
                                 zmq_provider_info_t *providers_,
                                 size_t *count_)
{
    if (!discovery_)
        return -1;
    zmq::discovery_t *discovery = static_cast<zmq::discovery_t *> (discovery_);
    if (!discovery->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    return discovery->get_providers (service_name_, providers_, count_);
}

int zmq_discovery_provider_count (void *discovery_,
                                  const char *service_name_)
{
    if (!discovery_)
        return -1;
    zmq::discovery_t *discovery = static_cast<zmq::discovery_t *> (discovery_);
    if (!discovery->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    return discovery->provider_count (service_name_);
}

int zmq_discovery_service_available (void *discovery_,
                                     const char *service_name_)
{
    if (!discovery_)
        return -1;
    zmq::discovery_t *discovery = static_cast<zmq::discovery_t *> (discovery_);
    if (!discovery->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    return discovery->service_available (service_name_);
}

int zmq_discovery_destroy (void **discovery_p_)
{
    if (!discovery_p_ || !*discovery_p_) {
        errno = EFAULT;
        return -1;
    }
    zmq::discovery_t *discovery = static_cast<zmq::discovery_t *> (*discovery_p_);
    *discovery_p_ = NULL;
    if (!discovery->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    discovery->destroy ();
    delete discovery;
    return 0;
}

void *zmq_gateway_new (void *ctx_, void *discovery_)
{
    if (!ctx_ || !(static_cast<zmq::ctx_t *> (ctx_))->check_tag ()) {
        errno = EFAULT;
        return NULL;
    }
    if (!discovery_) {
        errno = EINVAL;
        return NULL;
    }
    zmq::discovery_t *disc = static_cast<zmq::discovery_t *> (discovery_);
    if (!disc->check_tag ()) {
        errno = EFAULT;
        return NULL;
    }
    zmq::gateway_t *gateway =
      new (std::nothrow) zmq::gateway_t (static_cast<zmq::ctx_t *> (ctx_), disc);
    if (!gateway) {
        errno = ENOMEM;
        return NULL;
    }
    return static_cast<void *> (gateway);
}

int zmq_gateway_send (void *gateway_,
                      const char *service_name_,
                      zmq_msg_t *parts_,
                      size_t part_count_,
                      int flags_,
                      uint64_t *request_id_out_)
{
    if (!gateway_)
        return -1;
    zmq::gateway_t *gateway = static_cast<zmq::gateway_t *> (gateway_);
    if (!gateway->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    return gateway->send (service_name_, parts_, part_count_, flags_,
                          request_id_out_);
}

int zmq_gateway_recv (void *gateway_,
                      zmq_msg_t **parts_,
                      size_t *part_count_,
                      int flags_,
                      char *service_name_out_,
                      uint64_t *request_id_out_)
{
    if (!gateway_)
        return -1;
    zmq::gateway_t *gateway = static_cast<zmq::gateway_t *> (gateway_);
    if (!gateway->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    return gateway->recv (parts_, part_count_, flags_, service_name_out_,
                          request_id_out_);
}

void *zmq_gateway_threadsafe_router (void *gateway_, const char *service_name_)
{
    if (!gateway_)
        return NULL;
    zmq::gateway_t *gateway = static_cast<zmq::gateway_t *> (gateway_);
    if (!gateway->check_tag ()) {
        errno = EFAULT;
        return NULL;
    }
    return gateway->threadsafe_router (service_name_);
}

uint64_t zmq_gateway_request (void *gateway_,
                              const char *service_name_,
                              zmq_msg_t *parts_,
                              size_t part_count_,
                              zmq_gateway_request_cb_fn callback_,
                              int timeout_ms_)
{
    if (!gateway_)
        return 0;
    zmq::gateway_t *gateway = static_cast<zmq::gateway_t *> (gateway_);
    if (!gateway->check_tag ()) {
        errno = EFAULT;
        return 0;
    }
    return gateway->request (service_name_, parts_, part_count_, callback_,
                             timeout_ms_);
}

uint64_t zmq_gateway_request_send (void *gateway_,
                                   const char *service_name_,
                                   zmq_msg_t *parts_,
                                   size_t part_count_,
                                   int flags_)
{
    if (!gateway_)
        return 0;
    zmq::gateway_t *gateway = static_cast<zmq::gateway_t *> (gateway_);
    if (!gateway->check_tag ()) {
        errno = EFAULT;
        return 0;
    }
    return gateway->request_send (service_name_, parts_, part_count_, flags_);
}

int zmq_gateway_request_recv (void *gateway_,
                              zmq_gateway_completion_t *completion_,
                              int timeout_ms_)
{
    if (!gateway_)
        return -1;
    zmq::gateway_t *gateway = static_cast<zmq::gateway_t *> (gateway_);
    if (!gateway->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    return gateway->request_recv (completion_, timeout_ms_);
}

int zmq_gateway_set_lb_strategy (void *gateway_,
                                 const char *service_name_,
                                 int strategy_)
{
    if (!gateway_)
        return -1;
    zmq::gateway_t *gateway = static_cast<zmq::gateway_t *> (gateway_);
    if (!gateway->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    return gateway->set_lb_strategy (service_name_, strategy_);
}

int zmq_gateway_connection_count (void *gateway_, const char *service_name_)
{
    if (!gateway_)
        return -1;
    zmq::gateway_t *gateway = static_cast<zmq::gateway_t *> (gateway_);
    if (!gateway->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    return gateway->connection_count (service_name_);
}

int zmq_gateway_destroy (void **gateway_p_)
{
    if (!gateway_p_ || !*gateway_p_) {
        errno = EFAULT;
        return -1;
    }
    zmq::gateway_t *gateway = static_cast<zmq::gateway_t *> (*gateway_p_);
    *gateway_p_ = NULL;
    if (!gateway->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    gateway->destroy ();
    delete gateway;
    return 0;
}

void *zmq_provider_new (void *ctx_)
{
    if (!ctx_ || !(static_cast<zmq::ctx_t *> (ctx_))->check_tag ()) {
        errno = EFAULT;
        return NULL;
    }
    zmq::provider_t *provider =
      new (std::nothrow) zmq::provider_t (static_cast<zmq::ctx_t *> (ctx_));
    if (!provider) {
        errno = ENOMEM;
        return NULL;
    }
    return static_cast<void *> (provider);
}

int zmq_provider_bind (void *provider_, const char *bind_endpoint_)
{
    if (!provider_)
        return -1;
    zmq::provider_t *provider = static_cast<zmq::provider_t *> (provider_);
    if (!provider->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    return provider->bind (bind_endpoint_);
}

int zmq_provider_connect_registry (void *provider_,
                                   const char *registry_endpoint_)
{
    if (!provider_)
        return -1;
    zmq::provider_t *provider = static_cast<zmq::provider_t *> (provider_);
    if (!provider->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    return provider->connect_registry (registry_endpoint_);
}

int zmq_provider_register (void *provider_,
                           const char *service_name_,
                           const char *advertise_endpoint_,
                           uint32_t weight_)
{
    if (!provider_)
        return -1;
    zmq::provider_t *provider = static_cast<zmq::provider_t *> (provider_);
    if (!provider->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    return provider->register_service (service_name_, advertise_endpoint_,
                                       weight_);
}

int zmq_provider_update_weight (void *provider_,
                                const char *service_name_,
                                uint32_t weight_)
{
    if (!provider_)
        return -1;
    zmq::provider_t *provider = static_cast<zmq::provider_t *> (provider_);
    if (!provider->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    return provider->update_weight (service_name_, weight_);
}

int zmq_provider_unregister (void *provider_, const char *service_name_)
{
    if (!provider_)
        return -1;
    zmq::provider_t *provider = static_cast<zmq::provider_t *> (provider_);
    if (!provider->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    return provider->unregister_service (service_name_);
}

int zmq_provider_register_result (void *provider_,
                                  const char *service_name_,
                                  int *status_,
                                  char *resolved_endpoint_,
                                  char *error_message_)
{
    if (!provider_)
        return -1;
    zmq::provider_t *provider = static_cast<zmq::provider_t *> (provider_);
    if (!provider->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    return provider->register_result (service_name_, status_, resolved_endpoint_,
                                      error_message_);
}

void *zmq_provider_threadsafe_router (void *provider_)
{
    if (!provider_)
        return NULL;
    zmq::provider_t *provider = static_cast<zmq::provider_t *> (provider_);
    if (!provider->check_tag ()) {
        errno = EFAULT;
        return NULL;
    }
    return provider->threadsafe_router ();
}

int zmq_provider_destroy (void **provider_p_)
{
    if (!provider_p_ || !*provider_p_) {
        errno = EFAULT;
        return -1;
    }
    zmq::provider_t *provider = static_cast<zmq::provider_t *> (*provider_p_);
    *provider_p_ = NULL;
    if (!provider->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    provider->destroy ();
    delete provider;
    return 0;
}

void *zmq_spot_node_new (void *ctx_)
{
    if (!ctx_ || !(static_cast<zmq::ctx_t *> (ctx_))->check_tag ()) {
        errno = EFAULT;
        return NULL;
    }
    zmq::spot_node_t *node =
      new (std::nothrow) zmq::spot_node_t (static_cast<zmq::ctx_t *> (ctx_));
    if (!node) {
        errno = ENOMEM;
        return NULL;
    }
    return static_cast<void *> (node);
}

int zmq_spot_node_destroy (void **node_p_)
{
    if (!node_p_ || !*node_p_) {
        errno = EFAULT;
        return -1;
    }
    zmq::spot_node_t *node = static_cast<zmq::spot_node_t *> (*node_p_);
    *node_p_ = NULL;
    if (!node->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    node->destroy ();
    delete node;
    return 0;
}

int zmq_spot_node_bind (void *node_, const char *endpoint_)
{
    if (!node_)
        return -1;
    zmq::spot_node_t *node = static_cast<zmq::spot_node_t *> (node_);
    if (!node->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    return node->bind (endpoint_);
}

int zmq_spot_node_connect_registry (void *node_,
                                    const char *registry_endpoint_)
{
    if (!node_)
        return -1;
    zmq::spot_node_t *node = static_cast<zmq::spot_node_t *> (node_);
    if (!node->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    return node->connect_registry (registry_endpoint_);
}

int zmq_spot_node_connect_peer_pub (void *node_,
                                    const char *peer_pub_endpoint_)
{
    if (!node_)
        return -1;
    zmq::spot_node_t *node = static_cast<zmq::spot_node_t *> (node_);
    if (!node->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    return node->connect_peer_pub (peer_pub_endpoint_);
}

int zmq_spot_node_disconnect_peer_pub (void *node_,
                                       const char *peer_pub_endpoint_)
{
    if (!node_)
        return -1;
    zmq::spot_node_t *node = static_cast<zmq::spot_node_t *> (node_);
    if (!node->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    return node->disconnect_peer_pub (peer_pub_endpoint_);
}

int zmq_spot_node_register (void *node_,
                            const char *service_name_,
                            const char *advertise_endpoint_)
{
    if (!node_)
        return -1;
    zmq::spot_node_t *node = static_cast<zmq::spot_node_t *> (node_);
    if (!node->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    return node->register_node (service_name_, advertise_endpoint_);
}

int zmq_spot_node_unregister (void *node_, const char *service_name_)
{
    if (!node_)
        return -1;
    zmq::spot_node_t *node = static_cast<zmq::spot_node_t *> (node_);
    if (!node->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    return node->unregister_node (service_name_);
}

int zmq_spot_node_set_discovery (void *node_,
                                 void *discovery_,
                                 const char *service_name_)
{
    if (!node_ || !discovery_)
        return -1;
    zmq::spot_node_t *node = static_cast<zmq::spot_node_t *> (node_);
    if (!node->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    zmq::discovery_t *disc = static_cast<zmq::discovery_t *> (discovery_);
    if (!disc->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    return node->set_discovery (disc, service_name_);
}

void *zmq_spot_new (void *node_)
{
    if (!node_)
        return NULL;
    zmq::spot_node_t *node = static_cast<zmq::spot_node_t *> (node_);
    if (!node->check_tag ()) {
        errno = EFAULT;
        return NULL;
    }
    zmq::spot_t *spot = node->create_spot (false);
    if (!spot)
        return NULL;
    return static_cast<void *> (spot);
}

void *zmq_spot_new_threadsafe (void *node_)
{
    if (!node_)
        return NULL;
    zmq::spot_node_t *node = static_cast<zmq::spot_node_t *> (node_);
    if (!node->check_tag ()) {
        errno = EFAULT;
        return NULL;
    }
    zmq::spot_t *spot = node->create_spot (true);
    if (!spot)
        return NULL;
    return static_cast<void *> (spot);
}

int zmq_spot_destroy (void **spot_p_)
{
    if (!spot_p_ || !*spot_p_) {
        errno = EFAULT;
        return -1;
    }
    zmq::spot_t *spot = static_cast<zmq::spot_t *> (*spot_p_);
    *spot_p_ = NULL;
    if (!spot->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    spot->destroy ();
    delete spot;
    return 0;
}

int zmq_spot_topic_create (void *spot_, const char *topic_id_, int mode_)
{
    if (!spot_)
        return -1;
    zmq::spot_t *spot = static_cast<zmq::spot_t *> (spot_);
    if (!spot->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    return spot->topic_create (topic_id_, mode_);
}

int zmq_spot_topic_destroy (void *spot_, const char *topic_id_)
{
    if (!spot_)
        return -1;
    zmq::spot_t *spot = static_cast<zmq::spot_t *> (spot_);
    if (!spot->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    return spot->topic_destroy (topic_id_);
}

int zmq_spot_publish (void *spot_,
                      const char *topic_id_,
                      zmq_msg_t *parts_,
                      size_t part_count_,
                      int flags_)
{
    if (!spot_)
        return -1;
    zmq::spot_t *spot = static_cast<zmq::spot_t *> (spot_);
    if (!spot->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    return spot->publish (topic_id_, parts_, part_count_, flags_);
}

int zmq_spot_subscribe (void *spot_, const char *topic_id_)
{
    if (!spot_)
        return -1;
    zmq::spot_t *spot = static_cast<zmq::spot_t *> (spot_);
    if (!spot->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    return spot->subscribe (topic_id_);
}

int zmq_spot_subscribe_pattern (void *spot_, const char *pattern_)
{
    if (!spot_)
        return -1;
    zmq::spot_t *spot = static_cast<zmq::spot_t *> (spot_);
    if (!spot->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    return spot->subscribe_pattern (pattern_);
}

int zmq_spot_unsubscribe (void *spot_, const char *topic_id_or_pattern_)
{
    if (!spot_)
        return -1;
    zmq::spot_t *spot = static_cast<zmq::spot_t *> (spot_);
    if (!spot->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    return spot->unsubscribe (topic_id_or_pattern_);
}

int zmq_spot_recv (void *spot_,
                   zmq_msg_t **parts_,
                   size_t *part_count_,
                   int flags_,
                   char *topic_id_out_,
                   size_t *topic_id_len_)
{
    if (!spot_)
        return -1;
    zmq::spot_t *spot = static_cast<zmq::spot_t *> (spot_);
    if (!spot->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    return spot->recv (parts_, part_count_, flags_, topic_id_out_,
                       topic_id_len_);
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
#if defined(ZMQ_HAVE_OPENPGM)
    if (strcmp (capability_, "pgm") == 0 || strcmp (capability_, "epgm") == 0)
        return true;
#endif
    return false;
}
