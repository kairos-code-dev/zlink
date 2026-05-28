/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "api/monitoring/monitor_api_internal.hpp"
#include "api/socket/part_helper_internal.hpp"
#include "api/spot/request_reply/service_spot_request_reply_internal.hpp"
#include "api/service/service_handle_internal.hpp"
#include "utils/err.hpp"
#include "api/service/service_api_internal.hpp"
#include "services/spot/node/spot_node.hpp"
#include "services/spot/runtime/spot_handle.hpp"
#include "services/spot/runtime/spot_runtime.hpp"
#include "services/spot/pubsub/spot_pub.hpp"
#include "services/spot/pubsub/spot_subject_access.hpp"
#include "api/message/bind_result_internal.hpp"
#include "api/core/close_result_internal.hpp"
#include "api/core/config_result_internal.hpp"
#include "api/message/connect_result_internal.hpp"
#include "api/message/recv_result_internal.hpp"
#include "utils/routing_id.hpp"

#include <new>
#include <vector>

#include "services/spot/node/spot_node_access.hpp"

namespace
{
int refresh_external_router_identity (zlink::spot_node_t *node)
{
    if (!node) {
        errno = EFAULT;
        return -1;
    }
    zlink::spot_runtime_t *runtime = zlink::spot_node_access_t::runtime (node);
    if (!runtime || !runtime->external_router)
        return 0;

    zlink_routing_id_t node_rid;
    memset (&node_rid, 0, sizeof (node_rid));
    if (node->node_routing_id (&node_rid) != 0 || node_rid.size == 0)
        return -1;
    return runtime->external_router->setsockopt (ZLINK_INTERNAL_OPT_ROUTING_ID,
                                                 node_rid.data,
                                                 node_rid.size);
}

int ensure_external_router_ready (zlink::spot_node_t *node)
{
    if (!node) {
        errno = EFAULT;
        return -1;
    }
    return 0;
}

}

int zlink_service_spot_node_refresh_external_router_identity (void *node_handle_)
{
    if (!node_handle_) {
        errno = EFAULT;
        return -1;
    }
    zlink::spot_node_t *node = static_cast<zlink::spot_node_t *> (node_handle_);
    return refresh_external_router_identity (node);
}

namespace
{

extern "C" void zlink_spot_request_reply_cleanup_spot (void *spot_);
extern "C" void zlink_timer_cleanup_spot (void *spot_);
extern "C" int zlink_spot_has_joined_or_pending_actor (void *spot_);

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

static void *create_spot_facade (
  zlink::spot_node_t *node_,
  const std::shared_ptr<spot_logical_state_t> &logical_state_)
{
    if (!node_ || !logical_state_) {
        errno = EFAULT;
        return NULL;
    }

    spot_handle_t *spot = new (std::nothrow) spot_handle_t ();
    if (!spot) {
        errno = ENOMEM;
        return NULL;
    }

    spot->node = node_;
    spot->mode = zlink::spot_node_access_t::mode (node_);
    spot->logical_state = logical_state_;
    spot->spot_routing_id = logical_state_->routing_id;
    register_spot_mode_state (spot);
    if (zlink::spot_node_access_t::try_register_spot_facade (node_, spot) != 0) {
        const int err = errno;
        erase_spot_mode_state (spot);
        delete spot;
        errno = err;
        return NULL;
    }
    if (zlink::spot_node_access_t::routed_enabled (node_)) {
        if (!zlink::spot_reqrep_internal::find_or_create_spot_state (spot)) {
            const int err = errno;
            zlink_spot_request_reply_cleanup_spot (spot);
            zlink::spot_node_access_t::unregister_spot_facade (node_, spot);
            erase_spot_mode_state (spot);
            delete spot;
            errno = err;
            return NULL;
        }
        if (ensure_external_router_ready (node_) != 0) {
            const int err = errno;
            zlink_spot_request_reply_cleanup_spot (spot);
            zlink::spot_node_access_t::unregister_spot_facade (node_, spot);
            erase_spot_mode_state (spot);
            delete spot;
            errno = err;
            return NULL;
        }
    }
    return static_cast<void *> (spot);
}
}

void *zlink_spot_new (void *node_)
{
    zlink::spot_node_t *node = zlink::spot_node_access_t::from_handle (node_);
    if (!node)
        return NULL;
    std::shared_ptr<spot_logical_state_t> state =
      zlink::spot_node_access_t::create_user_spot_state (node);
    if (!state)
        return NULL;
    return create_spot_facade (node, state);
}

