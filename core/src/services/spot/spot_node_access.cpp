/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "services/spot/spot_node_access.hpp"

#include <new>

#include "services/discovery/discovery_access.hpp"
#include "services/spot/spot_node.hpp"
#include "services/spot/spot_pub.hpp"

namespace zlink
{
void *spot_node_access_t::create (ctx_t *ctx_)
{
    spot_node_t *node = new (std::nothrow) spot_node_t (ctx_);
    if (!node) {
        errno = ENOMEM;
        return NULL;
    }
    if (!node->check_tag ()) {
        delete node;
        errno = EINVAL;
        return NULL;
    }
    return node;
}

ctx_t *spot_node_access_t::ctx (spot_node_t *node_)
{
    return node_ ? node_->_ctx : NULL;
}

spot_node_t *spot_node_access_t::from_handle (void *node_)
{
    if (!node_) {
        errno = EFAULT;
        return NULL;
    }

    spot_node_t *node = static_cast<spot_node_t *> (node_);
    if (!node->check_tag ()) {
        errno = EFAULT;
        return NULL;
    }
    return node;
}

int spot_node_access_t::bind (spot_node_t *node_, const char *endpoint_)
{
    return node_ ? node_->bind (endpoint_) : -1;
}

int spot_node_access_t::connect_peer (spot_node_t *node_,
                                      const char *peer_endpoint_)
{
    return node_ ? node_->connect_peer_pub (peer_endpoint_) : -1;
}

int spot_node_access_t::disconnect_peer (spot_node_t *node_,
                                         const char *peer_endpoint_)
{
    return node_ ? node_->disconnect_peer_pub (peer_endpoint_) : -1;
}

int spot_node_access_t::set_node_option (spot_node_t *node_,
                                         zlink_spot_node_option_t option_,
                                         const void *optval_,
                                         size_t optvallen_)
{
    return node_ ? node_->set_node_option (option_, optval_, optvallen_) : -1;
}

int spot_node_access_t::get_node_option (spot_node_t *node_,
                                         zlink_spot_node_option_t option_,
                                         void *optval_,
                                         size_t *optvallen_)
{
    return node_ ? node_->get_node_option (option_, optval_, optvallen_) : -1;
}

int spot_node_access_t::begin_close_or_fail_busy (spot_node_t *node_)
{
    return node_ && node_->public_api_guard ().begin_close_or_fail_busy () ? 0
                                                                           : -1;
}

void spot_node_access_t::cancel_close (spot_node_t *node_)
{
    if (node_)
        node_->public_api_guard ().cancel_close ();
}

int spot_node_access_t::destroy (spot_node_t *node_)
{
    return node_ ? node_->destroy () : -1;
}

void spot_node_access_t::delete_handle (spot_node_t *node_)
{
    delete node_;
}

int spot_node_access_t::status_snapshot (spot_node_t *node_,
                                         zlink_spot_node_status_t *out_)
{
    if (!node_) {
        errno = EFAULT;
        return -1;
    }
    service_public_api_scope_t admission (node_->public_api_guard ());
    if (!admission.acquired ())
        return -1;
    return node_->snapshot_status (out_);
}

int spot_node_access_t::peers_snapshot (
  spot_node_t *node_,
  const zlink_spot_node_peer_filter_t *filter_,
  std::vector<zlink_spot_node_peer_entry_t> *out_)
{
    if (!node_) {
        errno = EFAULT;
        return -1;
    }
    service_public_api_scope_t admission (node_->public_api_guard ());
    if (!admission.acquired ())
        return -1;
    return node_->snapshot_peers (filter_, out_);
}

int spot_node_access_t::subjects_snapshot (
  spot_node_t *node_,
  const zlink_spot_node_subject_filter_t *filter_,
  std::vector<zlink_spot_node_subject_entry_t> *out_)
{
    if (!node_) {
        errno = EFAULT;
        return -1;
    }
    service_public_api_scope_t admission (node_->public_api_guard ());
    if (!admission.acquired ())
        return -1;
    return node_->snapshot_subjects (filter_, out_);
}

int spot_node_access_t::attach_discovery (spot_node_t *node_, void *discovery_)
{
    if (!node_ || !discovery_) {
        errno = EFAULT;
        return -1;
    }
    discovery_t *discovery = discovery_access_t::from_handle (discovery_);
    return discovery ? node_->attach_discovery (discovery) : -1;
}

void *spot_node_access_t::monitor_open (spot_node_t *node_,
                                        zlink_spot_role_t role_,
                                        int events_,
                                        void **snapshot_subject_out_,
                                        spot_node_monitor_subject_t *subject_kind_out_)
{
    if (snapshot_subject_out_)
        *snapshot_subject_out_ = NULL;
    if (subject_kind_out_)
        *subject_kind_out_ = spot_node_monitor_subject_none;

    if (!node_) {
        errno = EFAULT;
        return NULL;
    }

    service_public_api_scope_t admission (node_->public_api_guard ());
    if (!admission.acquired ())
        return NULL;

    if (role_ == ZLINK_SPOT_ROLE_PUB) {
        spot_pub_t *pub = node_->ensure_default_pub ();
        if (!pub)
            return NULL;
        if (snapshot_subject_out_)
            *snapshot_subject_out_ = pub;
        if (subject_kind_out_)
            *subject_kind_out_ = spot_node_monitor_subject_pub;
        return pub->monitor_open (events_);
    }

    if (role_ == ZLINK_SPOT_ROLE_SUB) {
        spot_internal_receiver_t *receiver = node_->ensure_internal_receiver ();
        if (!receiver)
            return NULL;
        if (snapshot_subject_out_)
            *snapshot_subject_out_ = receiver;
        if (subject_kind_out_)
            *subject_kind_out_ = spot_node_monitor_subject_internal_receiver;
        return receiver->monitor_open (events_);
    }

    errno = EINVAL;
    return NULL;
}

spot_runtime_t *spot_node_access_t::runtime (spot_node_t *node_)
{
    return node_ ? node_->runtime () : NULL;
}

spot_internal_receiver_t *
spot_node_access_t::ensure_internal_receiver (spot_node_t *node_)
{
    return node_ ? node_->ensure_internal_receiver () : NULL;
}

spot_internal_receiver_t *spot_node_access_t::internal_receiver (spot_node_t *node_)
{
    return node_ ? node_->internal_receiver () : NULL;
}

void spot_node_access_t::wake_control_task (spot_node_t *node_)
{
    if (node_)
        node_->wake_control_task ();
}

void spot_node_access_t::schedule_subscription_replay (spot_node_t *node_)
{
    if (node_)
        node_->schedule_subscription_replay ();
}
}
