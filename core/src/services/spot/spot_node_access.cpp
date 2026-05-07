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
ctx_t *spot_node_t::ctx () const
{
    return _ctx;
}

mutex_t &spot_node_t::sync ()
{
    return _sync;
}

socket_base_t *spot_node_t::create_socket (int socket_type_) const
{
    if (!_ctx) {
        errno = EFAULT;
        return NULL;
    }
    return _ctx->create_socket (socket_type_);
}

std::set<actor_handle_t *> &spot_node_t::actor_handles ()
{
    return _actor_state.actor_handles;
}

std::map<std::string, actor_handle_t *> &spot_node_t::actors_by_id ()
{
    return _actor_state.actors_by_id;
}

uint64_t &spot_node_t::next_actor_generation ()
{
    return _actor_state.next_generation;
}

void spot_node_t::snapshot_active_peer_endpoints (
  std::set<std::string> *out_) const
{
    if (!out_)
        return;

    out_->clear ();
    scoped_lock_t lock (_sync);
    *out_ = _peer_state.active_endpoints;
}

void spot_node_t::snapshot_tls_client_config (std::string *ca_out_,
                                              std::string *host_out_,
                                              int *trust_system_out_) const
{
    if (ca_out_)
        ca_out_->clear ();
    if (host_out_)
        host_out_->clear ();
    if (trust_system_out_)
        *trust_system_out_ = 0;

    scoped_lock_t lock (_sync);
    if (ca_out_)
        *ca_out_ = _tls_state.tls_ca;
    if (host_out_)
        *host_out_ = _tls_state.tls_hostname;
    if (trust_system_out_)
        *trust_system_out_ = _tls_state.tls_trust_system;
}

void spot_node_t::snapshot_tls_server_config (std::string *cert_out_,
                                              std::string *key_out_) const
{
    if (cert_out_)
        cert_out_->clear ();
    if (key_out_)
        key_out_->clear ();

    scoped_lock_t lock (_sync);
    if (cert_out_)
        *cert_out_ = _tls_state.tls_cert;
    if (key_out_)
        *key_out_ = _tls_state.tls_key;
}

void spot_node_t::mark_bound_endpoint_and_server_tls_locked (
  const std::string &bound_endpoint_)
{
    scoped_lock_t lock (_sync);
    _endpoint_state.bound_endpoint = bound_endpoint_;
    _tls_state.server_tls_locked = true;
}

void spot_node_t::mark_mesh_client_tls_locked ()
{
    scoped_lock_t lock (_sync);
    _tls_state.mesh_client_tls_locked = true;
}

int spot_node_t::close_owned_socket (socket_base_t *&socket_, int timeout_ms_)
{
    if (!socket_) {
        errno = EFAULT;
        return -1;
    }
    return _lifecycle.close_socket (socket_, timeout_ms_);
}

int spot_node_t::close_owned_socket_and_wait (socket_base_t *&socket_,
                                              int timeout_ms_)
{
    if (!socket_) {
        errno = EFAULT;
        return -1;
    }
    return _lifecycle.close_socket_and_wait (socket_, timeout_ms_);
}

void *spot_node_access_t::create (ctx_t *ctx_, zlink_spot_node_mode_t mode_)
{
    spot_node_t *node = new (std::nothrow) spot_node_t (ctx_, mode_);
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
    return node_ ? node_->ctx () : NULL;
}

