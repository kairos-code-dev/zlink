/* SPDX-License-Identifier: MPL-2.0 */

#include "precompiled.hpp"

#include "services/spot/node/spot_node.hpp"

namespace zlink
{

int spot_node_t::set_tls_server (const char *cert_, const char *key_)
{
    service_public_api_scope_t admission (_public_api);
    if (!admission.acquired ())
        return -1;

    if (!cert_ || !key_ || cert_[0] == '\0' || key_[0] == '\0') {
        errno = EINVAL;
        return -1;
    }

    scoped_lock_t lock (_sync);
    if (_tls_state.server_tls_locked || !_endpoint_state.bound_endpoint.empty ()) {
        errno = EBUSY;
        return -1;
    }
    _tls_state.tls_cert = cert_;
    _tls_state.tls_key = key_;
    return 0;
}

int spot_node_t::set_tls_client (const char *ca_cert_,
                                 const char *hostname_,
                                 int trust_system_)
{
    service_public_api_scope_t admission (_public_api);
    if (!admission.acquired ())
        return -1;

    if (trust_system_ < 0) {
        errno = EINVAL;
        return -1;
    }

    scoped_lock_t lock (_sync);
    if (_tls_state.mesh_client_tls_locked || _tls_state.registration_tls_locked) {
        errno = EBUSY;
        return -1;
    }
    _tls_state.tls_ca = ca_cert_ ? ca_cert_ : "";
    _tls_state.tls_hostname = hostname_ ? hostname_ : "";
    _tls_state.tls_trust_system = trust_system_;
    return 0;
}

}
