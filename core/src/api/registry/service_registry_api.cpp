/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "utils/err.hpp"
#include "api/service/service_api_internal.hpp"
#include "api/message/bind_result_internal.hpp"
#include "api/core/close_result_internal.hpp"
#include "api/core/config_result_internal.hpp"
#include "api/message/connect_result_internal.hpp"

#include <vector>

#include "services/registry/registry_access.hpp"
#include "services/registry/registry_query_access.hpp"

void *zlink_registry_new (void *ctx_)
{
    if (!ctx_ || !(static_cast<zlink::ctx_t *> (ctx_))->check_tag ()) {
        errno = EFAULT;
        return NULL;
    }
    void *registry =
      zlink::registry_access_t::create (static_cast<zlink::ctx_t *> (ctx_));
    register_registry_handle (registry);
    return registry;
}

zlink_bind_result_t zlink_registry_bind (void *registry_,
                                         const char *pub_endpoint_,
                                         const char *router_endpoint_)
{
    zlink::registry_t *registry =
      zlink::registry_access_t::from_handle (registry_);
    if (!registry) {
        errno = EFAULT;
        return ZLINK_BIND_INVALID_ARGUMENT;
    }
    return zlink::bind_result_internal::from_rc (
      zlink::registry_access_t::bind (registry, pub_endpoint_,
                                       router_endpoint_));
}

zlink_config_result_t zlink_registry_set_id (void *registry_, uint32_t registry_id_)
{
    return zlink_registry_set (registry_, ZLINK_REGISTRY_OPT_ID,
                               registry_id_);
}

zlink_config_result_t zlink_registry_set (void *registry_,
                                          zlink_registry_option_t option_,
                                          uint32_t value_)
{
    zlink::registry_t *registry =
      zlink::registry_access_t::from_handle (registry_);
    if (!registry) {
        errno = EFAULT;
        return ZLINK_CONFIG_INVALID_HANDLE;
    }
    if (value_ == 0) {
        errno = EINVAL;
        return ZLINK_CONFIG_INVALID_ARGUMENT;
    }
    return zlink::config_result_internal::from_rc (
      zlink::registry_access_t::set_option (registry, option_, value_));
}

uint32_t zlink_registry_get (void *registry_,
                             zlink_registry_option_t option_,
                             zlink_config_result_t *error_out_)
{
    zlink::registry_t *registry =
      zlink::registry_access_t::from_handle (registry_);
    if (!registry) {
        errno = EFAULT;
        if (error_out_)
            *error_out_ = ZLINK_CONFIG_INVALID_HANDLE;
        return 0;
    }

    uint32_t value = 0;
    const int rc = zlink::registry_access_t::get_option (registry, option_,
                                                         &value);
    if (rc != 0) {
        if (error_out_)
            *error_out_ = zlink::config_result_internal::from_rc (-1);
        return 0;
    }
    if (error_out_)
        *error_out_ = ZLINK_CONFIG_OK;
    return value;
}

zlink_config_result_t zlink_registry_add_peer (void *registry_, const char *peer_pub_endpoint_)
{
    zlink::registry_t *registry =
      zlink::registry_access_t::from_handle (registry_);
    if (!registry) {
        errno = EFAULT;
        return ZLINK_CONFIG_INVALID_HANDLE;
    }
    return zlink::config_result_internal::from_rc (
      zlink::registry_access_t::add_peer (registry, peer_pub_endpoint_));
}

zlink_config_result_t zlink_registry_set_heartbeat (void *registry_,
                                                    uint32_t interval_ms_,
                                                    uint32_t timeout_ms_)
{
    zlink::registry_t *registry =
      zlink::registry_access_t::from_handle (registry_);
    if (!registry) {
        errno = EFAULT;
        return ZLINK_CONFIG_INVALID_HANDLE;
    }
    return zlink::config_result_internal::from_rc (
      zlink::registry_access_t::set_heartbeat (registry, interval_ms_,
                                                timeout_ms_));
}

zlink_config_result_t zlink_registry_set_broadcast_interval (void *registry_,
                                                             uint32_t interval_ms_)
{
    return zlink_registry_set (
      registry_, ZLINK_REGISTRY_OPT_BROADCAST_INTERVAL_MS, interval_ms_);
}

zlink_close_result_t zlink_registry_destroy (void **registry_p_)
{
    if (!registry_p_ || !*registry_p_) {
        errno = EFAULT;
        return ZLINK_CLOSE_INVALID_HANDLE;
    }
    zlink::registry_t *registry =
      zlink::registry_access_t::from_handle (*registry_p_);
    if (!registry)
        return ZLINK_CLOSE_INVALID_HANDLE;
    if (zlink::registry_access_t::destroy (registry) != 0)
        return zlink::close_result_internal::from_rc (-1);
    zlink::registry_access_t::delete_handle (registry);
    *registry_p_ = NULL;
    return ZLINK_CLOSE_OK;
}