mutex_t &spot_node_access_t::sync (spot_node_t *node_)
{
    return node_->sync ();
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

int spot_node_access_t::disconnect_peer_rid (
  spot_node_t *node_,
  const zlink_routing_id_t *target_node_rid_)
{
    return node_ ? node_->disconnect_peer_pub_rid (target_node_rid_) : -1;
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

bool spot_node_access_t::has_active_peers (spot_node_t *node_)
{
    return node_ && node_->has_active_peers ();
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

int spot_node_access_t::internal_sockets_snapshot (
  spot_node_t *node_,
  const zlink_spot_node_socket_snapshot_filter_t *filter_,
  std::vector<zlink_spot_node_socket_snapshot_entry_t> *out_)
{
    if (!node_) {
        errno = EFAULT;
        return -1;
    }
    service_public_api_scope_t admission (node_->public_api_guard ());
    if (!admission.acquired ())
        return -1;
    return node_->snapshot_internal_sockets (filter_, out_);
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

bool spot_node_access_t::is_last_spot_facade_for_logical_state (
  spot_node_t *node_, spot_handle_t *spot_)
{
    return node_ ? node_->is_last_spot_facade_for_logical_state (spot_) : true;
}

std::shared_ptr<spot_logical_state_t>
spot_node_access_t::create_user_spot_state (spot_node_t *node_)
{
    return node_ ? node_->create_user_spot_state ()
                 : std::shared_ptr<spot_logical_state_t> ();
}

std::shared_ptr<spot_logical_state_t>
spot_node_access_t::entry_spot_state (spot_node_t *node_)
{
    return node_ ? node_->entry_spot_state ()
                 : std::shared_ptr<spot_logical_state_t> ();
}

std::shared_ptr<spot_logical_state_t>
spot_node_access_t::lookup_spot_state (spot_node_t *node_,
                                       const zlink_routing_id_t *spot_rid_)
{
    return node_ ? node_->lookup_spot_state (spot_rid_)
                 : std::shared_ptr<spot_logical_state_t> ();
}

int spot_node_access_t::update_spot_routing_id (spot_node_t *node_,
                                                spot_handle_t *spot_,
                                                const void *data_,
                                                size_t size_)
{
    if (!node_) {
        errno = EFAULT;
        return -1;
    }
    return node_->update_spot_routing_id (spot_, data_, size_);
}

spot_runtime_t *spot_node_access_t::runtime (spot_node_t *node_)
{
    return node_ ? node_->runtime () : NULL;
}

zlink_spot_node_mode_t spot_node_access_t::mode (spot_node_t *node_)
{
    return node_ ? node_->spot_node_mode () : ZLINK_SPOT_NODE_MODE_ALL;
}

bool spot_node_access_t::pubsub_enabled (spot_node_t *node_)
{
    return node_ && node_->pubsub_enabled ();
}

bool spot_node_access_t::routed_enabled (spot_node_t *node_)
{
    return node_ && node_->routed_enabled ();
}

bool spot_node_access_t::is_shutting_down (spot_node_t *node_)
{
    return !node_ || node_->is_shutting_down ();
}

socket_base_t *spot_node_access_t::create_socket (spot_node_t *node_,
                                                  int socket_type_)
{
    if (!node_) {
        errno = EFAULT;
        return NULL;
    }
    return node_->create_socket (socket_type_);
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

std::set<actor_handle_t *> &spot_node_access_t::actor_handles (
  spot_node_t *node_)
{
    return node_->actor_handles ();
}

std::map<std::string, actor_handle_t *> &spot_node_access_t::actors_by_id (
  spot_node_t *node_)
{
    return node_->actors_by_id ();
}

uint64_t &spot_node_access_t::next_actor_generation (spot_node_t *node_)
{
    return node_->next_actor_generation ();
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

void spot_node_access_t::snapshot_active_peer_endpoints (
  spot_node_t *node_, std::set<std::string> *out_)
{
    if (!out_)
        return;

    out_->clear ();
    if (!node_)
        return;

    node_->snapshot_active_peer_endpoints (out_);
}

void spot_node_access_t::snapshot_tls_client_config (spot_node_t *node_,
                                                     std::string *ca_out_,
                                                     std::string *host_out_,
                                                     int *trust_system_out_)
{
    if (ca_out_)
        ca_out_->clear ();
    if (host_out_)
        host_out_->clear ();
    if (trust_system_out_)
        *trust_system_out_ = 0;
    if (!node_)
        return;

    node_->snapshot_tls_client_config (ca_out_, host_out_, trust_system_out_);
}

void spot_node_access_t::snapshot_tls_server_config (spot_node_t *node_,
                                                     std::string *cert_out_,
                                                     std::string *key_out_)
{
    if (cert_out_)
        cert_out_->clear ();
    if (key_out_)
        key_out_->clear ();
    if (!node_)
        return;

    node_->snapshot_tls_server_config (cert_out_, key_out_);
}

void spot_node_access_t::mark_bound_endpoint_and_server_tls_locked (
  spot_node_t *node_, const std::string &bound_endpoint_)
{
    if (!node_)
        return;
    node_->mark_bound_endpoint_and_server_tls_locked (bound_endpoint_);
}

void spot_node_access_t::mark_mesh_client_tls_locked (spot_node_t *node_)
{
    if (!node_)
        return;
    node_->mark_mesh_client_tls_locked ();
}

int spot_node_access_t::apply_tls_server (spot_node_t *node_,
                                          socket_base_t *socket_,
                                          const std::string &cert_,
                                          const std::string &key_)
{
    (void) node_;
    return spot_node_t::apply_tls_server (socket_, cert_, key_);
}

int spot_node_access_t::apply_tls_client (spot_node_t *node_,
                                          socket_base_t *socket_,
                                          const std::string &ca_cert_,
                                          const std::string &hostname_,
                                          int trust_system_)
{
    (void) node_;
    return spot_node_t::apply_tls_client (socket_, ca_cert_, hostname_,
                                          trust_system_);
}

bool spot_node_access_t::recv_ctrl_reply (socket_base_t *socket_,
                                          int *out_errno_)
{
    return spot_node_t::recv_ctrl_reply (socket_, out_errno_);
}

int spot_node_access_t::close_owned_socket (spot_node_t *node_,
                                            socket_base_t *&socket_,
                                            int timeout_ms_)
{
    if (!node_ || !socket_) {
        errno = EFAULT;
        return -1;
    }
    return node_->close_owned_socket (socket_, timeout_ms_);
}

int spot_node_access_t::close_owned_socket_and_wait (spot_node_t *node_,
                                                     socket_base_t *&socket_,
                                                     int timeout_ms_)
{
    if (!node_ || !socket_) {
        errno = EFAULT;
        return -1;
    }
    return node_->close_owned_socket_and_wait (socket_, timeout_ms_);
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
