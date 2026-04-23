/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "utils/err.hpp"
#include "api/service_handle_internal.hpp"
#include "api/service_surface_internal.hpp"

#include "services/discovery/discovery_access.hpp"
#include "services/discovery/registry_access.hpp"

namespace
{
bool is_spot_service_handle_kind (zlink::service_handle_kind_t kind_)
{
    switch (kind_) {
        case zlink::service_handle_spot_pub_side:
        case zlink::service_handle_spot_sub_side:
        case zlink::service_handle_spot:
        case zlink::service_handle_spot_node:
            return true;
        default:
            return false;
    }
}
}

int zlink_service_set_common_option (void *handle_,
                                     zlink_option_t option_,
                                     int socket_option_,
                                     const void *optval_,
                                     size_t optvallen_)
{
    const zlink::service_handle_resolution_t resolved =
      zlink::resolve_service_handle (handle_);

    if (resolved.kind == zlink::service_handle_discovery)
        return zlink::discovery_access_t::set_option (
          resolved.discovery, socket_option_, optval_, optvallen_);
    if (!is_spot_service_handle_kind (resolved.kind)) {
        errno = EFAULT;
        return -1;
    }
    if (zlink_service_spot_set_common_option_internal (
          handle_, option_, socket_option_, optval_, optvallen_)
        == 0)
        return 0;
    if (errno != EFAULT)
        return -1;

    errno = EFAULT;
    return -1;
}

int zlink_service_get_common_option (void *handle_,
                                     zlink_option_t option_,
                                     int socket_option_,
                                     void *optval_,
                                     size_t *optvallen_)
{
    const zlink::service_handle_resolution_t resolved =
      zlink::resolve_service_handle (handle_);

    if (resolved.kind == zlink::service_handle_discovery)
        return zlink::discovery_access_t::get_option (
          resolved.discovery, socket_option_, optval_, optvallen_);
    if (!is_spot_service_handle_kind (resolved.kind)) {
        errno = EFAULT;
        return -1;
    }
    errno = 0;
    if (zlink_service_spot_get_common_option_internal (
          handle_, option_, socket_option_, optval_, optvallen_)
        == 0)
        return 0;
    if (errno != EFAULT)
        return -1;

    errno = EFAULT;
    return -1;
}

int zlink_service_set_routing_id (void *handle_,
                                  const void *data_,
                                  size_t size_)
{
    const zlink::service_handle_resolution_t resolved =
      zlink::resolve_service_handle (handle_);

    if (resolved.kind == zlink::service_handle_discovery)
        return zlink::discovery_access_t::set_routing_id (
          resolved.discovery, data_, size_);
    if (!is_spot_service_handle_kind (resolved.kind)) {
        errno = EFAULT;
        return -1;
    }
    if (zlink_service_spot_set_routing_id_internal (handle_, data_, size_) == 0)
        return 0;
    if (errno != EFAULT)
        return -1;

    errno = EFAULT;
    return -1;
}

int zlink_service_get_routing_id (void *handle_, zlink_routing_id_t *out_)
{
    const zlink::service_handle_resolution_t resolved =
      zlink::resolve_service_handle (handle_);

    if (resolved.kind == zlink::service_handle_discovery)
        return zlink::discovery_access_t::routing_id (resolved.discovery, out_);
    if (!is_spot_service_handle_kind (resolved.kind)) {
        errno = EFAULT;
        return -1;
    }
    if (zlink_service_spot_get_routing_id_internal (handle_, out_) == 0)
        return 0;
    if (errno != EFAULT)
        return -1;

    errno = EFAULT;
    return -1;
}

int zlink_service_set_admission_state (void *handle_,
                                       zlink_admission_state_t state_)
{
    const zlink::service_handle_resolution_t resolved =
      zlink::resolve_service_handle (handle_);

    if (!is_spot_service_handle_kind (resolved.kind)) {
        errno = EFAULT;
        return -1;
    }
    if (zlink_service_spot_set_admission_state_internal (handle_, state_) == 0)
        return 0;
    if (errno != EFAULT)
        return -1;

    errno = EFAULT;
    return -1;
}

int zlink_service_get_admission_state (void *handle_,
                                       zlink_admission_state_t *state_out_)
{
    const zlink::service_handle_resolution_t resolved =
      zlink::resolve_service_handle (handle_);

    if (!is_spot_service_handle_kind (resolved.kind)) {
        errno = EFAULT;
        return -1;
    }
    if (zlink_service_spot_get_admission_state_internal (handle_, state_out_)
        == 0) {
        return 0;
    }
    if (errno != EFAULT)
        return -1;

    errno = EFAULT;
    return -1;
}

int zlink_service_set_tls_server (void *handle_,
                                  const char *cert_,
                                  const char *key_,
                                  int require_client_cert_)
{
    const zlink::service_handle_resolution_t resolved =
      zlink::resolve_service_handle (handle_);

    if (resolved.kind == zlink::service_handle_registry)
        return zlink::registry_access_t::set_tls_server (
          resolved.registry, cert_, key_, require_client_cert_);
    if (!is_spot_service_handle_kind (resolved.kind)) {
        errno = EFAULT;
        return -1;
    }
    if (zlink_service_spot_set_tls_server_internal (
          handle_, cert_, key_, require_client_cert_)
        == 0)
        return 0;
    if (errno != EFAULT)
        return -1;

    errno = EFAULT;
    return -1;
}

