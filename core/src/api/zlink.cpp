/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"
#define ZLINK_TYPE_UNSAFE

#include "utils/macros.hpp"
#include "core/poller.hpp"
#include "utils/random.hpp"

#if !defined ZLINK_HAVE_POLLER
#if defined ZLINK_POLL_BASED_ON_POLL && !defined ZLINK_HAVE_WINDOWS
#include <poll.h>
#endif
#include "utils/polling_util.hpp"
#endif

#include <cstdio>

#if !defined ZLINK_HAVE_WINDOWS
#include <unistd.h>
#ifdef ZLINK_HAVE_VXWORKS
#include <strings.h>
#endif
#endif

// XSI vector I/O
#if defined ZLINK_HAVE_UIO
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

#ifdef ZLINK_HAVE_PPOLL
#include "utils/polling_util.hpp"
#include <sys/select.h>
#endif

//  Compile time check whether msg_t fits into zlink_msg_t.
typedef char
  check_msg_t_size[sizeof (zlink::msg_t) == sizeof (zlink_msg_t) ? 1 : -1];

//  Forward declarations for internal draft API functions
int zlink_msg_init_buffer (zlink_msg_t *msg_, const void *buf_, size_t size_);
int zlink_ctx_set_ext (void *ctx_, int option_, const void *optval_, size_t optvallen_);
int zlink_poller_wait_all (void *poller_, zlink_poller_event_t *events_, int n_events_, long timeout_);

void zlink_version (int *major_, int *minor_, int *patch_)
{
    *major_ = ZLINK_VERSION_MAJOR;
    *minor_ = ZLINK_VERSION_MINOR;
    *patch_ = ZLINK_VERSION_PATCH;
}

const char *zlink_strerror (int errnum_)
{
    return zlink::errno_to_string (errnum_);
}

int zlink_errno (void)
{
    return errno;
}

//  New context API

void *zlink_ctx_new (void)
{
    if (!zlink::initialize_network ()) {
        return NULL;
    }

    zlink::ctx_t *ctx = new (std::nothrow) zlink::ctx_t;
    if (ctx) {
        if (!ctx->valid ()) {
            delete ctx;
            return NULL;
        }
    }
    return ctx;
}

int zlink_ctx_term (void *ctx_)
{
    if (!ctx_ || !(static_cast<zlink::ctx_t *> (ctx_))->check_tag ()) {
        errno = EFAULT;
        return -1;
    }

    const int rc = (static_cast<zlink::ctx_t *> (ctx_))->terminate ();
    const int en = errno;

    if (!rc || en != EINTR) {
        zlink::shutdown_network ();
    }

    errno = en;
    return rc;
}