zlink_config_result_t zlink_registry_topology_snapshot (void *registry_,
                                                        zlink_registry_topology_entry_t *entries_,
                                                        size_t *count_)
{
    zlink::registry_t *registry =
      zlink::registry_access_t::from_handle (registry_);
    if (!registry) {
        errno = EFAULT;
        return ZLINK_CONFIG_INVALID_HANDLE;
    }
    return zlink::config_result_internal::from_rc (
      zlink::registry_access_t::topology_snapshot (registry, entries_,
                                                    count_));
}

zlink_config_result_t zlink_registry_status_snapshot (void *registry_,
                                                      zlink_registry_status_t *out_)
{
    zlink::registry_t *registry =
      zlink::registry_access_t::from_handle (registry_);
    if (!registry) {
        errno = EFAULT;
        return ZLINK_CONFIG_INVALID_HANDLE;
    }
    return zlink::config_result_internal::from_rc (
      zlink::registry_access_t::status_snapshot (registry, out_));
}

zlink_config_result_t zlink_registry_service_summary_snapshot (
  void *registry_,
  const zlink_registry_service_summary_filter_t *filter_,
  zlink_registry_service_summary_entry_t *entries_,
  size_t *count_)
{
    zlink::registry_t *registry =
      zlink::registry_access_t::from_handle (registry_);
    if (!registry) {
        errno = EFAULT;
        return ZLINK_CONFIG_INVALID_HANDLE;
    }
    if (!count_) {
        errno = EINVAL;
        return ZLINK_CONFIG_INVALID_ARGUMENT;
    }
    std::vector<zlink_registry_service_summary_entry_t> rows;
    if (zlink::registry_access_t::service_summary_snapshot (
          registry, filter_, &rows)
        != 0)
        return zlink::config_result_internal::from_rc (-1);
    if (!entries_) {
        *count_ = rows.size ();
        return ZLINK_CONFIG_OK;
    }
    if (*count_ < rows.size ()) {
        *count_ = rows.size ();
        errno = ENOBUFS;
        return ZLINK_CONFIG_INVALID_ARGUMENT;
    }
    for (size_t i = 0; i < rows.size (); ++i)
        entries_[i] = rows[i];
    *count_ = rows.size ();
    return ZLINK_CONFIG_OK;
}

zlink_config_result_t zlink_registry_member_peers (void *registry_,
                                                    const char *channel_name_,
                                                    zlink_member_peer_entry_t *entries_,
                                                    size_t *count_)
{
    zlink::registry_t *registry =
      zlink::registry_access_t::from_handle (registry_);
    if (!registry) {
        errno = EFAULT;
        return ZLINK_CONFIG_INVALID_HANDLE;
    }
    return zlink::config_result_internal::from_rc (
      zlink::registry_access_t::member_peers (
        registry, channel_name_, entries_, count_));
}

zlink_config_result_t zlink_registry_topology_query (
  void *registry_,
  const zlink_registry_topology_filter_t *filter_,
  zlink_registry_topology_entry_t *entries_,
  size_t *count_)
{
    zlink::registry_t *registry =
      zlink::registry_access_t::from_handle (registry_);
    if (!registry) {
        errno = EFAULT;
        return ZLINK_CONFIG_INVALID_HANDLE;
    }
    return zlink::config_result_internal::from_rc (
      zlink::registry_access_t::topology_query (registry, filter_, entries_,
                                                 count_));
}

void *zlink_registry_query_client_new (void *ctx_)
{
    if (!ctx_ || !(static_cast<zlink::ctx_t *> (ctx_))->check_tag ()) {
        errno = EFAULT;
        return NULL;
    }
    return zlink::registry_query_access_t::create (
      static_cast<zlink::ctx_t *> (ctx_));
}

zlink_connect_result_t zlink_registry_query_client_connect (void *client_,
                                                            const char *endpoint_)
{
    return zlink::connect_result_internal::from_rc (
      zlink::registry_query_access_t::connect (client_, endpoint_));
}

zlink_config_result_t zlink_registry_query_snapshot (
  void *client_,
  const zlink_registry_topology_filter_t *filter_,
  zlink_registry_topology_entry_t *entries_,
  size_t *count_)
{
    return zlink::config_result_internal::from_rc (
      zlink::registry_query_access_t::topology_query (client_, filter_,
                                                       entries_, count_));
}

zlink_close_result_t zlink_registry_query_destroy (void **client_p_)
{
    return zlink::close_result_internal::from_rc (
      zlink::registry_query_access_t::destroy (client_p_));
}
