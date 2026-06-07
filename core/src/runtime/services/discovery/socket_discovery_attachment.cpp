/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "services/discovery/socket_discovery_attachment.hpp"
#include "services/discovery/discovery_protocol.hpp"

namespace zlink
{
socket_discovery_attachment_t::socket_discovery_attachment_t (socket_base_t *socket_) :
    _socket (socket_),
    _discovery (NULL),
    _local_role (discovery_protocol::service_role_invalid),
    _registered (false),
    _shutdown_requested (false),
    _refresh_seq (0)
{
}

socket_discovery_attachment_t::~socket_discovery_attachment_t ()
{
}

bool socket_discovery_attachment_t::public_api_forbidden (int *errno_out_) const
{
    scoped_lock_t lock (_sync);
    if (!_discovery)
        return false;
    if (errno_out_)
        *errno_out_ = _shutdown_requested ? ESHUTDOWN : EFSM;
    return true;
}

}