int zlink_ctx_shutdown (void *ctx_)
{
    if (!ctx_ || !(static_cast<zlink::ctx_t *> (ctx_))->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    return (static_cast<zlink::ctx_t *> (ctx_))->shutdown ();
}

int zlink_ctx_set (void *ctx_, int option_, int optval_)
{
    return zlink_ctx_set_ext (ctx_, option_, &optval_, sizeof (int));
}

int zlink_ctx_set_ext (void *ctx_,
                     int option_,
                     const void *optval_,
                     size_t optvallen_)
{
    if (!ctx_ || !(static_cast<zlink::ctx_t *> (ctx_))->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    return (static_cast<zlink::ctx_t *> (ctx_))
      ->set (option_, optval_, optvallen_);
}

int zlink_ctx_get (void *ctx_, int option_)
{
    if (!ctx_ || !(static_cast<zlink::ctx_t *> (ctx_))->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    return (static_cast<zlink::ctx_t *> (ctx_))->get (option_);
}

// Sockets

struct socket_handle_t
{
    zlink::socket_base_t *socket;
};

static inline socket_handle_t as_socket_handle (void *s_)
{
    socket_handle_t handle;
    handle.socket = NULL;

    if (!s_) {
        errno = ENOTSOCK;
        return handle;
    }

    zlink::socket_base_t *s = static_cast<zlink::socket_base_t *> (s_);
    if (!s->check_tag ()) {
        errno = ENOTSOCK;
        return handle;
    }

    handle.socket = s;
    return handle;
}

void *zlink_socket (void *ctx_, int type_)
{
    if (!ctx_ || !(static_cast<zlink::ctx_t *> (ctx_))->check_tag ()) {
        errno = EFAULT;
        return NULL;
    }
    zlink::ctx_t *ctx = static_cast<zlink::ctx_t *> (ctx_);
    zlink::socket_base_t *s = ctx->create_socket (type_);
    return static_cast<void *> (s);
}

int zlink_close (void *s_)
{
    socket_handle_t handle = as_socket_handle (s_);
    if (!handle.socket)
        return -1;
    handle.socket->close ();
    return 0;
}

int zlink_setsockopt (void *s_,
                    int option_,
                    const void *optval_,
                    size_t optvallen_)
{
    socket_handle_t handle = as_socket_handle (s_);
    if (!handle.socket)
        return -1;
    return handle.socket->setsockopt (option_, optval_, optvallen_);
}

int zlink_getsockopt (void *s_, int option_, void *optval_, size_t *optvallen_)
{
    socket_handle_t handle = as_socket_handle (s_);
    if (!handle.socket)
        return -1;
    return handle.socket->getsockopt (option_, optval_, optvallen_);
}

int zlink_socket_monitor (void *s_, const char *addr_, int events_)
{
    socket_handle_t handle = as_socket_handle (s_);
    if (!handle.socket)
        return -1;
    return handle.socket->monitor (addr_, events_, 3, ZLINK_PAIR);
}

void *zlink_socket_monitor_open (void *s_, int events_)
{
    socket_handle_t handle = as_socket_handle (s_);
    if (!handle.socket)
        return NULL;

    char endpoint[128];
    const uint32_t rand_id = zlink::generate_random ();
    snprintf (endpoint, sizeof endpoint, "inproc://monitor-%p-%u",
              static_cast<void *> (s_), rand_id);

    const int monitor_rc =
      handle.socket->monitor (endpoint, events_, 3, ZLINK_PAIR);
    if (monitor_rc != 0)
        return NULL;

    void *monitor_socket = zlink_socket (handle.socket->get_ctx (), ZLINK_PAIR);
    if (!monitor_socket) {
        handle.socket->monitor (NULL, 0, 3, ZLINK_PAIR);
        return NULL;
    }

    if (zlink_connect (monitor_socket, endpoint) != 0) {
        zlink_close (monitor_socket);
        handle.socket->monitor (NULL, 0, 3, ZLINK_PAIR);
        return NULL;
    }

    return monitor_socket;
}

int zlink_monitor_recv (void *monitor_socket_,
                      zlink_monitor_event_t *event_,
                      int flags_)
{
    if (!monitor_socket_ || !event_) {
        errno = EINVAL;
        return -1;
    }

    zlink_msg_t msg;
    zlink_msg_init (&msg);
    int rc = zlink_msg_recv (&msg, monitor_socket_, flags_);
    if (rc == -1) {
        zlink_msg_close (&msg);
        return -1;
    }

    memset (event_, 0, sizeof (*event_));

    if (zlink_msg_size (&msg) < sizeof (uint64_t)) {
        zlink_msg_close (&msg);
        errno = EPROTO;
        return -1;
    }

    memcpy (&event_->event, zlink_msg_data (&msg), sizeof (uint64_t));
    zlink_msg_close (&msg);

    const int follow_flags = flags_ & ~ZLINK_DONTWAIT;

    zlink_msg_init (&msg);
    rc = zlink_msg_recv (&msg, monitor_socket_, follow_flags);
    if (rc == -1) {
        zlink_msg_close (&msg);
        return -1;
    }
    if (zlink_msg_size (&msg) < sizeof (uint64_t)) {
        zlink_msg_close (&msg);
        errno = EPROTO;
        return -1;
    }

    uint64_t value_count = 0;
    memcpy (&value_count, zlink_msg_data (&msg), sizeof (uint64_t));
    zlink_msg_close (&msg);

    for (uint64_t i = 0; i < value_count; ++i) {
        zlink_msg_init (&msg);
        rc = zlink_msg_recv (&msg, monitor_socket_, follow_flags);
        if (rc == -1) {
            zlink_msg_close (&msg);
            return -1;
        }
        if (i == 0 && zlink_msg_size (&msg) >= sizeof (uint64_t)) {
            memcpy (&event_->value, zlink_msg_data (&msg), sizeof (uint64_t));
        }
        zlink_msg_close (&msg);
    }

    zlink_msg_init (&msg);
    rc = zlink_msg_recv (&msg, monitor_socket_, follow_flags);
    if (rc == -1) {
        zlink_msg_close (&msg);
        return -1;
    }
    const size_t routing_id_size = zlink_msg_size (&msg);
    const size_t copy_size =
      routing_id_size > sizeof (event_->routing_id.data)
        ? sizeof (event_->routing_id.data)
        : routing_id_size;
    event_->routing_id.size = static_cast<uint8_t> (copy_size);
    if (copy_size > 0) {
        memcpy (event_->routing_id.data, zlink_msg_data (&msg), copy_size);
    }
    zlink_msg_close (&msg);

    zlink_msg_init (&msg);
    rc = zlink_msg_recv (&msg, monitor_socket_, follow_flags);
    if (rc == -1) {
        zlink_msg_close (&msg);
        return -1;
    }
    const size_t local_size = zlink_msg_size (&msg);
    const size_t local_copy =
      local_size >= sizeof (event_->local_addr)
        ? sizeof (event_->local_addr) - 1
        : local_size;
    if (local_copy > 0)
        memcpy (event_->local_addr, zlink_msg_data (&msg), local_copy);
    event_->local_addr[local_copy] = '\0';
    zlink_msg_close (&msg);

    zlink_msg_init (&msg);
    rc = zlink_msg_recv (&msg, monitor_socket_, follow_flags);
    if (rc == -1) {
        zlink_msg_close (&msg);
        return -1;
    }
    const size_t remote_size = zlink_msg_size (&msg);
    const size_t remote_copy =
      remote_size >= sizeof (event_->remote_addr)
        ? sizeof (event_->remote_addr) - 1
        : remote_size;
    if (remote_copy > 0)
        memcpy (event_->remote_addr, zlink_msg_data (&msg), remote_copy);
    event_->remote_addr[remote_copy] = '\0';
    zlink_msg_close (&msg);

    return 0;
}

int zlink_socket_peer_info (void *socket_,
                          const zlink_routing_id_t *routing_id_,
                          zlink_peer_info_t *info_)
{
    socket_handle_t handle = as_socket_handle (socket_);
    if (!handle.socket)
        return -1;
    return handle.socket->socket_peer_info (routing_id_, info_);
}

int zlink_socket_peer_routing_id (void *socket_,
                                int index_,
                                zlink_routing_id_t *out_)
{
    socket_handle_t handle = as_socket_handle (socket_);
    if (!handle.socket)
        return -1;
    return handle.socket->socket_peer_routing_id (index_, out_);
}

int zlink_socket_peer_count (void *socket_)
{
    socket_handle_t handle = as_socket_handle (socket_);
    if (!handle.socket)
        return -1;
    return handle.socket->socket_peer_count ();
}

int zlink_socket_peers (void *socket_, zlink_peer_info_t *peers_, size_t *count_)
{
    socket_handle_t handle = as_socket_handle (socket_);
    if (!handle.socket)
        return -1;
    return handle.socket->socket_peers (peers_, count_);
}

void zlink_msgv_close (zlink_msg_t *parts_, size_t part_count_)
{
    if (!parts_)
        return;
    for (size_t i = 0; i < part_count_; ++i)
        zlink_msg_close (&parts_[i]);
    free (parts_);
}

// Service Discovery API

void *zlink_registry_new (void *ctx_)
{
    if (!ctx_ || !(static_cast<zlink::ctx_t *> (ctx_))->check_tag ()) {
        errno = EFAULT;
        return NULL;
    }
    zlink::registry_t *registry =
      new (std::nothrow) zlink::registry_t (static_cast<zlink::ctx_t *> (ctx_));
    if (!registry) {
        errno = ENOMEM;
        return NULL;
    }
    return static_cast<void *> (registry);
}

int zlink_registry_set_endpoints (void *registry_,
                                const char *pub_endpoint_,
                                const char *router_endpoint_)
{
    if (!registry_)
        return -1;
    zlink::registry_t *registry = static_cast<zlink::registry_t *> (registry_);
    if (!registry->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    return registry->set_endpoints (pub_endpoint_, router_endpoint_);
}

int zlink_registry_set_id (void *registry_, uint32_t registry_id_)
{
    if (!registry_)
        return -1;
    zlink::registry_t *registry = static_cast<zlink::registry_t *> (registry_);
    if (!registry->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    return registry->set_id (registry_id_);
}

int zlink_registry_add_peer (void *registry_, const char *peer_pub_endpoint_)
{
    if (!registry_)
        return -1;
    zlink::registry_t *registry = static_cast<zlink::registry_t *> (registry_);
    if (!registry->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    return registry->add_peer (peer_pub_endpoint_);
}

int zlink_registry_set_heartbeat (void *registry_,
                                uint32_t interval_ms_,
                                uint32_t timeout_ms_)
{
    if (!registry_)
        return -1;
    zlink::registry_t *registry = static_cast<zlink::registry_t *> (registry_);
    if (!registry->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    return registry->set_heartbeat (interval_ms_, timeout_ms_);
}

int zlink_registry_set_broadcast_interval (void *registry_,
                                         uint32_t interval_ms_)
{
    if (!registry_)
        return -1;
    zlink::registry_t *registry = static_cast<zlink::registry_t *> (registry_);
    if (!registry->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    return registry->set_broadcast_interval (interval_ms_);
}

int zlink_registry_setsockopt (void *registry_,
                               int socket_role_,
                               int option_,
                               const void *optval_,
                               size_t optvallen_)
{
    if (!registry_)
        return -1;
    zlink::registry_t *registry = static_cast<zlink::registry_t *> (registry_);
    if (!registry->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    return registry->set_socket_option (socket_role_, option_, optval_,
                                        optvallen_);
}

int zlink_registry_start (void *registry_)
{
    if (!registry_)
        return -1;
    zlink::registry_t *registry = static_cast<zlink::registry_t *> (registry_);
    if (!registry->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    return registry->start ();
}

int zlink_registry_destroy (void **registry_p_)
{
    if (!registry_p_ || !*registry_p_) {
        errno = EFAULT;
        return -1;
    }
    zlink::registry_t *registry = static_cast<zlink::registry_t *> (*registry_p_);
    *registry_p_ = NULL;
    if (!registry->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    registry->destroy ();
    delete registry;
    return 0;
}

void *zlink_discovery_new (void *ctx_)
{
    if (!ctx_ || !(static_cast<zlink::ctx_t *> (ctx_))->check_tag ()) {
        errno = EFAULT;
        return NULL;
    }
    zlink::discovery_t *discovery =
      new (std::nothrow) zlink::discovery_t (static_cast<zlink::ctx_t *> (ctx_));
    if (!discovery) {
        errno = ENOMEM;
        return NULL;
    }
    return static_cast<void *> (discovery);
}

int zlink_discovery_connect_registry (void *discovery_,
                                    const char *registry_pub_endpoint_)
{
    if (!discovery_)
        return -1;
    zlink::discovery_t *discovery = static_cast<zlink::discovery_t *> (discovery_);
    if (!discovery->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    return discovery->connect_registry (registry_pub_endpoint_);
}

int zlink_discovery_subscribe (void *discovery_, const char *service_name_)
{
    if (!discovery_)
        return -1;
    zlink::discovery_t *discovery = static_cast<zlink::discovery_t *> (discovery_);
    if (!discovery->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    return discovery->subscribe (service_name_);
}

int zlink_discovery_unsubscribe (void *discovery_, const char *service_name_)
{
    if (!discovery_)
        return -1;
    zlink::discovery_t *discovery = static_cast<zlink::discovery_t *> (discovery_);
    if (!discovery->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    return discovery->unsubscribe (service_name_);
}

int zlink_discovery_get_providers (void *discovery_,
                                 const char *service_name_,
                                 zlink_provider_info_t *providers_,
                                 size_t *count_)
{
    if (!discovery_)
        return -1;
    zlink::discovery_t *discovery = static_cast<zlink::discovery_t *> (discovery_);
    if (!discovery->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    return discovery->get_providers (service_name_, providers_, count_);
}

int zlink_discovery_provider_count (void *discovery_,
                                  const char *service_name_)
{
    if (!discovery_)
        return -1;
    zlink::discovery_t *discovery = static_cast<zlink::discovery_t *> (discovery_);
    if (!discovery->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    return discovery->provider_count (service_name_);
}

int zlink_discovery_service_available (void *discovery_,
                                       const char *service_name_)
{
    if (!discovery_)
        return -1;
    zlink::discovery_t *discovery = static_cast<zlink::discovery_t *> (discovery_);
    if (!discovery->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    return discovery->service_available (service_name_);
}

int zlink_discovery_setsockopt (void *discovery_,
                                int socket_role_,
                                int option_,
                                const void *optval_,
                                size_t optvallen_)
{
    if (!discovery_)
        return -1;
    zlink::discovery_t *discovery = static_cast<zlink::discovery_t *> (discovery_);
    if (!discovery->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    return discovery->set_socket_option (socket_role_, option_, optval_,
                                         optvallen_);
}

int zlink_discovery_destroy (void **discovery_p_)
{
    if (!discovery_p_ || !*discovery_p_) {
        errno = EFAULT;
        return -1;
    }
    zlink::discovery_t *discovery = static_cast<zlink::discovery_t *> (*discovery_p_);
    *discovery_p_ = NULL;
    if (!discovery->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    discovery->destroy ();
    delete discovery;
    return 0;
}

void *zlink_gateway_new (void *ctx_, void *discovery_, const char *routing_id_)
{
    if (!ctx_ || !(static_cast<zlink::ctx_t *> (ctx_))->check_tag ()) {
        errno = EFAULT;
        return NULL;
    }
    if (!discovery_) {
        errno = EINVAL;
        return NULL;
    }
    zlink::discovery_t *disc = static_cast<zlink::discovery_t *> (discovery_);
    if (!disc->check_tag ()) {
        errno = EFAULT;
        return NULL;
    }
    zlink::gateway_t *gateway =
      new (std::nothrow) zlink::gateway_t (static_cast<zlink::ctx_t *> (ctx_),
                                           disc, routing_id_);
    if (!gateway) {
        errno = ENOMEM;
        return NULL;
    }
    return static_cast<void *> (gateway);
}

int zlink_gateway_send (void *gateway_,
                        const char *service_name_,
                        zlink_msg_t *parts_,
                        size_t part_count_,
                        int flags_)
{
    if (!gateway_)
        return -1;
    zlink::gateway_t *gateway = static_cast<zlink::gateway_t *> (gateway_);
    if (!gateway->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    return gateway->send (service_name_, parts_, part_count_, flags_);
}

int zlink_gateway_recv (void *gateway_,
                        zlink_msg_t **parts_,
                        size_t *part_count_,
                        int flags_,
                        char *service_name_out_)
{
    if (!gateway_)
        return -1;
    zlink::gateway_t *gateway = static_cast<zlink::gateway_t *> (gateway_);
    if (!gateway->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    return gateway->recv (parts_, part_count_, flags_, service_name_out_);
}

int zlink_gateway_send_rid (void *gateway_,
                            const char *service_name_,
                            const zlink_routing_id_t *routing_id_,
                            zlink_msg_t *parts_,
                            size_t part_count_,
                            int flags_)
{
    if (!gateway_)
        return -1;
    zlink::gateway_t *gateway = static_cast<zlink::gateway_t *> (gateway_);
    if (!gateway->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    return gateway->send_rid (service_name_, routing_id_, parts_, part_count_,
                              flags_);
}

int zlink_gateway_set_lb_strategy (void *gateway_,
                                   const char *service_name_,
                                   int strategy_)
{
    if (!gateway_)
        return -1;
    zlink::gateway_t *gateway = static_cast<zlink::gateway_t *> (gateway_);
    if (!gateway->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    return gateway->set_lb_strategy (service_name_, strategy_);
}

int zlink_gateway_setsockopt (void *gateway_,
                              int option_,
                              const void *optval_,
                              size_t optvallen_)
{
    if (!gateway_)
        return -1;
    zlink::gateway_t *gateway = static_cast<zlink::gateway_t *> (gateway_);
    if (!gateway->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    return gateway->set_socket_option (option_, optval_, optvallen_);
}

int zlink_gateway_set_tls_client (void *gateway_,
                                const char *ca_cert_,
                                const char *hostname_,
                                int trust_system_)
{
    if (!gateway_)
        return -1;
    zlink::gateway_t *gateway = static_cast<zlink::gateway_t *> (gateway_);
    if (!gateway->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    return gateway->set_tls_client (ca_cert_, hostname_, trust_system_);
}

void *zlink_gateway_router (void *gateway_)
{
    if (!gateway_)
        return NULL;
    zlink::gateway_t *gateway = static_cast<zlink::gateway_t *> (gateway_);
    if (!gateway->check_tag ()) {
        errno = EFAULT;
        return NULL;
    }
    return gateway->router ();
}

int zlink_gateway_connection_count (void *gateway_, const char *service_name_)
{
    if (!gateway_)
        return -1;
    zlink::gateway_t *gateway = static_cast<zlink::gateway_t *> (gateway_);
    if (!gateway->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    return gateway->connection_count (service_name_);
}

int zlink_gateway_destroy (void **gateway_p_)
{
    if (!gateway_p_ || !*gateway_p_) {
        errno = EFAULT;
        return -1;
    }
    zlink::gateway_t *gateway = static_cast<zlink::gateway_t *> (*gateway_p_);
    *gateway_p_ = NULL;
    if (!gateway->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    gateway->destroy ();
    delete gateway;
    return 0;
}

void *zlink_provider_new (void *ctx_, const char *routing_id_)
{
    if (!ctx_ || !(static_cast<zlink::ctx_t *> (ctx_))->check_tag ()) {
        errno = EFAULT;
        return NULL;
    }
    zlink::provider_t *provider =
      new (std::nothrow) zlink::provider_t (static_cast<zlink::ctx_t *> (ctx_),
                                            routing_id_);
    if (!provider) {
        errno = ENOMEM;
        return NULL;
    }
    return static_cast<void *> (provider);
}

int zlink_provider_bind (void *provider_, const char *bind_endpoint_)
{
    if (!provider_)
        return -1;
    zlink::provider_t *provider = static_cast<zlink::provider_t *> (provider_);
    if (!provider->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    return provider->bind (bind_endpoint_);
}

int zlink_provider_connect_registry (void *provider_,
                                   const char *registry_endpoint_)
{
    if (!provider_)
        return -1;
    zlink::provider_t *provider = static_cast<zlink::provider_t *> (provider_);
    if (!provider->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    return provider->connect_registry (registry_endpoint_);
}

int zlink_provider_register (void *provider_,
                           const char *service_name_,
                           const char *advertise_endpoint_,
                           uint32_t weight_)
{
    if (!provider_)
        return -1;
    zlink::provider_t *provider = static_cast<zlink::provider_t *> (provider_);
    if (!provider->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    return provider->register_service (service_name_, advertise_endpoint_,
                                       weight_);
}

int zlink_provider_update_weight (void *provider_,
                                const char *service_name_,
                                uint32_t weight_)
{
    if (!provider_)
        return -1;
    zlink::provider_t *provider = static_cast<zlink::provider_t *> (provider_);
    if (!provider->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    return provider->update_weight (service_name_, weight_);
}

int zlink_provider_unregister (void *provider_, const char *service_name_)
{
    if (!provider_)
        return -1;
    zlink::provider_t *provider = static_cast<zlink::provider_t *> (provider_);
    if (!provider->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    return provider->unregister_service (service_name_);
}

int zlink_provider_register_result (void *provider_,
                                  const char *service_name_,
                                  int *status_,
                                  char *resolved_endpoint_,
                                  char *error_message_)
{
    if (!provider_)
        return -1;
    zlink::provider_t *provider = static_cast<zlink::provider_t *> (provider_);
    if (!provider->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    return provider->register_result (service_name_, status_, resolved_endpoint_,
                                      error_message_);
}

int zlink_provider_set_tls_server (void *provider_,
                                   const char *cert_,
                                   const char *key_)
{
    if (!provider_)
        return -1;
    zlink::provider_t *provider = static_cast<zlink::provider_t *> (provider_);
    if (!provider->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    return provider->set_tls_server (cert_, key_);
}

int zlink_provider_setsockopt (void *provider_,
                               int socket_role_,
                               int option_,
                               const void *optval_,
                               size_t optvallen_)
{
    if (!provider_)
        return -1;
    zlink::provider_t *provider = static_cast<zlink::provider_t *> (provider_);
    if (!provider->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    return provider->set_socket_option (socket_role_, option_, optval_,
                                        optvallen_);
}

void *zlink_provider_router (void *provider_)
{
    if (!provider_)
        return NULL;
    zlink::provider_t *provider = static_cast<zlink::provider_t *> (provider_);
    if (!provider->check_tag ()) {
        errno = EFAULT;
        return NULL;
    }
    return provider->router ();
}

int zlink_provider_destroy (void **provider_p_)
{
    if (!provider_p_ || !*provider_p_) {
        errno = EFAULT;
        return -1;
    }
    zlink::provider_t *provider = static_cast<zlink::provider_t *> (*provider_p_);
    *provider_p_ = NULL;
    if (!provider->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    provider->destroy ();
    delete provider;
    return 0;
}

void *zlink_spot_node_new (void *ctx_)
{
    if (!ctx_ || !(static_cast<zlink::ctx_t *> (ctx_))->check_tag ()) {
        errno = EFAULT;
        return NULL;
    }
    zlink::spot_node_t *node =
      new (std::nothrow) zlink::spot_node_t (static_cast<zlink::ctx_t *> (ctx_));
    if (!node) {
        errno = ENOMEM;
        return NULL;
    }
    return static_cast<void *> (node);
}

int zlink_spot_node_destroy (void **node_p_)
{
    if (!node_p_ || !*node_p_) {
        errno = EFAULT;
        return -1;
    }
    zlink::spot_node_t *node = static_cast<zlink::spot_node_t *> (*node_p_);
    *node_p_ = NULL;
    if (!node->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    node->destroy ();
    delete node;
    return 0;
}

int zlink_spot_node_bind (void *node_, const char *endpoint_)
{
    if (!node_)
        return -1;
    zlink::spot_node_t *node = static_cast<zlink::spot_node_t *> (node_);
    if (!node->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    return node->bind (endpoint_);
}

int zlink_spot_node_connect_registry (void *node_,
                                    const char *registry_endpoint_)
{
    if (!node_)
        return -1;
    zlink::spot_node_t *node = static_cast<zlink::spot_node_t *> (node_);
    if (!node->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    return node->connect_registry (registry_endpoint_);
}

int zlink_spot_node_connect_peer_pub (void *node_,
                                    const char *peer_pub_endpoint_)
{
    if (!node_)
        return -1;
    zlink::spot_node_t *node = static_cast<zlink::spot_node_t *> (node_);
    if (!node->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    return node->connect_peer_pub (peer_pub_endpoint_);
}

int zlink_spot_node_disconnect_peer_pub (void *node_,
                                       const char *peer_pub_endpoint_)
{
    if (!node_)
        return -1;
    zlink::spot_node_t *node = static_cast<zlink::spot_node_t *> (node_);
    if (!node->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    return node->disconnect_peer_pub (peer_pub_endpoint_);
}

int zlink_spot_node_register (void *node_,
                            const char *service_name_,
                            const char *advertise_endpoint_)
{
    if (!node_)
        return -1;
    zlink::spot_node_t *node = static_cast<zlink::spot_node_t *> (node_);
    if (!node->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    return node->register_node (service_name_, advertise_endpoint_);
}

int zlink_spot_node_unregister (void *node_, const char *service_name_)
{
    if (!node_)
        return -1;
    zlink::spot_node_t *node = static_cast<zlink::spot_node_t *> (node_);
    if (!node->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    return node->unregister_node (service_name_);
}

int zlink_spot_node_set_discovery (void *node_,
                                void *discovery_,
                                const char *service_name_)
{
    if (!node_ || !discovery_)
        return -1;
    zlink::spot_node_t *node = static_cast<zlink::spot_node_t *> (node_);
    if (!node->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    zlink::discovery_t *disc = static_cast<zlink::discovery_t *> (discovery_);
    if (!disc->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    return node->set_discovery (disc, service_name_);
}

int zlink_spot_node_set_tls_server (void *node_,
                                  const char *cert_,
                                  const char *key_)
{
    if (!node_)
        return -1;
    zlink::spot_node_t *node = static_cast<zlink::spot_node_t *> (node_);
    if (!node->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    return node->set_tls_server (cert_, key_);
}

int zlink_spot_node_set_tls_client (void *node_,
                                  const char *ca_cert_,
                                  const char *hostname_,
                                  int trust_system_)
{
    if (!node_)
        return -1;
    zlink::spot_node_t *node = static_cast<zlink::spot_node_t *> (node_);
    if (!node->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    return node->set_tls_client (ca_cert_, hostname_, trust_system_);
}

void *zlink_spot_new (void *node_)
{
    if (!node_)
        return NULL;
    zlink::spot_node_t *node = static_cast<zlink::spot_node_t *> (node_);
    if (!node->check_tag ()) {
        errno = EFAULT;
        return NULL;
    }
    zlink::spot_t *spot = node->create_spot ();
    if (!spot)
        return NULL;
    return static_cast<void *> (spot);
}

int zlink_spot_destroy (void **spot_p_)
{
    if (!spot_p_ || !*spot_p_) {
        errno = EFAULT;
        return -1;
    }
    zlink::spot_t *spot = static_cast<zlink::spot_t *> (*spot_p_);
    *spot_p_ = NULL;
    if (!spot->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    spot->destroy ();
    delete spot;
    return 0;
}

int zlink_spot_topic_create (void *spot_, const char *topic_id_, int mode_)
{
    if (!spot_)
        return -1;
    zlink::spot_t *spot = static_cast<zlink::spot_t *> (spot_);
    if (!spot->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    return spot->topic_create (topic_id_, mode_);
}

int zlink_spot_topic_destroy (void *spot_, const char *topic_id_)
{
    if (!spot_)
        return -1;
    zlink::spot_t *spot = static_cast<zlink::spot_t *> (spot_);
    if (!spot->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    return spot->topic_destroy (topic_id_);
}

int zlink_spot_publish (void *spot_,
                      const char *topic_id_,
                      zlink_msg_t *parts_,
                      size_t part_count_,
                      int flags_)
{
    if (!spot_)
        return -1;
    zlink::spot_t *spot = static_cast<zlink::spot_t *> (spot_);
    if (!spot->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    return spot->publish (topic_id_, parts_, part_count_, flags_);
}

int zlink_spot_subscribe (void *spot_, const char *topic_id_)
{
    if (!spot_)
        return -1;
    zlink::spot_t *spot = static_cast<zlink::spot_t *> (spot_);
    if (!spot->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    return spot->subscribe (topic_id_);
}

int zlink_spot_subscribe_pattern (void *spot_, const char *pattern_)
{
    if (!spot_)
        return -1;
    zlink::spot_t *spot = static_cast<zlink::spot_t *> (spot_);
    if (!spot->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    return spot->subscribe_pattern (pattern_);
}

int zlink_spot_unsubscribe (void *spot_, const char *topic_id_or_pattern_)
{
    if (!spot_)
        return -1;
    zlink::spot_t *spot = static_cast<zlink::spot_t *> (spot_);
    if (!spot->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    return spot->unsubscribe (topic_id_or_pattern_);
}

int zlink_spot_recv (void *spot_,
                   zlink_msg_t **parts_,
                   size_t *part_count_,
                   int flags_,
                   char *topic_id_out_,
                   size_t *topic_id_len_)
{
    if (!spot_)
        return -1;
    zlink::spot_t *spot = static_cast<zlink::spot_t *> (spot_);
    if (!spot->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    return spot->recv (parts_, part_count_, flags_, topic_id_out_,
                       topic_id_len_);
}

void *zlink_spot_node_pub_socket (void *node_)
{
    if (!node_)
        return NULL;
    zlink::spot_node_t *node = static_cast<zlink::spot_node_t *> (node_);
    if (!node->check_tag ()) {
        errno = EFAULT;
        return NULL;
    }
    return static_cast<void *> (node->pub_socket ());
}

void *zlink_spot_node_sub_socket (void *node_)
{
    if (!node_)
        return NULL;
    zlink::spot_node_t *node = static_cast<zlink::spot_node_t *> (node_);
    if (!node->check_tag ()) {
        errno = EFAULT;
        return NULL;
    }
    return static_cast<void *> (node->sub_socket ());
}

int zlink_spot_node_setsockopt (void *node_,
                                int socket_role_,
                                int option_,
                                const void *optval_,
                                size_t optvallen_)
{
    if (!node_)
        return -1;
    zlink::spot_node_t *node = static_cast<zlink::spot_node_t *> (node_);
    if (!node->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    return node->set_socket_option (socket_role_, option_, optval_, optvallen_);
}

void *zlink_spot_pub_socket (void *spot_)
{
    if (!spot_)
        return NULL;
    zlink::spot_t *spot = static_cast<zlink::spot_t *> (spot_);
    if (!spot->check_tag ()) {
        errno = EFAULT;
        return NULL;
    }
    return static_cast<void *> (spot->pub_socket ());
}

void *zlink_spot_sub_socket (void *spot_)
{
    if (!spot_)
        return NULL;
    zlink::spot_t *spot = static_cast<zlink::spot_t *> (spot_);
    if (!spot->check_tag ()) {
        errno = EFAULT;
        return NULL;
    }
    return static_cast<void *> (spot->sub_socket ());
}

int zlink_spot_setsockopt (void *spot_,
                           int socket_role_,
                           int option_,
                           const void *optval_,
                           size_t optvallen_)
{
    if (!spot_)
        return -1;
    zlink::spot_t *spot = static_cast<zlink::spot_t *> (spot_);
    if (!spot->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    return spot->set_socket_option (socket_role_, option_, optval_, optvallen_);
}

int zlink_bind (void *s_, const char *addr_)
{
    socket_handle_t handle = as_socket_handle (s_);
    if (!handle.socket)
        return -1;
    return handle.socket->bind (addr_);
}

int zlink_connect (void *s_, const char *addr_)
{
    socket_handle_t handle = as_socket_handle (s_);
    if (!handle.socket)
        return -1;
    return handle.socket->connect (addr_);
}

int zlink_unbind (void *s_, const char *addr_)
{
    socket_handle_t handle = as_socket_handle (s_);
    if (!handle.socket)
        return -1;
    return handle.socket->term_endpoint (addr_);
}

int zlink_disconnect (void *s_, const char *addr_)
{
    socket_handle_t handle = as_socket_handle (s_);
    if (!handle.socket)
        return -1;
    return handle.socket->term_endpoint (addr_);
}

// Sending functions.

static inline int
s_sendmsg (socket_handle_t handle_, zlink_msg_t *msg_, int flags_)
{
    size_t sz = zlink_msg_size (msg_);
    int rc = handle_.socket->send (reinterpret_cast<zlink::msg_t *> (msg_),
                                   flags_);
    if (unlikely (rc < 0))
        return -1;

    size_t max_msgsz = INT_MAX;
    return static_cast<int> (sz < max_msgsz ? sz : max_msgsz);
}

int zlink_send (void *s_, const void *buf_, size_t len_, int flags_)
{
    socket_handle_t handle = as_socket_handle (s_);
    if (!handle.socket)
        return -1;
    zlink_msg_t msg;
    int rc = zlink_msg_init_buffer (&msg, buf_, len_);
    if (unlikely (rc < 0))
        return -1;

    rc = s_sendmsg (handle, &msg, flags_);
    if (unlikely (rc < 0)) {
        const int err = errno;
        zlink_msg_close (&msg);
        errno = err;
        return -1;
    }
    return rc;
}

int zlink_send_const (void *s_, const void *buf_, size_t len_, int flags_)
{
    socket_handle_t handle = as_socket_handle (s_);
    if (!handle.socket)
        return -1;
    zlink_msg_t msg;
    int rc =
      zlink_msg_init_data (&msg, const_cast<void *> (buf_), len_, NULL, NULL);
    if (rc != 0)
        return -1;

    rc = s_sendmsg (handle, &msg, flags_);
    if (unlikely (rc < 0)) {
        const int err = errno;
        zlink_msg_close (&msg);
        errno = err;
        return -1;
    }
    return rc;
}

// Receiving functions.

static int s_recvmsg (socket_handle_t handle_, zlink_msg_t *msg_, int flags_)
{
    int rc =
      handle_.socket->recv (reinterpret_cast<zlink::msg_t *> (msg_), flags_);
    if (unlikely (rc < 0))
        return -1;

    const size_t sz = zlink_msg_size (msg_);
    return static_cast<int> (sz < INT_MAX ? sz : INT_MAX);
}

int zlink_recv (void *s_, void *buf_, size_t len_, int flags_)
{
    socket_handle_t handle = as_socket_handle (s_);
    if (!handle.socket)
        return -1;
    zlink_msg_t msg;
    int rc = zlink_msg_init (&msg);
    errno_assert (rc == 0);

    const int nbytes = s_recvmsg (handle, &msg, flags_);
    if (unlikely (nbytes < 0)) {
        const int err = errno;
        zlink_msg_close (&msg);
        errno = err;
        return -1;
    }

    const size_t to_copy = size_t (nbytes) < len_ ? size_t (nbytes) : len_;
    if (to_copy) {
        memcpy (buf_, zlink_msg_data (&msg), to_copy);
    }
    zlink_msg_close (&msg);

    return nbytes;
}

// Message manipulators.

int zlink_msg_init (zlink_msg_t *msg_)
{
    return (reinterpret_cast<zlink::msg_t *> (msg_))->init ();
}

int zlink_msg_init_size (zlink_msg_t *msg_, size_t size_)
{
    return (reinterpret_cast<zlink::msg_t *> (msg_))->init_size (size_);
}

int zlink_msg_init_buffer (zlink_msg_t *msg_, const void *buf_, size_t size_)
{
    return (reinterpret_cast<zlink::msg_t *> (msg_))->init_buffer (buf_, size_);
}

int zlink_msg_init_data (
  zlink_msg_t *msg_, void *data_, size_t size_, zlink_free_fn *ffn_, void *hint_)
{
    return (reinterpret_cast<zlink::msg_t *> (msg_))
      ->init_data (data_, size_, ffn_, hint_);
}

int zlink_msg_send (zlink_msg_t *msg_, void *s_, int flags_)
{
    socket_handle_t handle = as_socket_handle (s_);
    if (!handle.socket)
        return -1;
    return s_sendmsg (handle, msg_, flags_);
}

int zlink_msg_recv (zlink_msg_t *msg_, void *s_, int flags_)
{
    socket_handle_t handle = as_socket_handle (s_);
    if (!handle.socket)
        return -1;
    return s_recvmsg (handle, msg_, flags_);
}

int zlink_msg_close (zlink_msg_t *msg_)
{
    return (reinterpret_cast<zlink::msg_t *> (msg_))->close ();
}

int zlink_msg_move (zlink_msg_t *dest_, zlink_msg_t *src_)
{
    return (reinterpret_cast<zlink::msg_t *> (dest_))
      ->move (*reinterpret_cast<zlink::msg_t *> (src_));
}

int zlink_msg_copy (zlink_msg_t *dest_, zlink_msg_t *src_)
{
    return (reinterpret_cast<zlink::msg_t *> (dest_))
      ->copy (*reinterpret_cast<zlink::msg_t *> (src_));
}

void *zlink_msg_data (zlink_msg_t *msg_)
{
    return (reinterpret_cast<zlink::msg_t *> (msg_))->data ();
}

size_t zlink_msg_size (const zlink_msg_t *msg_)
{
    return ((zlink::msg_t *) msg_)->size ();
}

int zlink_msg_more (const zlink_msg_t *msg_)
{
    return (((zlink::msg_t *) msg_)->flags () & zlink::msg_t::more) ? 1 : 0;
}

int zlink_msg_get (const zlink_msg_t *msg_, int property_)
{
    switch (property_) {
        case ZLINK_MORE:
            return (((zlink::msg_t *) msg_)->flags () & zlink::msg_t::more) ? 1 : 0;
        case ZLINK_SHARED:
            return (((zlink::msg_t *) msg_)->is_cmsg ())
                       || (((zlink::msg_t *) msg_)->flags () & zlink::msg_t::shared)
                     ? 1
                     : 0;
        default:
            errno = EINVAL;
            return -1;
    }
}

int zlink_msg_set (zlink_msg_t *, int, int)
{
    errno = EINVAL;
    return -1;
}

const char *zlink_msg_gets (const zlink_msg_t *msg_, const char *property_)
{
    const zlink::metadata_t *metadata =
      reinterpret_cast<const zlink::msg_t *> (msg_)->metadata ();
    const char *value = NULL;
    if (metadata)
        value = metadata->get (std::string (property_));
    if (value)
        return value;

    errno = EINVAL;
    return NULL;
}

// Polling.

int zlink_poll (zlink_pollitem_t *items_, int nitems_, long timeout_)
{
    if (unlikely (nitems_ < 0)) {
        errno = EINVAL;
        return -1;
    }
    if (unlikely (nitems_ == 0)) {
        if (timeout_ == 0)
            return 0;
#if defined ZLINK_HAVE_WINDOWS
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

    zlink::fast_vector_t<pollfd, ZLINK_POLLITEMS_DFLT> pollfds (nitems_);
    zlink::fast_vector_t<zlink::socket_base_t *, ZLINK_POLLITEMS_DFLT>
      sockets (nitems_);
    bool socket_fd_unavailable = false;

    for (int i = 0; i != nitems_; i++) {
        if (items_[i].socket) {
            zlink::socket_base_t *socket =
              static_cast<zlink::socket_base_t *> (items_[i].socket);
            if (!socket->check_tag ()) {
                errno = ENOTSOCK;
                return -1;
            }
            sockets[i] = socket;
            size_t zlink_fd_size = sizeof (zlink::fd_t);
            if (socket->getsockopt (ZLINK_FD, &pollfds[i].fd, &zlink_fd_size)
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
            sockets[i] = NULL;
            pollfds[i].fd = items_[i].fd;
            pollfds[i].events =
              (items_[i].events & ZLINK_POLLIN ? POLLIN : 0)
              | (items_[i].events & ZLINK_POLLOUT ? POLLOUT : 0)
              | (items_[i].events & ZLINK_POLLPRI ? POLLPRI : 0);
        }
    }

    zlink::clock_t clock;
    uint64_t now = 0;
    uint64_t end = 0;
    bool first_pass = true;
    int nevents = 0;

    while (true) {
        const zlink::timeout_t timeout =
          zlink::compute_timeout (first_pass, timeout_, now, end);

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
            if (sockets[i]) {
                uint32_t zlink_events = 0;
                if (sockets[i]->get_events (items_[i].events, &zlink_events)
                    == -1) {
                    return -1;
                }
                if ((items_[i].events & ZLINK_POLLOUT)
                    && (zlink_events & ZLINK_POLLOUT))
                    items_[i].revents |= ZLINK_POLLOUT;
                if ((items_[i].events & ZLINK_POLLIN)
                    && (zlink_events & ZLINK_POLLIN))
                    items_[i].revents |= ZLINK_POLLIN;
            }
            else {
                if (pollfds[i].revents & POLLIN)
                    items_[i].revents |= ZLINK_POLLIN;
                if (pollfds[i].revents & POLLOUT)
                    items_[i].revents |= ZLINK_POLLOUT;
                if (pollfds[i].revents & POLLPRI)
                    items_[i].revents |= ZLINK_POLLPRI;
                if (pollfds[i].revents & ~(POLLIN | POLLOUT | POLLPRI))
                    items_[i].revents |= ZLINK_POLLERR;
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

int zlink_proxy (void *frontend_, void *backend_, void *capture_)
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

    zlink::socket_base_t *capture_socket = NULL;
    if (capture_) {
        socket_handle_t capture = as_socket_handle (capture_);
        if (!capture.socket)
            return -1;
        capture_socket = capture.socket;
    }

    return zlink::proxy (frontend.socket, backend.socket, capture_socket);
}

int zlink_proxy_steerable (void *frontend_,
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

    zlink::socket_base_t *capture_socket = NULL;
    if (capture_) {
        socket_handle_t capture = as_socket_handle (capture_);
        if (!capture.socket)
            return -1;
        capture_socket = capture.socket;
    }

    zlink::socket_base_t *control_socket = NULL;
    if (control_) {
        socket_handle_t control = as_socket_handle (control_);
        if (!control.socket)
            return -1;
        control_socket = control.socket;
    }

    return zlink::proxy_steerable (frontend.socket, backend.socket,
                                 capture_socket, control_socket);
}

int zlink_has (const char *capability_)
{
    // TCP is always available as a core transport
    if (strcmp (capability_, "tcp") == 0)
        return true;
#if defined(ZLINK_HAVE_IPC)
    if (strcmp (capability_, zlink::protocol_name::ipc) == 0)
        return true;
#endif
#if defined(ZLINK_HAVE_TLS)
    if (strcmp (capability_, "tls") == 0)
        return true;
#endif
#if defined(ZLINK_HAVE_WS)
    if (strcmp (capability_, "ws") == 0)
        return true;
#endif
#if defined(ZLINK_HAVE_WSS)
    if (strcmp (capability_, "wss") == 0)
        return true;
#endif
#if defined(ZLINK_HAVE_OPENPGM)
    if (strcmp (capability_, "pgm") == 0 || strcmp (capability_, "epgm") == 0)
        return true;
#endif
    return false;
}