int zlink_service_set_tls_client (void *handle_,
                                  const char *ca_cert_,
                                  const char *hostname_,
                                  int trust_system_)
{
    const zlink::service_handle_resolution_t resolved =
      zlink::resolve_service_handle (handle_);

    if (resolved.kind == zlink::service_handle_discovery)
        return zlink::discovery_access_t::set_tls_client (
          resolved.discovery, ca_cert_, hostname_, trust_system_);
    if (resolved.kind == zlink::service_handle_registry)
        return zlink::registry_access_t::set_tls_client (
          resolved.registry, ca_cert_, hostname_, trust_system_);
    if (!is_spot_service_handle_kind (resolved.kind)) {
        errno = EFAULT;
        return -1;
    }
    if (zlink_service_spot_set_tls_client_internal (
          handle_, ca_cert_, hostname_, trust_system_)
        == 0)
        return 0;
    if (errno != EFAULT)
        return -1;

    errno = EFAULT;
    return -1;
}

int zlink_service_set_router_option (void *handle_,
                                     zlink_router_option_t option_,
                                     int socket_option_,
                                     const void *optval_,
                                     size_t optvallen_)
{
    LIBZLINK_UNUSED (handle_);
    LIBZLINK_UNUSED (option_);
    LIBZLINK_UNUSED (socket_option_);
    LIBZLINK_UNUSED (optval_);
    LIBZLINK_UNUSED (optvallen_);
    errno = EFAULT;
    return -1;
}

int zlink_service_get_router_option (void *handle_,
                                     zlink_router_option_t option_,
                                     int socket_option_,
                                     void *optval_,
                                     size_t *optvallen_)
{
    LIBZLINK_UNUSED (handle_);
    LIBZLINK_UNUSED (option_);
    LIBZLINK_UNUSED (socket_option_);
    LIBZLINK_UNUSED (optval_);
    LIBZLINK_UNUSED (optvallen_);
    errno = EFAULT;
    return -1;
}

int zlink_service_set_pub_option (void *handle_,
                                  zlink_pub_option_t option_,
                                  int socket_option_,
                                  const void *optval_,
                                  size_t optvallen_)
{
    if (zlink_service_spot_set_pub_option_internal (
          handle_, option_, socket_option_, optval_, optvallen_)
        == 0)
        return 0;
    if (errno != EFAULT)
        return -1;

    LIBZLINK_UNUSED (socket_option_);
    errno = EFAULT;
    return -1;
}

int zlink_service_get_pub_option (void *handle_,
                                  zlink_pub_option_t option_,
                                  int socket_option_,
                                  void *optval_,
                                  size_t *optvallen_)
{
    if (zlink_service_spot_get_pub_option_internal (
          handle_, option_, socket_option_, optval_, optvallen_)
        == 0)
        return 0;
    if (errno != EFAULT)
        return -1;

    errno = EFAULT;
    return -1;
}

int zlink_service_set_sub_option (void *handle_,
                                  zlink_sub_option_t option_,
                                  int socket_option_,
                                  const void *optval_,
                                  size_t optvallen_)
{
    if (zlink_service_spot_set_sub_option_internal (
          handle_, option_, socket_option_, optval_, optvallen_)
        == 0)
        return 0;
    if (errno != EFAULT)
        return -1;
    errno = EFAULT;
    return -1;
}

int zlink_service_get_sub_option (void *handle_,
                                  zlink_sub_option_t option_,
                                  int socket_option_,
                                  void *optval_,
                                  size_t *optvallen_)
{
    if (zlink_service_spot_get_sub_option_internal (
          handle_, option_, socket_option_, optval_, optvallen_)
        == 0)
        return 0;
    if (errno != EFAULT)
        return -1;

    errno = EFAULT;
    return -1;
}

int zlink_service_set_subscription (void *handle_, const char *filter_)
{
    const zlink::service_handle_resolution_t resolved =
      zlink::resolve_service_handle (handle_);

    if (!is_spot_service_handle_kind (resolved.kind)) {
        errno = EFAULT;
        return -1;
    }
    if (zlink_service_spot_set_subscription_internal (handle_, filter_) == 0)
        return 0;
    if (errno != EFAULT)
        return -1;

    errno = EFAULT;
    return -1;
}

int zlink_service_unset_subscription (void *handle_, const char *filter_)
{
    const zlink::service_handle_resolution_t resolved =
      zlink::resolve_service_handle (handle_);

    if (!is_spot_service_handle_kind (resolved.kind)) {
        errno = EFAULT;
        return -1;
    }
    if (zlink_service_spot_unset_subscription_internal (handle_, filter_) == 0)
        return 0;
    if (errno != EFAULT)
        return -1;

    errno = EFAULT;
    return -1;
}

int zlink_service_subscription_at (void *handle_,
                                   size_t index_,
                                   char *filter_out_,
                                   size_t *filter_len_inout_,
                                   int *is_pattern_out_)
{
    const zlink::service_handle_resolution_t resolved =
      zlink::resolve_service_handle (handle_);

    if (!is_spot_service_handle_kind (resolved.kind)) {
        errno = EFAULT;
        return -1;
    }
    if (zlink_service_spot_subscription_at_internal (
          handle_, index_, filter_out_, filter_len_inout_, is_pattern_out_)
        == 0)
        return 0;
    if (errno != EFAULT)
        return -1;

    errno = EFAULT;
    return -1;
}
