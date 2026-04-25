/* SPDX-License-Identifier: MPL-2.0 */

#include "precompiled.hpp"

#include "services/spot/spot_data_plane_internal.hpp"
#include "services/spot/spot_data_plane_message_io_internal.hpp"
#include "services/spot/spot_message_parts_internal.hpp"

#include "api/request_reply_protocol_internal.hpp"
#include "core/multipart_send_txn.hpp"
#include "services/spot/spot_node.hpp"
#include "services/spot/spot_runtime.hpp"
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
  spot_runtime_t *runtime_,
  spot_data_plane_runtime_state_t *state_,
  const spot_node_t *node_)
{
    if (!src_ || !runtime_ || !state_) {
        errno = EINVAL;
        return -1;
    }
    (void) runtime_;
    (void) state_;

    unsigned int forwarded_messages = 0;
    size_t forwarded_bytes = 0;

    for (;;) {
        std::string topic;
        spot_owned_msg_parts_t parts;
        size_t wire_bytes = 0;
        if (spot_recv_logical_message_parts (
              src_, true, &topic, &parts, &wire_bytes)
            != 0) {
            if (errno == EAGAIN)
                return 0;
            return -1;
        }

        const bool forward_to_fanout =
          fanout_ && node_ && node_->has_local_filtered_subs ();
        if (forward_to_fanout && !mesh_pub_) {
            if (spot_publish_msg_parts_consume (fanout_, topic, &parts) != 0)
                return -1;
        } else if (forward_to_fanout
                   && spot_publish_msg_parts (fanout_, topic, parts) != 0) {
            spot_clear_msg_parts (&parts);
            return -1;
        }

        if (mesh_pub_
            && spot_publish_msg_parts_consume (mesh_pub_, topic, &parts) != 0)
            return -1;

        spot_clear_msg_parts (&parts);

        ++forwarded_messages;
        forwarded_bytes += wire_bytes;
        if (forwarded_messages >= ingress_forward_batch_limit
            || forwarded_bytes >= ingress_forward_batch_bytes_limit)
            return 0;
    }
}
}
