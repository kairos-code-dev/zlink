/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "services/spot/pubsub/spot_subject_access.hpp"

int zlink_spot_subject_set_common_option_internal (void *handle_,
                                                   zlink_option_t option_,
                                                   const void *optval_,
                                                   size_t optvallen_)
{
    return spot_subject_set_common_option (handle_, option_, optval_, optvallen_);
}

int zlink_spot_subject_get_common_option_internal (void *handle_,
                                                   zlink_option_t option_,
                                                   void *optval_,
                                                   size_t *optvallen_)
{
    return spot_subject_get_common_option (handle_, option_, optval_, optvallen_);
}

int zlink_spot_subject_set_pub_option_internal (void *handle_,
                                                zlink_pub_option_t option_,
                                                const void *optval_,
                                                size_t optvallen_)
{
    return spot_subject_set_pub_option (handle_, option_, optval_, optvallen_);
}

int zlink_spot_subject_get_pub_option_internal (void *handle_,
                                                zlink_pub_option_t option_,
                                                void *optval_,
                                                size_t *optvallen_)
{
    return spot_subject_get_pub_option (handle_, option_, optval_, optvallen_);
}

int zlink_spot_subject_set_sub_option_internal (void *handle_,
                                                zlink_sub_option_t option_,
                                                const void *optval_,
                                                size_t optvallen_)
{
    return spot_subject_set_sub_option (handle_, option_, optval_, optvallen_);
}

int zlink_spot_subject_set_routing_id_internal (void *handle_, const void *data_, size_t size_)
{
    return spot_subject_set_routing_id (handle_, data_, size_);
}

int zlink_spot_subject_get_routing_id_internal (void *handle_, zlink_routing_id_t *out_)
{
    return spot_subject_get_routing_id (handle_, out_);
}

int zlink_spot_subject_set_tls_server_internal (void *handle_,
                                                const char *cert_,
                                                const char *key_,
                                                int require_client_cert_)
{
    return spot_subject_set_tls_server (handle_, cert_, key_, require_client_cert_);
}

int zlink_spot_subject_set_tls_client_internal (void *handle_,
                                                const char *ca_cert_,
                                                const char *hostname_,
                                                int trust_system_)
{
    return spot_subject_set_tls_client (handle_, ca_cert_, hostname_, trust_system_);
}
