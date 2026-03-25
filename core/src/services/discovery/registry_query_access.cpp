/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "services/discovery/registry_query_access.hpp"

#include <new>
#include <string.h>

#include "core/ctx.hpp"
#include "core/recv_internal.hpp"
#include "services/common/service_public_api.hpp"
#include "services/discovery/discovery_protocol.hpp"
#include "sockets/socket_base.hpp"
#include "utils/err.hpp"
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

static registry_query_client_t *as_registry_query_client (void *client_)
{
    if (!client_) {
        errno = EFAULT;
        return NULL;
    }

    registry_query_client_t *client =
      static_cast<registry_query_client_t *> (client_);
    if (!client->check_tag ()) {
        errno = EFAULT;
        return NULL;
    }
    return client;
}

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
}

namespace zlink
{
void *registry_query_access_t::create (ctx_t *ctx_)
{
    registry_query_client_t *client =
      new (std::nothrow) registry_query_client_t (ctx_);
    if (!client) {
        errno = ENOMEM;
        return NULL;
    }
    return client;
}

service_public_api_guard_t *
registry_query_access_t::public_api_guard_for_testing (void *client_)
{
    registry_query_client_t *client = as_registry_query_client (client_);
    return client ? &client->public_api : NULL;
}

void registry_query_access_t::destroy_for_testing (void *client_)
{
    registry_query_client_t *client = as_registry_query_client (client_);
    if (!client)
        return;
    client->destroy_locked ();
    delete client;
}

int registry_query_access_t::connect (void *client_, const char *endpoint_)
{
    registry_query_client_t *client = as_registry_query_client (client_);
    if (!client)
        return -1;

    service_public_api_scope_t admission (client->public_api);
    if (!admission.acquired ())
        return -1;

    scoped_lock_t lock (client->sync);
    return client->connect_locked (endpoint_);
}

int registry_query_access_t::topology_query (
  void *client_,
  const zlink_registry_topology_filter_t *filter_,
  zlink_registry_topology_entry_t *entries_,
  size_t *count_)
{
    registry_query_client_t *client = as_registry_query_client (client_);
    if (!client || !client->dealer) {
        errno = EFAULT;
        return -1;
    }

    service_public_api_scope_t admission (client->public_api);
    if (!admission.acquired ())
        return -1;

    scoped_lock_t lock (client->sync);
    if (!client->dealer) {
        errno = EFAULT;
        return -1;
    }

    if (discovery_protocol::send_u16 (static_cast<void *> (client->dealer),
                                      discovery_protocol::msg_topology_query,
                                      ZLINK_SNDMORE)
        < 0)
        return -1;

    zlink_registry_topology_filter_t filter;
    memset (&filter, 0, sizeof (filter));
    if (filter_)
        filter = *filter_;
    if (discovery_protocol::send_frame (static_cast<void *> (client->dealer),
                                        &filter, sizeof (filter), 0)
        < 0)
        return -1;

    return recv_topology_reply_frames (static_cast<void *> (client->dealer),
                                       entries_, count_);
}

int registry_query_access_t::destroy (void **client_p_)
{
    if (!client_p_ || !*client_p_) {
        errno = EFAULT;
        return -1;
    }

    registry_query_client_t *client = as_registry_query_client (*client_p_);
    if (!client)
        return -1;

    if (!client->public_api.begin_close_or_fail_busy ())
        return -1;

    {
        scoped_lock_t lock (client->sync);
        client->destroy_locked ();
    }
    *client_p_ = NULL;
    delete client;
    return 0;
}
}