zlink_config_result_t zlink_spot_node_entry_spot (void *node_,
                                                  void **spot_out_)
{
    if (spot_out_)
        *spot_out_ = NULL;
    if (!node_) {
        errno = EFAULT;
        return ZLINK_CONFIG_INVALID_HANDLE;
    }
    if (!spot_out_) {
        errno = EINVAL;
        return ZLINK_CONFIG_INVALID_ARGUMENT;
    }
    zlink::spot_node_t *node = zlink::spot_node_access_t::from_handle (node_);
    if (!node) {
        errno = EFAULT;
        return ZLINK_CONFIG_INVALID_HANDLE;
    }

    std::shared_ptr<spot_logical_state_t> state =
      zlink::spot_node_access_t::entry_spot_state (node);
    if (!state)
        return zlink::config_result_internal::from_errno (errno);
    void *spot = create_spot_facade (node, state);
    if (!spot)
        return zlink::config_result_internal::from_errno (errno);
    *spot_out_ = spot;
    return ZLINK_CONFIG_OK;
}

zlink_config_result_t zlink_spot_node_spot_lookup (
  void *node_, const zlink_routing_id_t *spot_rid_, void **spot_out_)
{
    if (!node_) {
        errno = EFAULT;
        return ZLINK_CONFIG_INVALID_HANDLE;
    }
    if (!spot_rid_ || !spot_out_) {
        errno = EINVAL;
        return ZLINK_CONFIG_INVALID_ARGUMENT;
    }
    zlink::spot_node_t *node = zlink::spot_node_access_t::from_handle (node_);
    if (!node) {
        errno = EFAULT;
        return ZLINK_CONFIG_INVALID_HANDLE;
    }
    std::shared_ptr<spot_logical_state_t> state =
      zlink::spot_node_access_t::lookup_spot_state (node, spot_rid_);
    if (!state)
        return zlink::config_result_internal::from_errno (errno);
    void *spot = create_spot_facade (node, state);
    if (!spot)
        return zlink::config_result_internal::from_errno (errno);
    *spot_out_ = spot;
    return ZLINK_CONFIG_OK;
}

zlink_config_result_t zlink_spot_node_spot_get_or_new (
  void *node_,
  const zlink_routing_id_t *spot_rid_,
  void **spot_out_,
  uint32_t *created_out_)
{
    if (spot_out_)
        *spot_out_ = NULL;
    if (created_out_)
        *created_out_ = 0;
    if (!node_) {
        errno = EFAULT;
        return ZLINK_CONFIG_INVALID_HANDLE;
    }
    if (!spot_rid_ || !spot_out_) {
        errno = EINVAL;
        return ZLINK_CONFIG_INVALID_ARGUMENT;
    }
    zlink::spot_node_t *node = zlink::spot_node_access_t::from_handle (node_);
    if (!node) {
        errno = EFAULT;
        return ZLINK_CONFIG_INVALID_HANDLE;
    }

    bool created = false;
    std::shared_ptr<spot_logical_state_t> state =
      zlink::spot_node_access_t::get_or_new_spot_state (node, spot_rid_,
                                                        &created);
    if (!state)
        return zlink::config_result_internal::from_errno (errno);

    void *spot = create_spot_facade (node, state);
    if (!spot) {
        if (created)
            zlink::spot_node_access_t::cancel_get_or_new_spot_state (node,
                                                                     state);
        return zlink::config_result_internal::from_errno (errno);
    }

    if (created
        && !zlink::spot_node_access_t::publish_get_or_new_spot_state (node,
                                                                      state)) {
        zlink_spot_destroy (&spot);
        std::shared_ptr<spot_logical_state_t> existing =
          zlink::spot_node_access_t::lookup_spot_state (node, spot_rid_);
        if (!existing)
            return zlink::config_result_internal::from_errno (errno);
        spot = create_spot_facade (node, existing);
        if (!spot)
            return zlink::config_result_internal::from_errno (errno);
        created = false;
    }

    *spot_out_ = spot;
    if (created_out_)
        *created_out_ = created ? 1u : 0u;
    return ZLINK_CONFIG_OK;
}

