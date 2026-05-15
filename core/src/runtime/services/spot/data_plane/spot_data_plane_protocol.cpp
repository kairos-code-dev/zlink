/* SPDX-License-Identifier: MPL-2.0 */

#include "precompiled.hpp"

#include "services/spot/data_plane/spot_data_plane_internal.hpp"
#include "services/spot/data_plane/spot_data_plane_message_io_internal.hpp"
#include "services/spot/common/spot_message_parts_internal.hpp"
#include "services/spot/data_plane/spot_mesh_pub_hwm.hpp"

#include "api/socket/request_reply_protocol_internal.hpp"
#include "api/spot/request_reply/service_spot_request_reply_internal.hpp"
#include "services/spot/common/spot_control_protocol.hpp"
#include "services/spot/common/spot_debug.hpp"
#include "services/spot/node/spot_node.hpp"
#include "services/spot/node/spot_node_access.hpp"
#include "services/spot/runtime/spot_runtime.hpp"

#include "services/common/monitor_decode.hpp"
#include "sockets/common/socket_base.hpp"
#include "utils/clock.hpp"
#include "utils/env.hpp"
#include "utils/err.hpp"

#include <errno.h>
#include <map>
#include <set>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace zlink
{
namespace
{
namespace spot_io = zlink::spot_data_plane_message_io;

static void spot_ready_ack_ctrl_debugf (const char *fmt_, ...)
{
    va_list args;
    va_start (args, fmt_);
    debug_vfprintf_with_file ("ZLINK_DEBUG_SPOT_READY_ACK",
                              "[spot-ready-ack-ctrl] ",
                              spot_debug::ready_ack_log_path, fmt_, args);
    va_end (args);
}

static uint64_t default_bootstrap_broadcast_interval_ms (
  const spot_runtime_t *runtime_)
{
    if (runtime_) {
        const std::string &bound_endpoint = runtime_->bound_endpoint;
        if (bound_endpoint.compare (0, 6, "tcp://") == 0
            || bound_endpoint.compare (0, 6, "tls://") == 0) {
            return 5000;
        }
    }

    return 1000;
}

}

int spot_data_plane_protocol_t::recv_ascii_command (
  socket_base_t *socket_, std::vector<std::string> *frames_)
{
    if (!frames_)
        return -1;
    frames_->clear ();
    while (true) {
        msg_t frame;
        if (frame.init () != 0)
            return -1;
        if (socket_->recv (&frame, 0) != 0) {
            frame.close ();
            return -1;
        }
        frames_->push_back (std::string (
          static_cast<const char *> (frame.data ()), frame.size ()));
        const bool more = (frame.flags () & msg_t::more) != 0;
        frame.close ();
        if (!more)
            break;
    }
    return frames_->empty () ? -1 : 0;
}

int spot_data_plane_protocol_t::send_subscription_update (
  socket_base_t *socket_, const std::string &raw_filter_, bool subscribe_)
{
    if (!socket_) {
        errno = EFAULT;
        return -1;
    }

    msg_t msg;
    if (msg.init_size (raw_filter_.size () + 1) != 0)
        return -1;

    unsigned char *data = static_cast<unsigned char *> (msg.data ());
    data[0] = subscribe_ ? 1 : 0;
    if (!raw_filter_.empty ())
        memcpy (data + 1, raw_filter_.data (), raw_filter_.size ());

    const int rc = socket_->send (&msg, 0);
    msg.close ();
    return rc;
}

int spot_data_plane_protocol_t::send_errno_reply (socket_base_t *socket_,
                                                  int error_)
{
    char buf[32];
    snprintf (buf, sizeof (buf), "%d", error_);
    if (spot_io::send_ascii_frame (
          socket_, spot_control_protocol::reply_error, ZLINK_SNDMORE)
        != 0)
        return -1;
    return spot_io::send_ascii_frame (socket_, buf, 0);
}

int spot_data_plane_protocol_t::send_ok_reply (socket_base_t *socket_)
{
    return spot_io::send_ascii_frame (
      socket_, spot_control_protocol::reply_ok, 0);
}

uint64_t spot_data_plane_protocol_t::resolve_bootstrap_broadcast_interval_ms (
  const spot_runtime_t *runtime_, bool bootstrap_ready_)
{
    static uint64_t env_cached = 0;
    static bool env_checked = false;
    if (env_checked)
        return env_cached != 0 ? env_cached
                               : (bootstrap_ready_
                                    ? default_bootstrap_broadcast_interval_ms (
                                        runtime_)
                                    : 1000);

    env_cached = env::positive_u64 ("ZLINK_SPOT_BOOTSTRAP_INTERVAL_MS", 0);
    env_checked = true;
    return env_cached != 0 ? env_cached
                           : (bootstrap_ready_
                                ? default_bootstrap_broadcast_interval_ms (
                                    runtime_)
                                : 1000);
}

}
