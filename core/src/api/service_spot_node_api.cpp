/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "api/monitor_api_internal.hpp"
#include "utils/err.hpp"
#include "api/service_api_internal.hpp"
#include "api/zlink_testing.hpp"

#include <new>
#include <vector>

#include "services/spot/spot_node_access.hpp"

namespace
{
extern "C" void zlink_spot_request_reply_cleanup_spot (void *spot_);

template <typename Row>
static int copy_snapshot_rows (const std::vector<Row> &rows_,
                               Row *entries_,
                               size_t *count_)
{
    if (!count_) {
        errno = EINVAL;
        return -1;
    }

    if (!entries_) {
        *count_ = rows_.size ();
        return 0;
    }

    if (*count_ < rows_.size ()) {
        *count_ = rows_.size ();
        errno = ENOBUFS;
        return -1;
    }

    for (size_t i = 0; i < rows_.size (); ++i)
        entries_[i] = rows_[i];

    *count_ = rows_.size ();
    return 0;
}
}

void *zlink_spot_new (void *node_)
{
    zlink::spot_node_t *node = zlink::spot_node_access_t::from_handle (node_);
    if (!node)
        return NULL;

    spot_handle_t *spot = new (std::nothrow) spot_handle_t ();
    if (!spot) {
        errno = ENOMEM;
        return NULL;
    }

    spot->node = node;
    if (!ensure_spot_pub (spot)) {
        const int err = errno;
        delete spot;
        errno = err;
        return NULL;
    }
    return static_cast<void *> (spot);
}

int zlink_spot_destroy (void **spot_p_)
{
    if (!spot_p_ || !*spot_p_) {
        errno = EFAULT;
        return -1;
    }

    spot_handle_t *spot = as_spot_handle (*spot_p_);
    if (!spot)
        return -1;

    zlink::spot_node_t *node = zlink::spot_node_access_t::from_handle (spot->node);
    if (!node)
        return -1;

    if (in_spot_node_send_ready_callback (node)
        || in_spot_node_monitor_callback (node)) {
        errno = EBUSY;
        return -1;
    }

    zlink_spot_request_reply_cleanup_spot (spot);
    zlink::destroy_spot_handle_for_testing (spot);
    *spot_p_ = NULL;
    return 0;
}

void *zlink_spot_node_new (void *ctx_)
{
    if (!ctx_ || !(static_cast<zlink::ctx_t *> (ctx_))->check_tag ()) {
        errno = EFAULT;
        return NULL;
    }
    zlink::spot_node_t *node = static_cast<zlink::spot_node_t *> (
      zlink::spot_node_access_t::create (static_cast<zlink::ctx_t *> (ctx_)));
    if (!node)
        return NULL;
    return static_cast<void *> (node);
}

int zlink_spot_node_destroy (void **node_p_)
{
    if (!node_p_ || !*node_p_) {
        errno = EFAULT;
        return -1;
    }
    zlink::spot_node_t *node = zlink::spot_node_access_t::from_handle (*node_p_);
    if (!node)
        return -1;
    if (in_spot_node_send_ready_callback (node)
        || in_spot_node_monitor_callback (node)) {
        errno = EBUSY;
        return -1;
    }
    if (zlink::spot_node_access_t::begin_close_or_fail_busy (node) != 0)
        return -1;
    clear_spot_node_handler_registration (node);
    if (zlink::spot_node_access_t::destroy (node) != 0) {
        zlink::spot_node_access_t::cancel_close (node);
        return -1;
    }
    zlink::spot_node_access_t::delete_handle (node);
    *node_p_ = NULL;
    return 0;
}

int zlink_spot_node_bind (void *node_, const char *endpoint_)
{
    zlink::spot_node_t *node = zlink::spot_node_access_t::from_handle (node_);
    return node ? zlink::spot_node_access_t::bind (node, endpoint_) : -1;
}

int zlink_spot_node_connect_peer (void *node_, const char *peer_endpoint_)
{
    zlink::spot_node_t *node = zlink::spot_node_access_t::from_handle (node_);
    return node ? zlink::spot_node_access_t::connect_peer (node, peer_endpoint_)
                : -1;
}

int zlink_spot_node_disconnect_peer (void *node_, const char *peer_endpoint_)
{
    zlink::spot_node_t *node = zlink::spot_node_access_t::from_handle (node_);
    return node
             ? zlink::spot_node_access_t::disconnect_peer (node, peer_endpoint_)
             : -1;
}

int zlink_spot_node_status_snapshot (void *node_,
                                     zlink_spot_node_status_t *out_)
{
    zlink::spot_node_t *node = zlink::spot_node_access_t::from_handle (node_);
    return node ? zlink::spot_node_access_t::status_snapshot (node, out_) : -1;
}

int zlink_spot_node_peers_snapshot (void *node_,
                                    zlink_spot_node_peer_entry_t *entries_,
                                    size_t *count_)
{
    zlink::spot_node_t *node = zlink::spot_node_access_t::from_handle (node_);
    if (!node)
        return -1;
    std::vector<zlink_spot_node_peer_entry_t> rows;
    if (zlink::spot_node_access_t::peers_snapshot (node, NULL, &rows) != 0)
        return -1;
    return copy_snapshot_rows (rows, entries_, count_);
}

int zlink_spot_node_peers_query (void *node_,
                                 const zlink_spot_node_peer_filter_t *filter_,
                                 zlink_spot_node_peer_entry_t *entries_,
                                 size_t *count_)
{
    zlink::spot_node_t *node = zlink::spot_node_access_t::from_handle (node_);
    if (!node)
        return -1;
    std::vector<zlink_spot_node_peer_entry_t> rows;
    if (zlink::spot_node_access_t::peers_snapshot (node, filter_, &rows) != 0)
        return -1;
    return copy_snapshot_rows (rows, entries_, count_);
}

int zlink_spot_node_subjects_snapshot (
  void *node_,
  const zlink_spot_node_subject_filter_t *filter_,
  zlink_spot_node_subject_entry_t *entries_,
  size_t *count_)
{
    zlink::spot_node_t *node = zlink::spot_node_access_t::from_handle (node_);
    if (!node)
        return -1;
    std::vector<zlink_spot_node_subject_entry_t> rows;
    if (zlink::spot_node_access_t::subjects_snapshot (node, filter_, &rows)
        != 0)
        return -1;
    return copy_snapshot_rows (rows, entries_, count_);
}

int zlink_spot_node_attach_discovery (void *node_, void *discovery_)
{
    zlink::spot_node_t *node = zlink::spot_node_access_t::from_handle (node_);
    return node ? zlink::spot_node_access_t::attach_discovery (
                    node, discovery_)
                : -1;
}