zlink_close_result_t zlink_spot_destroy (void **spot_p_)
{
    if (!spot_p_ || !*spot_p_) {
        errno = EFAULT;
        return ZLINK_CLOSE_INVALID_HANDLE;
    }

    spot_handle_t *spot = as_spot_handle (*spot_p_);
    if (!spot)
        return ZLINK_CLOSE_INVALID_HANDLE;

    zlink::spot_node_t *node = zlink::spot_node_access_t::from_handle (spot->node);
    if (!node)
        return ZLINK_CLOSE_INVALID_HANDLE;

    if (in_spot_node_send_ready_callback (node)) {
        errno = EBUSY;
        return ZLINK_CLOSE_BUSY;
    }
    if (zlink::spot_reqrep_internal::in_spot_request_completion_callback (spot)) {
        errno = EBUSY;
        return ZLINK_CLOSE_BUSY;
    }
    std::shared_ptr<zlink::spot_reqrep_internal::spot_request_reply_state_t> state =
      zlink::spot_reqrep_internal::try_find_spot_state (spot);
    if (zlink::spot_reqrep_internal::has_pending_spot_request_work (state))
        zlink::spot_reqrep_internal::claim_spot_completion_owner (state);
    const bool last_facade =
      zlink::spot_node_access_t::is_last_spot_facade_for_logical_state (node,
                                                                        spot);
    if (last_facade && zlink_spot_has_joined_or_pending_actor (spot)) {
        errno = EBUSY;
        return ZLINK_CLOSE_BUSY;
    }
    if (last_facade)
        zlink::spot_reqrep_internal::unregister_spot_identity (state);

    zlink::part_helper_internal::cleanup_handle (spot);
    zlink_spot_request_reply_cleanup_spot (spot);
    zlink_timer_cleanup_spot (spot);
    zlink::spot_node_access_t::unregister_spot_facade (node, spot);
    erase_spot_mode_state (spot);
    if (!last_facade)
        spot->logical_state.reset ();
    zlink::destroy_spot_handle_internal (spot);
    *spot_p_ = NULL;
    return ZLINK_CLOSE_OK;
}

void *zlink_spot_node_new (void *ctx_,
                           const zlink_spot_node_options_t *options_)
{
    if (!ctx_ || !(static_cast<zlink::ctx_t *> (ctx_))->check_tag ()) {
        errno = EFAULT;
        return NULL;
    }
    zlink_spot_node_mode_t mode = ZLINK_SPOT_NODE_MODE_ALL;
    if (options_ && options_->mode != 0)
        mode = options_->mode;
    if (mode != ZLINK_SPOT_NODE_MODE_PUBSUB
        && mode != ZLINK_SPOT_NODE_MODE_ROUTED
        && mode != ZLINK_SPOT_NODE_MODE_ALL) {
        errno = EINVAL;
        return NULL;
    }
    zlink::spot_node_t *node = static_cast<zlink::spot_node_t *> (
      zlink::spot_node_access_t::create (static_cast<zlink::ctx_t *> (ctx_),
                                         mode));
    if (!node)
        return NULL;
    if (!zlink::spot_node_access_t::entry_spot_state (node)) {
        const int err = errno;
        zlink::spot_node_access_t::destroy (node);
        zlink::spot_node_access_t::delete_handle (node);
        errno = err;
        return NULL;
    }
    return static_cast<void *> (node);
}

zlink_close_result_t zlink_spot_node_destroy (void **node_p_)
{
    if (!node_p_ || !*node_p_) {
        errno = EFAULT;
        return ZLINK_CLOSE_INVALID_HANDLE;
    }
    zlink::spot_node_t *node = zlink::spot_node_access_t::from_handle (*node_p_);
    if (!node)
        return ZLINK_CLOSE_INVALID_HANDLE;
    if (in_spot_node_send_ready_callback (node)) {
        errno = EBUSY;
        return ZLINK_CLOSE_BUSY;
    }
    if (zlink::spot_node_access_t::begin_close_or_fail_busy (node) != 0)
        return ZLINK_CLOSE_BUSY;
    clear_spot_node_handler_registration (node);
    if (zlink::spot_node_access_t::destroy (node) != 0) {
        zlink::spot_node_access_t::cancel_close (node);
        return zlink::close_result_internal::from_rc (-1);
    }
    zlink::spot_node_access_t::delete_handle (node);
    *node_p_ = NULL;
    return ZLINK_CLOSE_OK;
}

zlink_config_result_t zlink_spot_node_set_pub_bind (void *node_, const char *endpoint_)
{
    zlink::spot_node_t *node = zlink::spot_node_access_t::from_handle (node_);
    if (!node) {
        errno = EFAULT;
        return ZLINK_CONFIG_INVALID_ARGUMENT;
    }
    return zlink::config_result_internal::from_rc (
      zlink::spot_node_access_t::set_pub_bind (node, endpoint_));
}

zlink_config_result_t zlink_spot_node_set_router_bind (
  void *node_, const char *endpoint_)
{
    zlink::spot_node_t *node = zlink::spot_node_access_t::from_handle (node_);
    if (!node) {
        errno = EFAULT;
        return ZLINK_CONFIG_INVALID_ARGUMENT;
    }
    return zlink::config_result_internal::from_rc (
      zlink::spot_node_access_t::set_router_bind (node, endpoint_));
}

