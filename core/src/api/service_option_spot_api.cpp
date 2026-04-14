/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "utils/err.hpp"
#include "api/service_api_internal.hpp"

int zlink_service_spot_set_common_option_internal (void *handle_,
                                                   zlink_option_t option_,
                                                   int socket_option_,
                                                   const void *optval_,
                                                   size_t optvallen_)
{
    LIBZLINK_UNUSED (socket_option_);
    return spot_subject_set_common_option (handle_, option_, optval_, optvallen_);
}

int zlink_service_spot_get_common_option_internal (void *handle_,
                                                   zlink_option_t option_,
                                                   int socket_option_,
                                                   void *optval_,
                                                   size_t *optvallen_)
{
    LIBZLINK_UNUSED (socket_option_);
    return spot_subject_get_common_option (handle_, option_, optval_, optvallen_);
}

int zlink_service_spot_set_routing_id_internal (void *handle_,
                                                const void *data_,
                                                size_t size_)
{
    return spot_subject_set_routing_id (handle_, data_, size_);
}

int zlink_service_spot_get_routing_id_internal (void *handle_,
                                                zlink_routing_id_t *out_)
{
    return spot_subject_get_routing_id (handle_, out_);
}

int zlink_service_spot_set_admission_state_internal (
  void *handle_,
  zlink_admission_state_t state_)
{
    return spot_subject_set_admission_state (handle_, state_);
}

int zlink_service_spot_get_admission_state_internal (
  void *handle_,
  zlink_admission_state_t *state_out_)
{
    return spot_subject_get_admission_state (handle_, state_out_);
}

int zlink_service_spot_set_tls_server_internal (void *handle_,
                                                const char *cert_,
                                                const char *key_,
                                                int require_client_cert_)
{
    return spot_subject_set_tls_server (handle_, cert_, key_,
                                        require_client_cert_);
}

int zlink_service_spot_set_tls_client_internal (void *handle_,
                                                const char *ca_cert_,
                                                const char *hostname_,
                                                int trust_system_)
{
    return spot_subject_set_tls_client (handle_, ca_cert_, hostname_,
                                        trust_system_);
}

int zlink_service_spot_set_pub_option_internal (void *handle_,
                                                zlink_pub_option_t option_,
                                                int socket_option_,
                                                const void *optval_,
                                                size_t optvallen_)
{
    LIBZLINK_UNUSED (socket_option_);
    return spot_subject_set_pub_option (handle_, option_, optval_, optvallen_);
}

int zlink_service_spot_get_pub_option_internal (void *handle_,
                                                zlink_pub_option_t option_,
                                                int socket_option_,
                                                void *optval_,
                                                size_t *optvallen_)
{
    LIBZLINK_UNUSED (socket_option_);
    return spot_subject_get_pub_option (handle_, option_, optval_, optvallen_);
}

int zlink_service_spot_set_sub_option_internal (void *handle_,
                                                zlink_sub_option_t option_,
                                                int socket_option_,
                                                const void *optval_,
                                                size_t optvallen_)
{
    LIBZLINK_UNUSED (socket_option_);
    return spot_subject_set_sub_option (handle_, option_, optval_, optvallen_);
}

int zlink_service_spot_get_sub_option_internal (void *handle_,
                                                zlink_sub_option_t option_,
                                                int socket_option_,
                                                void *optval_,
                                                size_t *optvallen_)
{
    LIBZLINK_UNUSED (socket_option_);
    return spot_subject_get_sub_option (handle_, option_, optval_, optvallen_);
}

int zlink_service_spot_set_subscription_internal (void *handle_,
                                                  const char *filter_)
{
    return spot_subject_set_subscription (handle_, filter_);
}

int zlink_service_spot_unset_subscription_internal (void *handle_,
                                                    const char *filter_)
{
    return spot_subject_unset_subscription (handle_, filter_);
}

int zlink_service_spot_subscription_at_internal (void *handle_,
                                                 size_t index_,
                                                 char *filter_out_,
                                                 size_t *filter_len_inout_,
                                                 int *is_pattern_out_)
{
    return spot_subject_subscription_at (handle_, index_, filter_out_,
                                         filter_len_inout_, is_pattern_out_);
}
