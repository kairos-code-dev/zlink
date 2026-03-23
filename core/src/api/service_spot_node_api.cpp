/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "api/monitor_api_internal.hpp"
#include "utils/err.hpp"
#include "api/service_api_internal.hpp"
#include "api/zlink_testing.hpp"

#include <new>
#include <vector>

#include "services/discovery/discovery.hpp"

namespace
{
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

void *zlink_spot_new (void *ctx_, const char *service_name_)
{
    if (!ctx_ || !(static_cast<zlink::ctx_t *> (ctx_))->check_tag ()) {
        errno = EFAULT;
        return NULL;
    }
    if (!service_name_ || service_name_[0] == '\0') {
        errno = EINVAL;
        return NULL;
    }

    zlink::spot_node_t *node =
      new (std::nothrow)
        zlink::spot_node_t (static_cast<zlink::ctx_t *> (ctx_), service_name_);
    if (!node) {
        errno = ENOMEM;
        return NULL;
    }
    if (!node->check_tag ()) {
        delete node;
        errno = EINVAL;
        return NULL;
    }

    spot_handle_t *spot = new (std::nothrow) spot_handle_t ();
    if (!spot) {
        delete node;
        errno = ENOMEM;
        return NULL;
    }

    spot->node = node;
    register_spot_mode_state (spot);
    register_spot_node_mode_state (node);
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

    zlink::spot_node_t *node = spot->node;
    if (!node || !node->check_tag ()) {
        errno = EFAULT;
        return -1;
    }

    if (in_spot_node_send_ready_callback (node)
        || in_spot_node_monitor_callback (node)) {
        errno = EBUSY;
        return -1;
    }

    erase_spot_mode_state (spot);
    zlink::destroy_spot_handle_for_testing (spot);
    *spot_p_ = NULL;

    void *node_handle = node;
    return zlink_spot_node_destroy (&node_handle);
}

void *zlink_spot_node_new (void *ctx_, const char *service_name_)
{
    if (!ctx_ || !(static_cast<zlink::ctx_t *> (ctx_))->check_tag ()) {
        errno = EFAULT;
        return NULL;
    }
    if (!service_name_ || service_name_[0] == '\0') {
        errno = EINVAL;
        return NULL;
    }
    zlink::spot_node_t *node =
      new (std::nothrow)
        zlink::spot_node_t (static_cast<zlink::ctx_t *> (ctx_), service_name_);
    if (!node) {
        errno = ENOMEM;
        return NULL;
    }
    if (!node->check_tag ()) {
        delete node;
        errno = EINVAL;
        return NULL;
    }
    register_spot_node_mode_state (node);
    return static_cast<void *> (node);
}

int zlink_spot_node_destroy (void **node_p_)
{
    if (!node_p_ || !*node_p_) {
        errno = EFAULT;
        return -1;
    }
    zlink::spot_node_t *node = static_cast<zlink::spot_node_t *> (*node_p_);
    if (!node->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    if (in_spot_node_send_ready_callback (node)
        || in_spot_node_monitor_callback (node)) {
        errno = EBUSY;
        return -1;
    }
    if (!node->public_api_guard ().begin_close_or_fail_busy ())
        return -1;
    clear_spot_node_handler_registration (node);
    if (node->destroy () != 0) {
        node->public_api_guard ().cancel_close ();
        return -1;
    }
    erase_spot_node_mode_state (node);
    delete node;
    *node_p_ = NULL;
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

int zlink_spot_node_connect_peer (void *node_, const char *peer_endpoint_)
{
    if (!node_)
        return -1;
    zlink::spot_node_t *node = static_cast<zlink::spot_node_t *> (node_);
    if (!node->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    return node->connect_peer_pub (peer_endpoint_);
}

int zlink_spot_node_disconnect_peer (void *node_, const char *peer_endpoint_)
{
    if (!node_)
        return -1;
    zlink::spot_node_t *node = static_cast<zlink::spot_node_t *> (node_);
    if (!node->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    return node->disconnect_peer_pub (peer_endpoint_);
}

int zlink_spot_node_status_snapshot (void *node_,
                                     zlink_spot_node_status_t *out_)
{
    if (!node_) {
        errno = EFAULT;
        return -1;
    }
    zlink::spot_node_t *node = static_cast<zlink::spot_node_t *> (node_);
    if (!node->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    zlink::service_public_api_scope_t admission (node->public_api_guard ());
    if (!admission.acquired ())
        return -1;
    return node->snapshot_status (out_);
}

int zlink_spot_node_peers_snapshot (void *node_,
                                    zlink_spot_node_peer_entry_t *entries_,
                                    size_t *count_)
{
    if (!node_) {
        errno = EFAULT;
        return -1;
    }
    zlink::spot_node_t *node = static_cast<zlink::spot_node_t *> (node_);
    if (!node->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    zlink::service_public_api_scope_t admission (node->public_api_guard ());
    if (!admission.acquired ())
        return -1;
    std::vector<zlink_spot_node_peer_entry_t> rows;
    if (node->snapshot_peers (NULL, &rows) != 0)
        return -1;
    return copy_snapshot_rows (rows, entries_, count_);
}

int zlink_spot_node_peers_query (void *node_,
                                 const zlink_spot_node_peer_filter_t *filter_,
                                 zlink_spot_node_peer_entry_t *entries_,
                                 size_t *count_)
{
    if (!node_) {
        errno = EFAULT;
        return -1;
    }
    zlink::spot_node_t *node = static_cast<zlink::spot_node_t *> (node_);
    if (!node->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    zlink::service_public_api_scope_t admission (node->public_api_guard ());
    if (!admission.acquired ())
        return -1;
    std::vector<zlink_spot_node_peer_entry_t> rows;
    if (node->snapshot_peers (filter_, &rows) != 0)
        return -1;
    return copy_snapshot_rows (rows, entries_, count_);
}

int zlink_spot_node_subjects_snapshot (
  void *node_,
  const zlink_spot_node_subject_filter_t *filter_,
  zlink_spot_node_subject_entry_t *entries_,
  size_t *count_)
{
    if (!node_) {
        errno = EFAULT;
        return -1;
    }
    zlink::spot_node_t *node = static_cast<zlink::spot_node_t *> (node_);
    if (!node->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    zlink::service_public_api_scope_t admission (node->public_api_guard ());
    if (!admission.acquired ())
        return -1;
    std::vector<zlink_spot_node_subject_entry_t> rows;
    if (node->snapshot_subjects (filter_, &rows) != 0)
        return -1;
    return copy_snapshot_rows (rows, entries_, count_);
}

int zlink_spot_node_attach_discovery (void *node_, void *discovery_)
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
    return node->attach_discovery (disc);
}