zlink_connect_result_t zlink_spot_node_connect_peer (void *node_, const char *peer_endpoint_)
{
    zlink::spot_node_t *node = zlink::spot_node_access_t::from_handle (node_);
    if (!node) {
        errno = EFAULT;
        return ZLINK_CONNECT_INVALID_ARGUMENT;
    }
    return zlink::connect_result_internal::from_rc (
      zlink::spot_node_access_t::connect_peer (node, peer_endpoint_));
}

zlink_connect_result_t zlink_spot_node_disconnect_peer (void *node_, const char *peer_endpoint_)
{
    zlink::spot_node_t *node = zlink::spot_node_access_t::from_handle (node_);
    if (!node) {
        errno = EFAULT;
        return ZLINK_CONNECT_INVALID_ARGUMENT;
    }
    return zlink::connect_result_internal::from_rc (
      zlink::spot_node_access_t::disconnect_peer (node, peer_endpoint_));
}

zlink_connect_result_t zlink_spot_node_disconnect_peer_rid (
  void *node_,
  const zlink_routing_id_t *target_node_rid_)
{
    zlink::spot_node_t *node = zlink::spot_node_access_t::from_handle (node_);
    if (!node) {
        errno = EFAULT;
        return ZLINK_CONNECT_INVALID_HANDLE;
    }
    const int rc =
      zlink::spot_node_access_t::disconnect_peer_rid (node, target_node_rid_);
    if (target_node_rid_)
        note_actor_spot_node_peer_disconnected (node, target_node_rid_);
    return zlink::connect_result_internal::from_rc (rc);
}

zlink_connect_result_t zlink_spot_node_connect_router_channel_peer (
  void *node_, const char *channel_name_, const char *endpoint_)
{
    zlink::spot_node_t *node = zlink::spot_node_access_t::from_handle (node_);
    if (!node) {
        errno = EFAULT;
        return ZLINK_CONNECT_INVALID_HANDLE;
    }
    return zlink::connect_result_internal::from_rc (
      zlink::spot_node_access_t::connect_router_channel_peer (
        node, channel_name_, endpoint_));
}

zlink_connect_result_t zlink_spot_node_connect_router_channel_peer_rid (
  void *node_,
  const char *channel_name_,
  const zlink_routing_id_t *peer_rid_,
  const char *endpoint_)
{
    zlink::spot_node_t *node = zlink::spot_node_access_t::from_handle (node_);
    if (!node) {
        errno = EFAULT;
        return ZLINK_CONNECT_INVALID_HANDLE;
    }
    return zlink::connect_result_internal::from_rc (
      zlink::spot_node_access_t::connect_router_channel_peer_rid (
        node, channel_name_, peer_rid_, endpoint_));
}

zlink_connect_result_t zlink_spot_node_disconnect_router_channel_peer (
  void *node_, const char *channel_name_, const char *endpoint_)
{
    zlink::spot_node_t *node = zlink::spot_node_access_t::from_handle (node_);
    if (!node) {
        errno = EFAULT;
        return ZLINK_CONNECT_INVALID_HANDLE;
    }
    return zlink::connect_result_internal::from_rc (
      zlink::spot_node_access_t::disconnect_router_channel_peer (
        node, channel_name_, endpoint_));
}

zlink_connect_result_t zlink_spot_node_disconnect_router_channel_peer_rid (
  void *node_,
  const char *channel_name_,
  const zlink_routing_id_t *peer_rid_)
{
    zlink::spot_node_t *node = zlink::spot_node_access_t::from_handle (node_);
    if (!node) {
        errno = EFAULT;
        return ZLINK_CONNECT_INVALID_HANDLE;
    }
    return zlink::connect_result_internal::from_rc (
      zlink::spot_node_access_t::disconnect_router_channel_peer_rid (
        node, channel_name_, peer_rid_));
}

zlink_config_result_t zlink_spot_node_status (void *node_,
                                                       zlink_spot_node_status_t *out_)
{
    zlink::spot_node_t *node = zlink::spot_node_access_t::from_handle (node_);
    if (!node) {
        errno = EFAULT;
        return ZLINK_CONFIG_INVALID_HANDLE;
    }
    return zlink::config_result_internal::from_rc (
      zlink::spot_node_access_t::status_snapshot (node, out_));
}

