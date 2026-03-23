/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "utils/err.hpp"
#include "api/service_api_internal.hpp"
#include "api/zlink_testing.hpp"

#include <new>
#include <string.h>
#include <vector>

#include "core/recv_internal.hpp"
#include "services/discovery/discovery_protocol.hpp"
#include "services/discovery/registry.hpp"
#include "utils/random.hpp"

namespace
{
struct registry_query_client_t
{
    zlink::ctx_t *ctx;
    zlink::mutex_t sync;
    zlink::service_public_api_guard_t public_api;
    zlink::socket_base_t *dealer;
    uint32_t tag;

    explicit registry_query_client_t (zlink::ctx_t *ctx_) :
        ctx (ctx_),
        dealer (NULL),
        tag (0x1e6700f1)
    {
    }

    ~registry_query_client_t () { destroy_locked (); }

    bool check_tag () const { return tag == 0x1e6700f1; }

    int connect_locked (const char *endpoint_)
    {
        if (!endpoint_ || !*endpoint_) {
            errno = EINVAL;
            return -1;
        }
        if (dealer) {
            dealer->close ();
            dealer = NULL;
        }
        dealer = ctx->create_socket (ZLINK_CORE_SOCKET_DEALER);
        if (!dealer)
            return -1;
        unsigned char rid[5];
        rid[0] = 0;
        uint32_t rid_word = zlink::generate_random ();
        if (rid_word == 0)
            rid_word = 1;
        memcpy (rid + 1, &rid_word, sizeof (rid_word));
        dealer->setsockopt (ZLINK_INTERNAL_OPT_ROUTING_ID, rid, sizeof (rid));
        const int linger = 0;
        const int sndtimeo_ms = 1000;
        const int rcvtimeo_ms = 1000;
        dealer->setsockopt (ZLINK_INTERNAL_OPT_LINGER, &linger, sizeof (linger));
        dealer->setsockopt (ZLINK_INTERNAL_OPT_SNDTIMEO, &sndtimeo_ms,
                            sizeof (sndtimeo_ms));
        dealer->setsockopt (ZLINK_INTERNAL_OPT_RCVTIMEO, &rcvtimeo_ms,
                            sizeof (rcvtimeo_ms));
        if (dealer->connect (endpoint_) != 0)
            return -1;
        if (zlink::wait_socket_events_internal (
              static_cast<void *> (dealer), ZLINK_POLLOUT, 1000)
            <= 0) {
            errno = EAGAIN;
            return -1;
        }
        return 0;
    }

    int destroy_locked ()
    {
        tag = 0xdeadbeef;
        if (dealer) {
            dealer->close ();
            dealer = NULL;
        }
        return 0;
    }
};

static int recv_registry_reply_frames (void *socket_,
                                       uint16_t expected_msg_id_,
                                       void *entries_,
                                       size_t entry_size_,
                                       size_t *count_)
{
    if (!count_) {
        errno = EINVAL;
        return -1;
    }

    zlink_msg_t frame;
    zlink_msg_init (&frame);
#define RECV_TOPOLOGY_FRAME_OR_RETURN()                                          \
    do {                                                                         \
        if (zlink::wait_socket_events_internal (socket_, ZLINK_POLLIN, 1000)     \
            <= 0) {                                                              \
            errno = EAGAIN;                                                      \
            return -1;                                                           \
        }                                                                        \
        if (zlink::recv_msg_internal (socket_, &frame, 0) < 0) {                 \
            zlink_msg_close (&frame);                                            \
            return -1;                                                           \
        }                                                                        \
    } while (false)
    RECV_TOPOLOGY_FRAME_OR_RETURN();
    uint16_t msg_id = 0;
    const bool ok_msg =
      zlink::discovery_protocol::read_u16 (frame, &msg_id)
      && msg_id == expected_msg_id_;
    zlink_msg_close (&frame);
    if (!ok_msg) {
        errno = EPROTO;
        return -1;
    }

    zlink_msg_init (&frame);
    RECV_TOPOLOGY_FRAME_OR_RETURN();
    uint32_t remote_count = 0;
    const bool ok_count =
      zlink::discovery_protocol::read_u32 (frame, &remote_count);
    zlink_msg_close (&frame);
    if (!ok_count) {
        errno = EPROTO;
        return -1;
    }

    if (!entries_) {
        *count_ = remote_count;
        for (uint32_t i = 0; i < remote_count; ++i) {
            zlink_msg_init (&frame);
            RECV_TOPOLOGY_FRAME_OR_RETURN();
            zlink_msg_close (&frame);
        }
        return 0;
    }

    if (*count_ < remote_count) {
        for (uint32_t i = 0; i < remote_count; ++i) {
            zlink_msg_init (&frame);
            RECV_TOPOLOGY_FRAME_OR_RETURN();
            zlink_msg_close (&frame);
        }
        *count_ = remote_count;
        errno = ENOBUFS;
        return -1;
    }

    for (uint32_t i = 0; i < remote_count; ++i) {
        zlink_msg_init (&frame);
        RECV_TOPOLOGY_FRAME_OR_RETURN();
        if (zlink_msg_size (&frame) != entry_size_) {
            zlink_msg_close (&frame);
            errno = EPROTO;
            return -1;
        }
        memcpy (static_cast<char *> (entries_) + (i * entry_size_),
                zlink_msg_data (&frame), entry_size_);
        zlink_msg_close (&frame);
    }
#undef RECV_TOPOLOGY_FRAME_OR_RETURN

    *count_ = remote_count;
    return 0;
}

static int recv_topology_reply_frames (
  void *socket_,
  zlink_registry_topology_entry_t *entries_,
  size_t *count_)
{
    return recv_registry_reply_frames (
      socket_, zlink::discovery_protocol::msg_topology_reply, entries_,
      sizeof (zlink_registry_topology_entry_t), count_);
}

static int recv_gateway_peer_reply_frames (
  void *socket_,
  zlink_registry_gateway_peer_entry_t *entries_,
  size_t *count_)
{
    return recv_registry_reply_frames (
      socket_, zlink::discovery_protocol::msg_gateway_peer_reply, entries_,
      sizeof (zlink_registry_gateway_peer_entry_t), count_);
}
}

