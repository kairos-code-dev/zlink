/* SPDX-License-Identifier: MPL-2.0 */

#include "precompiled.hpp"

#include "services/spot/spot_data_plane_internal.hpp"

#include "services/spot/spot_node.hpp"
#include "sockets/socket_base.hpp"
#include "utils/err.hpp"

#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

namespace zlink
{
namespace
{
static const unsigned int ingress_forward_batch_limit = 2048;
// Bound one ingress burst by bytes as well as message count so large payloads
// cannot monopolize the data-plane thread for an entire poll cycle.
static const size_t ingress_forward_batch_bytes_limit = 16 * 1024 * 1024;
}

void spot_data_plane_forwarder_t::pump_socket_commands (socket_base_t *socket_)
{
    if (!socket_)
        return;

    uint32_t ignored = 0;
    const int rc = socket_->get_events_internal (0, &ignored);
    if (rc == 0)
        return;

    if (errno == EINTR || errno == ETERM)
        return;

    errno_assert (false);
}

int spot_data_plane_forwarder_t::resolve_internal_hwm_override (
  const char *env_name_, int default_value_)
{
    if (!env_name_ || env_name_[0] == '\0')
        return default_value_;

    const char *value = getenv (env_name_);
    if (!value || value[0] == '\0')
        return default_value_;

    char *end = NULL;
    errno = 0;
    const long parsed = strtol (value, &end, 10);
    if (errno != 0 || end == value)
        return default_value_;
    if (parsed < 0)
        return 0;
    if (parsed > INT_MAX)
        return INT_MAX;
    return static_cast<int> (parsed);
}

int spot_data_plane_forwarder_t::recv_and_forward_ingress (
  socket_base_t *src_,
  socket_base_t *mesh_pub_,
  socket_base_t *fanout_,
  const spot_node_t *node_)
{
    bool receiving_multipart = false;
    bool forward_to_fanout = false;
    unsigned int forwarded_messages = 0;
    size_t forwarded_bytes = 0;

    for (;;) {
        msg_t msg;
        if (msg.init () != 0)
            return -1;
        const int recv_flags = receiving_multipart ? 0 : ZLINK_DONTWAIT;
        if (src_->recv (&msg, recv_flags) != 0) {
            const int err = errno;
            msg.close ();
            if (err == EAGAIN && !receiving_multipart)
                return 0;
            errno = err != 0 ? err : EPROTO;
            return -1;
        }

        const bool more = (msg.flags () & msg_t::more) != 0;
        forwarded_bytes += msg.size ();

        if (!receiving_multipart)
            forward_to_fanout = fanout_ && node_ && node_->has_local_filtered_subs ();

        if (mesh_pub_ && forward_to_fanout) {
            msg_t copy;
            if (copy.init () != 0) {
                msg.close ();
                return -1;
            }
            if (copy.copy (msg) != 0) {
                copy.close ();
                msg.close ();
                return -1;
            }
            if (mesh_pub_->send (&msg, more ? ZLINK_SNDMORE : 0) != 0) {
                copy.close ();
                msg.close ();
                return -1;
            }
            if (fanout_->send (&copy, more ? ZLINK_SNDMORE : 0) != 0) {
                copy.close ();
                return -1;
            }
            copy.close ();
        } else if (mesh_pub_) {
            if (mesh_pub_->send (&msg, more ? ZLINK_SNDMORE : 0) != 0) {
                msg.close ();
                return -1;
            }
        } else if (forward_to_fanout) {
            if (fanout_->send (&msg, more ? ZLINK_SNDMORE : 0) != 0) {
                msg.close ();
                return -1;
            }
        }

        receiving_multipart = more;
        if (!receiving_multipart) {
            forward_to_fanout = false;
            ++forwarded_messages;
            if (forwarded_messages >= ingress_forward_batch_limit
                || forwarded_bytes >= ingress_forward_batch_bytes_limit)
                return 0;
        }
        msg.close ();
    }
}

}