zlink_config_result_t zlink_spot_node_peers (void *node_,
                                                    const zlink_spot_node_peer_filter_t *filter_,
                                                    zlink_spot_node_peer_entry_t *entries_,
                                                    size_t *count_)
{
    zlink::spot_node_t *node = zlink::spot_node_access_t::from_handle (node_);
    if (!node) {
        errno = EFAULT;
        return ZLINK_CONFIG_INVALID_HANDLE;
    }
    std::vector<zlink_spot_node_peer_entry_t> rows;
    if (zlink::spot_node_access_t::peers_snapshot (node, filter_, &rows) != 0)
        return zlink::config_result_internal::from_rc (-1);
    return zlink::config_result_internal::from_rc (
      copy_snapshot_rows (rows, entries_, count_));
}

zlink_config_result_t zlink_spot_node_subjects (
  void *node_,
  const zlink_spot_node_subject_filter_t *filter_,
  zlink_spot_node_subject_entry_t *entries_,
  size_t *count_)
{
    zlink::spot_node_t *node = zlink::spot_node_access_t::from_handle (node_);
    if (!node) {
        errno = EFAULT;
        return ZLINK_CONFIG_INVALID_HANDLE;
    }
    std::vector<zlink_spot_node_subject_entry_t> rows;
    if (zlink::spot_node_access_t::subjects_snapshot (node, filter_, &rows)
        != 0)
        return zlink::config_result_internal::from_rc (-1);
    return zlink::config_result_internal::from_rc (
      copy_snapshot_rows (rows, entries_, count_));
}

zlink_config_result_t zlink_spot_node_internal_sockets (
  void *node_,
  const zlink_spot_node_socket_filter_t *filter_,
  zlink_spot_node_socket_entry_t *entries_,
  size_t *count_)
{
    zlink::spot_node_t *node = zlink::spot_node_access_t::from_handle (node_);
    if (!node) {
        errno = EFAULT;
        return ZLINK_CONFIG_INVALID_HANDLE;
    }
    std::vector<zlink_spot_node_socket_entry_t> rows;
    if (zlink::spot_node_access_t::internal_sockets_snapshot (node, filter_,
                                                              &rows)
        != 0)
        return zlink::config_result_internal::from_rc (-1);
    return zlink::config_result_internal::from_rc (
      copy_snapshot_rows (rows, entries_, count_));
}

zlink_config_result_t zlink_spot_node_attach_discovery (void *node_, void *discovery_)
{
    zlink::spot_node_t *node = zlink::spot_node_access_t::from_handle (node_);
    if (!node) {
        errno = EFAULT;
        return ZLINK_CONFIG_INVALID_HANDLE;
    }
    return zlink::config_result_internal::from_rc (
      zlink::spot_node_access_t::attach_discovery (node, discovery_));
}

zlink_config_result_t zlink_spot_node_attach_router_channel_discovery (
  void *node_, const char *channel_name_, void *discovery_)
{
    zlink::spot_node_t *node = zlink::spot_node_access_t::from_handle (node_);
    if (!node) {
        errno = EFAULT;
        return ZLINK_CONFIG_INVALID_HANDLE;
    }
    return zlink::config_result_internal::from_rc (
      zlink::spot_node_access_t::attach_router_channel_discovery (
        node, channel_name_, discovery_));
}

zlink_config_result_t zlink_spot_node_attach_channel_dealer (void *node_,
                                                             void *discovery_,
                                                             void *dealer_)
{
    zlink::spot_node_t *node = zlink::spot_node_access_t::from_handle (node_);
    if (!node) {
        errno = EFAULT;
        return ZLINK_CONFIG_INVALID_HANDLE;
    }
    return zlink::config_result_internal::from_rc (
      zlink::spot_node_access_t::attach_channel_dealer (
        node, discovery_, dealer_));
}

zlink_config_result_t zlink_spot_node_attach_channel_dealer_manual (
  void *node_,
  const char *channel_name_,
  void *dealer_)
{
    zlink::spot_node_t *node = zlink::spot_node_access_t::from_handle (node_);
    if (!node) {
        errno = EFAULT;
        return ZLINK_CONFIG_INVALID_HANDLE;
    }
    return zlink::config_result_internal::from_rc (
      zlink::spot_node_access_t::attach_channel_dealer_manual (
        node, channel_name_, dealer_));
}

zlink_config_result_t zlink_spot_node_attach_pub_ingress (void *node_,
                                                          void *pub_)
{
    zlink::spot_node_t *node = zlink::spot_node_access_t::from_handle (node_);
    if (!node) {
        errno = EFAULT;
        return ZLINK_CONFIG_INVALID_HANDLE;
    }
    if (!pub_) {
        errno = EFAULT;
        return ZLINK_CONFIG_INVALID_HANDLE;
    }
    return zlink::config_result_internal::from_rc (
      zlink::spot_node_access_t::attach_pub_ingress (node, pub_));
}
