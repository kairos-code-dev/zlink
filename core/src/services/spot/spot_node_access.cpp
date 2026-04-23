/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "services/spot/spot_node_access.hpp"

#include <new>

#include "api/service_handle_internal.hpp"
#include "api/socket_api_internal.hpp"
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
    register_spot_node_mode_state (node);
    return node;
}

ctx_t *spot_node_access_t::ctx (spot_node_t *node_)
{
    return node_ ? node_->_ctx : NULL;
}

mutex_t &spot_node_access_t::sync (spot_node_t *node_)
{
    return node_->_sync;
}

spot_node_t *spot_node_access_t::from_handle (void *node_)
{
    if (!node_ || !is_registered_spot_node_handle (node_)) {
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
    erase_spot_node_mode_state (node_);
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

std::string spot_node_access_t::summary_service_name (spot_node_t *node_)
{
    if (!node_)
        return std::string ();
    return node_->summary_service_name ();
}

socket_base_t *spot_node_access_t::select_service_router (
  spot_node_t *node_,
  const std::string &service_name_)
{
    return node_ ? node_->select_service_router (service_name_) : NULL;
}

socket_base_t *spot_node_access_t::service_pub_socket (
  spot_node_t *node_,
  const std::string &service_name_)
{
    return node_ ? node_->service_pub_socket (service_name_) : NULL;
}

int spot_node_access_t::service_subscribe_recv (
  spot_node_t *node_,
  zlink_routing_id_t *source_rid_out_,
  zlink_msg_t **parts_out_,
  size_t *part_count_out_,
  char *service_name_out_,
  size_t *service_name_len_out_,
  char *topic_id_out_,
  size_t *topic_id_len_out_,
  zlink_recv_flags_t flags_)
{
    return node_
             ? node_->service_subscribe_recv (
                 source_rid_out_, parts_out_, part_count_out_, service_name_out_,
                 service_name_len_out_, topic_id_out_, topic_id_len_out_,
                 flags_)
             : -1;
}

int spot_node_access_t::service_subscription_event_recv (
  spot_node_t *node_,
  zlink_routing_id_t *source_rid_out_,
  int *subscribed_out_,
  char *service_name_out_,
  size_t *service_name_len_out_,
  char *topic_id_out_,
  size_t *topic_id_len_out_,
  zlink_recv_flags_t flags_)
{
    return node_
             ? node_->service_subscription_event_recv (
                 source_rid_out_, subscribed_out_, service_name_out_,
                 service_name_len_out_, topic_id_out_, topic_id_len_out_,
                 flags_)
             : -1;
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

int spot_node_access_t::attach_channel_dealer (spot_node_t *node_,
                                               void *discovery_,
                                               void *dealer_)
{
    if (!node_ || !discovery_ || !dealer_) {
        errno = EFAULT;
        return -1;
    }
    discovery_t *discovery = discovery_access_t::from_handle (discovery_);
    socket_base_t *dealer = try_as_socket (dealer_);
    return discovery && dealer ? node_->attach_channel_dealer (discovery, dealer)
                               : -1;
}

int spot_node_access_t::attach_channel_dealer_manual (spot_node_t *node_,
                                                      const char *channel_name_,
                                                      void *dealer_)
{
    socket_base_t *dealer = try_as_socket (dealer_);
    return node_ && dealer
             ? node_->attach_channel_dealer_manual (channel_name_, dealer)
             : -1;
}

int spot_node_access_t::attach_pub_ingress (spot_node_t *node_, void *pub_)
{
    socket_base_t *pub = try_as_socket (pub_);
    return node_ && pub ? node_->attach_pub_ingress (pub) : -1;
}

int spot_node_access_t::try_register_spot_facade (spot_node_t *node_,
                                                  spot_handle_t *spot_)
{
    if (!node_ || !spot_) {
        errno = EFAULT;
        return -1;
    }
    return node_->try_register_spot_facade (spot_);
}

void spot_node_access_t::unregister_spot_facade (spot_node_t *node_,
                                                 spot_handle_t *spot_)
{
    if (node_ && spot_)
        node_->unregister_spot_facade (spot_);
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

void spot_node_access_t::track_owned_socket (spot_node_t *node_,
                                             socket_base_t *socket_)
{
    if (node_ && socket_)
        node_->track_owned_socket (socket_);
}

void spot_node_access_t::untrack_owned_socket (spot_node_t *node_,
                                               const socket_base_t *socket_)
{
    if (node_ && socket_)
        node_->untrack_owned_socket (socket_);
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

int spot_node_access_t::send_internal_subscription_update (
  spot_node_t *node_,
  const std::string &raw_filter_,
  bool subscribe_)
{
    if (!node_) {
        errno = EFAULT;
        return -1;
    }
    return node_->send_subscription_update (raw_filter_, subscribe_);
}
}