namespace zlink
{
service_public_api_guard_t *
registry_query_public_api_guard_for_testing (void *client_)
{
    registry_query_client_t *client =
      static_cast<registry_query_client_t *> (client_);
    if (!client || !client->check_tag ())
        return NULL;
    return &client->public_api;
}

void destroy_registry_query_client_for_testing (void *client_)
{
    registry_query_client_t *client =
      static_cast<registry_query_client_t *> (client_);
    if (!client || !client->check_tag ())
        return;
    client->destroy_locked ();
    delete client;
}
}

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

int zlink_registry_bind (void *registry_,
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
    return registry->bind (pub_endpoint_, router_endpoint_);
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

int zlink_registry_destroy (void **registry_p_)
{
    if (!registry_p_ || !*registry_p_) {
        errno = EFAULT;
        return -1;
    }
    zlink::registry_t *registry = static_cast<zlink::registry_t *> (*registry_p_);
    if (!registry->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    if (registry->destroy () != 0)
        return -1;
    delete registry;
    *registry_p_ = NULL;
    return 0;
}

int zlink_registry_topology_snapshot (void *registry_,
                                      zlink_registry_topology_entry_t *entries_,
                                      size_t *count_)
{
    if (!registry_)
        return -1;
    zlink::registry_t *registry = static_cast<zlink::registry_t *> (registry_);
    if (!registry->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    return registry->topology_snapshot (entries_, count_);
}

int zlink_registry_status_snapshot (void *registry_,
                                    zlink_registry_status_t *out_)
{
    if (!registry_) {
        errno = EFAULT;
        return -1;
    }
    zlink::registry_t *registry = static_cast<zlink::registry_t *> (registry_);
    if (!registry->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    return registry->status_snapshot (out_);
}

int zlink_registry_service_summary_snapshot (
  void *registry_,
  const zlink_registry_service_summary_filter_t *filter_,
  zlink_registry_service_summary_entry_t *entries_,
  size_t *count_)
{
    if (!registry_) {
        errno = EFAULT;
        return -1;
    }
    zlink::registry_t *registry = static_cast<zlink::registry_t *> (registry_);
    if (!registry->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    if (!count_) {
        errno = EINVAL;
        return -1;
    }
    std::vector<zlink_registry_service_summary_entry_t> rows;
    if (registry->service_summary_snapshot (filter_, &rows) != 0)
        return -1;
    if (!entries_) {
        *count_ = rows.size ();
        return 0;
    }
    if (*count_ < rows.size ()) {
        *count_ = rows.size ();
        errno = ENOBUFS;
        return -1;
    }
    for (size_t i = 0; i < rows.size (); ++i)
        entries_[i] = rows[i];
    *count_ = rows.size ();
    return 0;
}

int zlink_registry_topology_query (
  void *registry_,
  const zlink_registry_topology_filter_t *filter_,
  zlink_registry_topology_entry_t *entries_,
  size_t *count_)
{
    if (!registry_)
        return -1;
    zlink::registry_t *registry = static_cast<zlink::registry_t *> (registry_);
    if (!registry->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    return registry->topology_query (filter_, entries_, count_);
}

int zlink_registry_gateway_peers_snapshot (
  void *registry_,
  zlink_registry_gateway_peer_entry_t *entries_,
  size_t *count_)
{
    if (!registry_)
        return -1;
    zlink::registry_t *registry = static_cast<zlink::registry_t *> (registry_);
    if (!registry->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    return registry->gateway_peers_snapshot (entries_, count_);
}

int zlink_registry_gateway_peers_query (
  void *registry_,
  const zlink_registry_gateway_peer_filter_t *filter_,
  zlink_registry_gateway_peer_entry_t *entries_,
  size_t *count_)
{
    if (!registry_)
        return -1;
    zlink::registry_t *registry = static_cast<zlink::registry_t *> (registry_);
    if (!registry->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    return registry->gateway_peers_query (filter_, entries_, count_);
}

void *zlink_registry_query_client_new (void *ctx_)
{
    if (!ctx_ || !(static_cast<zlink::ctx_t *> (ctx_))->check_tag ()) {
        errno = EFAULT;
        return NULL;
    }
    registry_query_client_t *client =
      new (std::nothrow)
        registry_query_client_t (static_cast<zlink::ctx_t *> (ctx_));
    if (!client) {
        errno = ENOMEM;
        return NULL;
    }
    return client;
}

int zlink_registry_query_client_connect (void *client_,
                                         const char *endpoint_)
{
    if (!client_) {
        errno = EFAULT;
        return -1;
    }
    registry_query_client_t *client =
      static_cast<registry_query_client_t *> (client_);
    if (!client->check_tag ()) {
        errno = EFAULT;
        return -1;
    }

    zlink::service_public_api_scope_t admission (client->public_api);
    if (!admission.acquired ())
        return -1;

    zlink::scoped_lock_t lock (client->sync);
    return client->connect_locked (endpoint_);
}

int zlink_registry_query_snapshot (
  void *client_,
  const zlink_registry_topology_filter_t *filter_,
  zlink_registry_topology_entry_t *entries_,
  size_t *count_)
{
    if (!client_) {
        errno = EFAULT;
        return -1;
    }
    registry_query_client_t *client =
      static_cast<registry_query_client_t *> (client_);
    if (!client->check_tag () || !client->dealer) {
        errno = EFAULT;
        return -1;
    }

    zlink::service_public_api_scope_t admission (client->public_api);
    if (!admission.acquired ())
        return -1;

    zlink::scoped_lock_t lock (client->sync);
    if (!client->dealer) {
        errno = EFAULT;
        return -1;
    }

    if (zlink::discovery_protocol::send_u16 (
          static_cast<void *> (client->dealer),
          zlink::discovery_protocol::msg_topology_query, ZLINK_SNDMORE)
        < 0)
        return -1;

    zlink_registry_topology_filter_t filter;
    memset (&filter, 0, sizeof (filter));
    if (filter_)
        filter = *filter_;
    if (zlink::discovery_protocol::send_frame (static_cast<void *> (client->dealer),
                                               &filter, sizeof (filter), 0)
        < 0)
        return -1;

    return recv_topology_reply_frames (static_cast<void *> (client->dealer),
                                       entries_, count_);
}

int zlink_registry_query_gateway_peers_snapshot (
  void *client_,
  const zlink_registry_gateway_peer_filter_t *filter_,
  zlink_registry_gateway_peer_entry_t *entries_,
  size_t *count_)
{
    if (!client_) {
        errno = EFAULT;
        return -1;
    }
    registry_query_client_t *client =
      static_cast<registry_query_client_t *> (client_);
    if (!client->check_tag () || !client->dealer) {
        errno = EFAULT;
        return -1;
    }

    zlink::service_public_api_scope_t admission (client->public_api);
    if (!admission.acquired ())
        return -1;

    zlink::scoped_lock_t lock (client->sync);
    if (!client->dealer) {
        errno = EFAULT;
        return -1;
    }

    if (zlink::discovery_protocol::send_u16 (
          static_cast<void *> (client->dealer),
          zlink::discovery_protocol::msg_gateway_peer_query, ZLINK_SNDMORE)
        < 0)
        return -1;

    zlink_registry_gateway_peer_filter_t filter;
    memset (&filter, 0, sizeof (filter));
    if (filter_)
        filter = *filter_;
    if (zlink::discovery_protocol::send_frame (static_cast<void *> (client->dealer),
                                               &filter, sizeof (filter), 0)
        < 0)
        return -1;

    return recv_gateway_peer_reply_frames (static_cast<void *> (client->dealer),
                                           entries_, count_);
}

int zlink_registry_query_destroy (void **client_p_)
{
    if (!client_p_ || !*client_p_) {
        errno = EFAULT;
        return -1;
    }
    registry_query_client_t *client =
      static_cast<registry_query_client_t *> (*client_p_);
    if (!client->check_tag ()) {
        errno = EFAULT;
        return -1;
    }

    if (!client->public_api.begin_close_or_fail_busy ())
        return -1;

    {
        zlink::scoped_lock_t lock (client->sync);
        client->destroy_locked ();
    }
    *client_p_ = NULL;
    delete client;
    return 0;
}
