/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "services/discovery/registry_query_access.hpp"

#include <new>
#include <string.h>
#include <vector>

#include "core/ctx.hpp"
#include "services/common/service_public_api.hpp"
#include "services/discovery/discovery_protocol.hpp"
#include "services/discovery/discovery_registry_rpc.hpp"
#include "sockets/socket_base.hpp"
#include "utils/err.hpp"

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
        if (dealer) {
            dealer->close ();
            dealer = NULL;
        }
        return zlink::discovery_registry_rpc::prepare_query_dealer (
          ctx, endpoint_, &dealer);
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

static int recv_topology_reply_frames (zlink::socket_base_t *socket_,
                                       zlink_registry_topology_entry_t *entries_,
                                       size_t *count_)
{
    if (!socket_ || !count_) {
        errno = EINVAL;
        return -1;
    }

    std::vector<zlink_registry_topology_entry_t> decoded;
    if (zlink::discovery_registry_rpc::recv_topology_reply_entries (
          socket_, &decoded)
        != 0)
        return -1;

    const size_t remote_count = decoded.size ();

    if (!entries_) {
        *count_ = remote_count;
        return 0;
    }

    if (*count_ < remote_count) {
        *count_ = remote_count;
        errno = ENOBUFS;
        return -1;
    }

    for (size_t i = 0; i < remote_count; ++i)
        entries_[i] = decoded[i];

    *count_ = remote_count;
    return 0;
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

    return recv_topology_reply_frames (client->dealer, entries_, count_);
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
