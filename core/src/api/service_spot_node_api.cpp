/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "api/monitor_api_internal.hpp"
#include "api/part_helper_internal.hpp"
#include "api/service_spot_request_reply_internal.hpp"
#include "utils/err.hpp"
#include "api/service_api_internal.hpp"
#include "services/spot/spot_node.hpp"
#include "services/spot/spot_runtime.hpp"
#include "services/spot/spot_pub.hpp"
#include "api/zlink_testing.hpp"
#include "api/bind_result_internal.hpp"
#include "api/close_result_internal.hpp"
#include "api/config_result_internal.hpp"
#include "api/connect_result_internal.hpp"
#include "api/recv_result_internal.hpp"

#include <new>
#include <vector>

#include "services/spot/spot_node_access.hpp"

namespace
{
int ensure_spot_routed_mesh_subscription (zlink::spot_node_t *node)
{
    if (!node)
        return -1;

    zlink::spot_runtime_t *runtime = zlink::spot_node_access_t::runtime (node);
    if (!runtime)
        return -1;

    zlink::spot_pub_t *node_pub = node->ensure_default_pub ();
    if (!node_pub)
        return -1;

    zlink_routing_id_t node_rid;
    memset (&node_rid, 0, sizeof (node_rid));
    if (node_pub->routing_id (&node_rid) != 0 || node_rid.size == 0)
        return -1;

    const std::string topic =
      zlink::spot_reqrep_internal::spot_routed_mesh_topic_for_node (
        std::string (reinterpret_cast<const char *> (node_rid.data),
                     node_rid.size));

    if (topic == runtime->routed_mesh_subscription_topic)
        return 0;

    if (!runtime->routed_mesh_subscription_topic.empty ())
        (void) zlink::spot_node_access_t::send_internal_subscription_update (
          node, runtime->routed_mesh_subscription_topic, false);

    if (zlink::spot_node_access_t::send_internal_subscription_update (
          node, topic, true)
        != 0)
        return -1;
    runtime->routed_mesh_subscription_topic = topic;
    return 0;
}

}

int zlink_service_spot_node_refresh_routed_mesh_subscription (void *node_handle_)
{
    if (!node_handle_) {
        errno = EFAULT;
        return -1;
    }
    zlink::spot_node_t *node = static_cast<zlink::spot_node_t *> (node_handle_);
    return ensure_spot_routed_mesh_subscription (node);
}

namespace
{

extern "C" void zlink_spot_request_reply_cleanup_spot (void *spot_);
extern "C" void zlink_timer_cleanup_spot (void *spot_);

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
    if (zlink::spot_node_access_t::try_register_spot_facade (node, spot) != 0) {
        const int err = errno;
        delete spot;
        errno = err;
        return NULL;
    }
    if (!zlink::spot_reqrep_internal::find_or_create_spot_state (spot)) {
        const int err = errno;
        zlink_spot_request_reply_cleanup_spot (spot);
        zlink::spot_node_access_t::unregister_spot_facade (node, spot);
        delete spot;
        errno = err;
        return NULL;
    }
    if (ensure_spot_routed_mesh_subscription (node) != 0) {
        const int err = errno;
        zlink_spot_request_reply_cleanup_spot (spot);
        zlink::spot_node_access_t::unregister_spot_facade (node, spot);
        delete spot;
        errno = err;
        return NULL;
    }
    return static_cast<void *> (spot);
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

    if (in_spot_node_send_ready_callback (node)
        || in_spot_node_monitor_callback (node)) {
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
    zlink::spot_reqrep_internal::unregister_spot_identity (state);

    zlink::part_helper_internal::cleanup_handle (spot);
    zlink_spot_request_reply_cleanup_spot (spot);
    zlink_timer_cleanup_spot (spot);
    zlink::spot_node_access_t::unregister_spot_facade (node, spot);
    zlink::destroy_spot_handle_for_testing (spot);
    *spot_p_ = NULL;
    return ZLINK_CLOSE_OK;
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

zlink_close_result_t zlink_spot_node_destroy (void **node_p_)
{
    if (!node_p_ || !*node_p_) {
        errno = EFAULT;
        return ZLINK_CLOSE_INVALID_HANDLE;
    }
    zlink::spot_node_t *node = zlink::spot_node_access_t::from_handle (*node_p_);
    if (!node)
        return ZLINK_CLOSE_INVALID_HANDLE;
    if (in_spot_node_send_ready_callback (node)
        || in_spot_node_monitor_callback (node)) {
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

zlink_bind_result_t zlink_spot_node_bind (void *node_, const char *endpoint_)
{
    zlink::spot_node_t *node = zlink::spot_node_access_t::from_handle (node_);
    if (!node) {
        errno = EFAULT;
        return ZLINK_BIND_INVALID_ARGUMENT;
    }
    return zlink::bind_result_internal::from_rc (
      zlink::spot_node_access_t::bind (node, endpoint_));
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

zlink_config_result_t zlink_spot_node_status_snapshot (void *node_,
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

zlink_config_result_t zlink_spot_node_peers_snapshot (void *node_,
                                                      zlink_spot_node_peer_entry_t *entries_,
                                                      size_t *count_)
{
    zlink::spot_node_t *node = zlink::spot_node_access_t::from_handle (node_);
    if (!node) {
        errno = EFAULT;
        return ZLINK_CONFIG_INVALID_HANDLE;
    }
    std::vector<zlink_spot_node_peer_entry_t> rows;
    if (zlink::spot_node_access_t::peers_snapshot (node, NULL, &rows) != 0)
        return zlink::config_result_internal::from_rc (-1);
    return zlink::config_result_internal::from_rc (
      copy_snapshot_rows (rows, entries_, count_));
}

zlink_config_result_t zlink_spot_node_peers_query (void *node_,
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

zlink_config_result_t zlink_spot_node_subjects_snapshot (
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
    return zlink::config_result_internal::from_rc (
      zlink::spot_node_access_t::attach_pub_ingress (node, pub_));
}
